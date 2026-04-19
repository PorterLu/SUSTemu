/*
 * src/cpu/pipeline.c — 5-stage in-order pipeline (Phase 2)
 *
 * Stages (executed in reverse order each cycle to avoid data races):
 *   WB  — write result to register file, retire instruction
 *   MEM — perform load/store via ir_mem_access()
 *   EX  — run exec_fn (ALU + address compute); detect control hazards
 *   ID  — forward from EX/MEM; detect load-use hazards
 *   IF  — fetch next instruction from memory
 *
 * Hazard handling:
 *   Load-use : detected in ID when EX holds a LOAD targeting a source reg.
 *              Action: insert bubble into EX, freeze IF and ID (stall 1 cycle).
 *   Control  : detected in EX when dnpc != snpc (taken branch or jump).
 *              Action: flush ID latch (2 instructions flushed: one in IF/ID).
 *              EX stage sets fetch_pc = dnpc for the corrected fetch stream.
 *
 * Forwarding paths (implemented in stage_ID):
 *   EX  → ID : ALU result from the instruction currently in EX (non-load)
 *   MEM → ID : result from the instruction in MEM (includes load results
 *              that are available after ir_mem_access() has run)
 *
 * Note: stage_WB() runs first each cycle so that its GPR write is visible
 * to stage_IF()'s ir_decode() call later in the same cycle, giving free
 * "WB → ID" forwarding through the register file.
 */

#include <pipeline.h>
#include <core.h>
#include <bpred.h>
#include <ir.h>
#include <reg.h>
#include <state.h>
#include <vmem.h>
#include <log.h>
#include <watchpoint.h>
#include <decode.h>
#include <inttypes.h>
#include <csr.h>
#include <exec_cfg.h>
#include <stdlib.h>

/* ── Global pipeline state ──────────────────────────────────────────────── */
Pipeline  cpu_pipe;
PipeStats pipe_stats;

/* ── Forward declarations ───────────────────────────────────────────────── */
static void stage_WB(void);
static void stage_MEM(void);
static void stage_EX(void);
static void stage_ID(void);
static void stage_IF(void);
static void pipeline_trace_cycle(void);

/* ── pipeline_init ──────────────────────────────────────────────────────── */

void pipeline_init(void)
{
    /* Point the four latch pointers at the backing slots */
    cpu_pipe.id  = &cpu_pipe.slots[0];
    cpu_pipe.ex  = &cpu_pipe.slots[1];
    cpu_pipe.mem = &cpu_pipe.slots[2];
    cpu_pipe.wb  = &cpu_pipe.slots[3];

    cpu_pipe.id->valid   = 0;
    cpu_pipe.ex->valid   = 0;
    cpu_pipe.mem->valid  = 0;
    cpu_pipe.wb->valid   = 0;
    cpu_pipe.fetch_pc   = cpu.pc;
    cpu_pipe.mem_stall_rem = 0;

    pipe_stats.cycles          = 0;
    pipe_stats.insts           = 0;
    pipe_stats.stall_load_use  = 0;
    pipe_stats.stall_control   = 0;
    pipe_stats.stall_cache_miss = 0;

    if (g_bpred_mode) bpred_init(&bpred);
}

/* ── pipeline_cycle ─────────────────────────────────────────────────────── */
/*
 * Advance every stage by one clock.  Stages run in reverse pipeline order
 * (WB first, IF last) so that each stage sees the latch values written by
 * the *previous* cycle's upstream stage, not the current cycle's.
 *
 * When a cache miss is detected in stage_MEM, cpu_pipe.mem_stall_rem is set to
 * the remaining wait cycles.  The normal end-of-cycle rotate runs as usual,
 * moving the load into the wb slot.  For the stall cycles the pipeline is fully
 * frozen (no stage runs) — the load sits in wb until the stall expires, at
 * which point stage_WB retires it normally.
 */
static int g_pipe_stalled;  /* set by stage_ID on load-use stall */
static int g_pipe_rotated;  /* reserved — currently unused */
/* The slot that stage_IF writes into this cycle.
 * Points to the freed wb slot (which becomes the new id after rotate).
 * This ensures IF and ID operate on different slots each cycle. */
static PipeReg *g_fetch_slot;

/* ── Inline difftest ────────────────────────────────────────────────────────
 * Enabled by --difftest flag in in-order mode.
 * Maintains a reference CPU (diff_cpu) that executes purely functionally.
 * After each stage_WB commit, we check that:
 *   1. The committed PC equals diff_cpu.pc (correct instruction order)
 *   2. The rd write result equals what functional produces
 * This catches pipeline control and forwarding bugs immediately.
 */
extern int g_difftest_en;

static CPU_state diff_cpu;
static bool     diff_init = false;

static void difftest_step(IR_Inst *committed)
{
    if (!g_difftest_en) return;

    /* Initialise reference CPU before the very first commit. */
    if (!diff_init) {
        diff_cpu    = cpu;          /* cpu is in pre-commit state here */
        diff_cpu.pc = committed->pc;
        diff_init   = true;
    }

    /* Check PC order */
    if (committed->pc != diff_cpu.pc) {
        fprintf(stderr,
            "\n[DIFFTEST] PC mismatch at commit #%" PRIu64
            ": pipeline committed pc=0x%lx  functional expected pc=0x%lx\n",
            pipe_stats.insts, committed->pc, diff_cpu.pc);
        abort();
    }

    /* Run functional step independently on diff_cpu */
    IR_Inst diff_ir;
    ir_decode(committed->raw, committed->pc, committed->pc + 4, &diff_ir);
    if (diff_ir.rs1 > 0) diff_ir.src1_val = diff_cpu.gpr[diff_ir.rs1];
    if (diff_ir.rs2 > 0) diff_ir.src2_val = diff_cpu.gpr[diff_ir.rs2];
    ir_execute(&diff_ir, &diff_cpu);
    ir_mem_access(&diff_ir, NULL);
    ir_writeback(&diff_ir, &diff_cpu);
    diff_cpu.pc = diff_ir.dnpc;

    /* Compare rd write result (skip CSR instructions whose side-effects
     * depend on the global cycle counter, not the diff_cpu state) */
    bool is_csr = (committed->raw & 0x7f) == 0x73; /* SYSTEM opcode */
    if (committed->rd > 0 && !is_csr && committed->result != diff_ir.result) {
        fprintf(stderr,
            "\n[DIFFTEST] result mismatch at commit #%" PRIu64
            " pc=0x%lx (%s): "
            "pipeline rd[%d]=0x%lx  functional=0x%lx\n"
            "  pipeline src1=0x%lx src2=0x%lx\n"
            "  diff     src1=0x%lx src2=0x%lx\n",
            pipe_stats.insts,
            committed->pc, committed->name ? committed->name : "?",
            committed->rd, committed->result, diff_ir.result,
            committed->src1_val, committed->src2_val,
            diff_ir.src1_val, diff_ir.src2_val);
        fprintf(stderr,
            "\n[DIFFTEST] result mismatch at pc=0x%lx (%s): "
            "pipeline rd[%d]=0x%lx  functional=0x%lx\n",
            committed->pc, committed->name ? committed->name : "?",
            committed->rd, committed->result, diff_ir.result);
        abort();
    }
    /* For CSR instructions we can't compare results (cycle-counter-dependent),
     * but we must sync diff_cpu's rd to the pipeline value so that subsequent
     * instructions (e.g. "sub s4, s4, a5" after "rdcycle s4") use the same
     * value and don't generate false-positive mismatches. */
    if (committed->rd > 0 && is_csr)
        diff_cpu.gpr[committed->rd] = committed->result;
}

void pipeline_cycle(void)
{
    if (cpu_pipe.mem_stall_rem > 0) {
        cpu_pipe.mem_stall_rem--;
        pipe_stats.cycles++;
        g_sim_cycles++;
        pipeline_trace_cycle();
        return;
    }
    g_pipe_stalled = 0;
    g_pipe_rotated = 0;
    /* Reserve the wb slot for the new fetch this cycle.
     * stage_IF writes into g_fetch_slot; rotate then makes it the new id.
     * Do NOT pre-clear g_fetch_slot->valid — stage_WB needs wb->valid intact. */
    g_fetch_slot = cpu_pipe.wb;
    stage_WB();
    stage_MEM();
    stage_EX();
    stage_ID();
    stage_IF();

    /* Rotate latch pointers instead of copying 160-byte IR_Inst structs.
     *
     * Normal cycle:
     *   new wb  = old mem   (MEM output → WB next cycle)
     *   new mem = old ex    (EX output  → MEM next cycle)
     *   new ex  = old id    (ID output  → EX next cycle)
     *   new id  = old wb    (recycled bubble for IF to fill)
     *
     * Load-use stall cycle (g_pipe_stalled=1):
     *   id is frozen (same instruction must retry next cycle)
     *   ex must receive a bubble (not the stalled id)
     *   new wb  = old mem
     *   new mem = old ex
     *   new ex  = old wb    (recycled bubble, valid=0)
     *   new id  = old id    (stays frozen)
     */
    if (!g_pipe_rotated) {
        if (!g_pipe_stalled) {
            /* Normal rotate:
             *   wb  ← old mem  (MEM output → WB next cycle)
             *   mem ← old ex   (EX output  → MEM next cycle)
             *   ex  ← old id   (ID-processed instruction → EX next cycle)
             *   id  ← g_fetch_slot (newly fetched by IF this cycle) */
            cpu_pipe.wb  = cpu_pipe.mem;
            cpu_pipe.mem = cpu_pipe.ex;
            cpu_pipe.ex  = cpu_pipe.id;
            cpu_pipe.id  = g_fetch_slot;
        } else {
            /* Load-use stall: id is frozen (same instruction retried next cycle).
             * Insert a bubble into EX by recycling the fetch slot (which IF left empty). */
            cpu_pipe.wb  = cpu_pipe.mem;
            cpu_pipe.mem = cpu_pipe.ex;
            cpu_pipe.ex  = g_fetch_slot;   /* bubble (valid=0) goes to EX */
            /* cpu_pipe.id stays pointing at the stalled instruction */
        }
    }

    pipe_stats.cycles++;
    g_sim_cycles++;
    g_sim_instret = pipe_stats.insts;
    pipeline_trace_cycle();
}

/* ── stage_WB ───────────────────────────────────────────────────────────── */

static void stage_WB(void)
{
    if (!cpu_pipe.wb->valid)
        return;

    IR_Inst *ir = &cpu_pipe.wb->ir;

    difftest_step(ir);   /* check BEFORE writeback so cpu is in pre-commit state */

    /* Commit GPR result */
    ir_writeback(ir, &cpu);

    /* Update architectural PC to the retired instruction's successor */
    cpu.pc = ir->dnpc;

    pipe_stats.insts++;
    cpu_pipe.wb->valid = 0;
}

/* ── stage_MEM ──────────────────────────────────────────────────────────── */

static void stage_MEM(void)
{
    /* MEM stage processes the instruction currently in the MEM latch in-place.
     * After the rotate at end of pipeline_cycle, this slot becomes wb. */
    if (!cpu_pipe.mem->valid)
        return;

    int lat = 0;
    ir_mem_access(&cpu_pipe.mem->ir, &lat);   /* fills ir->result for loads */

    /* If this is a load that missed the L1, stall the pipeline for the
     * remaining cache latency.  The result is already in mem->ir.result;
     * after the rotate it will be in the wb slot, committed after the stall. */
    if (lat > 1 && cpu_pipe.mem->ir.type == ITYPE_LOAD) {
        cpu_pipe.mem_stall_rem = lat - 1;
        pipe_stats.stall_cache_miss += (uint64_t)(lat - 1);
        /* The normal end-of-cycle rotate will move this load into wb.
         * The pipeline then freezes for mem_stall_rem cycles while WB waits. */
    }
}

/* ── stage_EX ───────────────────────────────────────────────────────────── */

static void stage_EX(void)
{
    if (!cpu_pipe.ex->valid)
        return;   /* bubble in EX — no-op; rotate will propagate it */

    IR_Inst *ir = &cpu_pipe.ex->ir;

    ir_execute(ir, &cpu);   /* ALU + address compute (no mem access) */
    if (ir->fault) INV(ir->pc);  /* in-order: raise invalid-instruction immediately */

    /* The ex slot (with execution results) will become mem after rotate. */

    if (g_bpred_mode) {
        bpred_update(&bpred, ir);
        if (ir->dnpc != ir->bp_predicted_pc) {
            cpu_pipe.id->valid   = 0;   /* flush wrongly-speculated instruction in ID */
            g_fetch_slot->valid  = 0;   /* also kill any fetch that IF hasn't done yet */
            cpu_pipe.fetch_pc    = ir->dnpc;
        }
    } else {
        if (ir->dnpc != ir->snpc) {
            cpu_pipe.id->valid   = 0;
            g_fetch_slot->valid  = 0;
            cpu_pipe.fetch_pc    = ir->dnpc;
            pipe_stats.stall_control++;
        }
    }
}

static void stage_ID(void)
{
    if (!cpu_pipe.id->valid) {
        /* Bubble in ID: set stall flag so rotate inserts bubble into EX
         * (id slot is empty, so the recycled slot going to ex must be empty too).
         * But actually: if id is empty, rotate normally sends id (empty) → ex.
         * That's already a bubble. No special action needed. */
        return;
    }

    IR_Inst *ir = &cpu_pipe.id->ir;

    /*
     * Re-read rs1/rs2 from the register file.
     *
     * stage_WB runs first this cycle and may have committed an instruction
     * whose result is now in cpu.gpr but is no longer in any pipeline latch.
     * By re-reading here we capture "WB→ID forwarding via register file"
     * (equivalent to write-first register file in a real design).
     *
     * rs > 0 guards: x0 is always 0 (skip read), and -1 means "no reg source"
     * (e.g., rs2 = -1 for I-type after the fix in ir_fill_operands).
     */
    if (ir->rs1 > 0) ir->src1_val = cpu.gpr[ir->rs1];
    if (ir->rs2 > 0) ir->src2_val = cpu.gpr[ir->rs2];

    /*
     * After WB, MEM, EX have run this cycle:
     *   cpu_pipe.ex  = instruction just executed by stage_EX (ALU result ready)
     *   cpu_pipe.mem = instruction just memory-accessed by stage_MEM (load result ready)
     *
     * With pointer rotation, the slots have NOT yet rotated — ex and mem still
     * hold the same slot indices as at the start of this cycle.
     *
     * Load-use hazard: if the instruction just executed in EX is a LOAD and
     * targets a register that ID needs, the result won't be available until
     * after stage_MEM runs NEXT cycle → stall.
     */
    if (cpu_pipe.ex->valid
        && cpu_pipe.ex->ir.type == ITYPE_LOAD
        && cpu_pipe.ex->ir.rd > 0
        && (cpu_pipe.ex->ir.rd == ir->rs1 || cpu_pipe.ex->ir.rd == ir->rs2)) {
        pipe_stats.stall_load_use++;
        g_pipe_stalled = 1;
        return;   /* id->valid == 1: stage_IF stalls too (id still occupied) */
    }

    /*
     * Forwarding (overrides register-file read above):
     *   cpu_pipe.mem → MEM-stage result (2-cycle-ahead: includes load results)
     *   cpu_pipe.ex  → EX-stage result  (1-cycle-ahead: ALU/JAL/etc, not LOAD)
     * Apply mem first (older), then ex (newer, takes priority).
     */
    if (cpu_pipe.mem->valid && cpu_pipe.mem->ir.rd > 0) {
        if (cpu_pipe.mem->ir.rd == ir->rs1) ir->src1_val = cpu_pipe.mem->ir.result;
        if (cpu_pipe.mem->ir.rd == ir->rs2) ir->src2_val = cpu_pipe.mem->ir.result;
    }
    if (cpu_pipe.ex->valid && cpu_pipe.ex->ir.rd > 0
        && cpu_pipe.ex->ir.type != ITYPE_LOAD) {
        if (cpu_pipe.ex->ir.rd == ir->rs1) ir->src1_val = cpu_pipe.ex->ir.result;
        if (cpu_pipe.ex->ir.rd == ir->rs2) ir->src2_val = cpu_pipe.ex->ir.result;
    }

    /* id → ex: the rotate at end of pipeline_cycle moves id slot to ex position.
     * The id slot's content (with forwarded src vals) is already correct;
     * stage_IF will fill the recycled slot next cycle. */
    /* (nothing to copy — rotate handles the pointer swap) */
}

/* ── stage_IF ───────────────────────────────────────────────────────────── */

static void stage_IF(void)
{
    /* Can't fetch if ID stage is stalled (instruction in id hasn't advanced yet). */
    if (g_pipe_stalled)
        return;

    /* Fetch into the pre-assigned fetch slot (old wb, will become new id after rotate). */
    vaddr_t  pc  = cpu_pipe.fetch_pc;
    uint32_t raw = (uint32_t)vaddr_ifetch(pc, 4);
    ir_decode(raw, pc, pc + 4, &g_fetch_slot->ir);
    g_fetch_slot->valid = 1;
    cpu_pipe.fetch_pc   = pc + 4;

    g_fetch_slot->ir.bp_predict_taken = 0;
    g_fetch_slot->ir.bp_predicted_pc  = pc + 4;
    if (g_bpred_mode) {
        /* Only call bpred for control-flow instructions (BRANCH/JAL/JALR).
         * opcode bits[6:2]: 0x18=BRANCH, 0x1b=JAL, 0x19=JALR.
         * Non-control instructions cannot redirect fetch_pc, so skip the
         * BTB/predictor lookup to avoid ~7% bpred_predict overhead. */
        uint8_t op5 = (raw >> 2) & 0x1f;
        if (op5 == 0x18 || op5 == 0x1b || op5 == 0x19) {
            BPredResult r = bpred_predict(&bpred, pc, raw);
            g_fetch_slot->ir.bp_predict_taken = r.taken;
            g_fetch_slot->ir.bp_predicted_pc  = (r.taken && r.btb_hit) ? r.target : pc + 4;
            if (r.taken && r.btb_hit)
                cpu_pipe.fetch_pc = r.target;
        }
    }
}

/* ── pipeline_trace_cycle ────────────────────────────────────────────────── */
/*
 * Print a one-line per-cycle pipeline snapshot to the log file.
 * Enabled by --trace (g_trace_en=1); requires -l <file> to set log_fp.
 *
 * Format:
 *   [C= N] ID:pc/name   EX:pc/name   MEM:pc/name   WB:pc/name
 *
 * Labels reflect the state AT THE END of the cycle (what each latch holds
 * going into the next cycle):
 *   ID  = just fetched by IF, awaiting decode
 *   EX  = just decoded by ID, awaiting execution
 *   MEM = just executed by EX, awaiting memory access
 *   WB  = just memory-accessed, awaiting writeback
 */
static void pipeline_trace_cycle(void)
{
    if (!g_trace_en || !log_fp) return;

#define TP(label, reg) do {                                          \
    fprintf(log_fp, " %s:", label);                                  \
    if ((reg)->valid)                                                  \
        fprintf(log_fp, "%08x/%-4s  ",                              \
                (unsigned)(reg)->ir.pc,                               \
                (reg)->ir.name ? (reg)->ir.name : "??");               \
    else                                                              \
        fprintf(log_fp, "--            ");                           \
} while (0)

    fprintf(log_fp, "[C=%6" PRIu64 "]", pipe_stats.cycles);
    TP("ID",  cpu_pipe.id);
    TP("EX",  cpu_pipe.ex);
    TP("MEM", cpu_pipe.mem);
    TP("WB",  cpu_pipe.wb);
    fprintf(log_fp, "\n");
    fflush(log_fp);

#undef TP
}

/* ── pipeline_report ────────────────────────────────────────────────────── */

void pipeline_report(void)
{
    double ipc = (pipe_stats.cycles > 0)
                 ? (double)pipe_stats.insts / (double)pipe_stats.cycles
                 : 0.0;

    printf("\n=== Pipeline Statistics ===\n");
    printf("Cycles          : %" PRIu64 "\n", pipe_stats.cycles);
    printf("Instructions    : %" PRIu64 "\n", pipe_stats.insts);
    printf("IPC             : %.3f\n", ipc);
    printf("Load-use stalls : %" PRIu64 " cycles\n", pipe_stats.stall_load_use);
    printf("Control flushes : %" PRIu64 " (x1 cycle = %" PRIu64 " cycles lost)\n",
           pipe_stats.stall_control, pipe_stats.stall_control);
    printf("Cache miss stalls: %" PRIu64 " cycles\n", pipe_stats.stall_cache_miss);
    printf("===========================\n\n");

    if (g_bpred_mode) bpred_report(&bpred);
}

/* Fix up latch pointers after a Pipeline struct is copied.
 * The pointers id/ex/mem/wb point into the *source* struct's slots array
 * after a plain struct copy; this patches them to point into dst->slots. */
static void pipeline_fix_ptrs(Pipeline *dst, const Pipeline *src)
{
    /* Each pointer is one of src->slots[0..3].
     * Compute the slot index from the source pointer and re-aim at dst. */
    for (int i = 0; i < 4; i++) {
        if (src->id  == &src->slots[i]) dst->id  = &dst->slots[i];
        if (src->ex  == &src->slots[i]) dst->ex  = &dst->slots[i];
        if (src->mem == &src->slots[i]) dst->mem = &dst->slots[i];
        if (src->wb  == &src->slots[i]) dst->wb  = &dst->slots[i];
    }
}

/* ── Multi-core entry points ─────────────────────────────────────────────── */
/*
 * pipeline_init_core — initialise this core's Pipeline/PipeStats,
 * temporarily installing them as the globals so that pipeline_init()
 * can reference cpu.pc.
 */
void pipeline_init_core(Core *c)
{
    /* Swap globals to this core's state */
    Pipeline  save_pipe  = cpu_pipe;
    PipeStats save_stats = pipe_stats;

    cpu_pipe       = c->cpu_pipe;
    pipeline_fix_ptrs(&cpu_pipe, &c->cpu_pipe);
    pipe_stats = c->pipe_stats;

    /* Init latches and fetch_pc */
    cpu_pipe.id->valid   = 0;
    cpu_pipe.ex->valid   = 0;
    cpu_pipe.mem->valid  = 0;
    cpu_pipe.wb->valid   = 0;
    cpu_pipe.fetch_pc   = cpu.pc;
    cpu_pipe.mem_stall_rem = 0;

    pipe_stats.cycles          = 0;
    pipe_stats.insts           = 0;
    pipe_stats.stall_load_use  = 0;
    pipe_stats.stall_control   = 0;
    pipe_stats.stall_cache_miss = 0;

    if (c->bpred_enabled) bpred_init(&c->bpred);

    c->cpu_pipe       = cpu_pipe;
    c->pipe_stats = pipe_stats;

    cpu_pipe       = save_pipe;
    pipeline_fix_ptrs(&cpu_pipe, &save_pipe);
    pipe_stats = save_stats;
}

/*
 * pipeline_cycle_core — advance this core's pipeline by one cycle.
 * Saves/restores the single-core globals so the stage functions work
 * without modification.
 */
void pipeline_cycle_core(Core *c)
{
    /* Install this core's state into the legacy globals */
    Pipeline       save_pipe  = cpu_pipe;
    PipeStats      save_stats = pipe_stats;
    BranchPredictor save_bp   = bpred;

    cpu_pipe       = c->cpu_pipe;
    pipeline_fix_ptrs(&cpu_pipe, &c->cpu_pipe);
    pipe_stats = c->pipe_stats;
    bpred      = c->bpred;

    /* Update caches used by vmem (core_cycle already set L1I/L1D) */

    g_pipe_stalled = 0;
    g_pipe_rotated = 0;
    /* Run the 5 stages (with cache-miss stall support) */
    if (cpu_pipe.mem_stall_rem > 0) {
        cpu_pipe.mem_stall_rem--;
    } else {
        g_fetch_slot = cpu_pipe.wb;
        stage_WB();
        stage_MEM();
        stage_EX();
        stage_ID();
        stage_IF();
        if (!g_pipe_rotated) {
            if (!g_pipe_stalled) {
                cpu_pipe.wb  = cpu_pipe.mem;
                cpu_pipe.mem = cpu_pipe.ex;
                cpu_pipe.ex  = cpu_pipe.id;
                cpu_pipe.id  = g_fetch_slot;
            } else {
                cpu_pipe.wb  = cpu_pipe.mem;
                cpu_pipe.mem = cpu_pipe.ex;
                cpu_pipe.ex  = g_fetch_slot;
            }
        }
    }
    pipe_stats.cycles++;
    g_sim_cycles++;
    c->sim_cycles = pipe_stats.cycles;
    g_sim_instret = pipe_stats.insts;
    c->sim_instret = pipe_stats.insts;

    /* Save back to core */
    c->cpu_pipe       = cpu_pipe;
    c->pipe_stats = pipe_stats;
    c->bpred      = bpred;

    /* Restore single-core globals */
    cpu_pipe       = save_pipe;
    pipeline_fix_ptrs(&cpu_pipe, &save_pipe);
    pipe_stats = save_stats;
    bpred      = save_bp;
}
