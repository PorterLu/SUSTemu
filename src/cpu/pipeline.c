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

/* ── Global pipeline state ──────────────────────────────────────────────── */
Pipeline  pipe;
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
    pipe.id.valid   = 0;
    pipe.ex.valid   = 0;
    pipe.mem.valid  = 0;
    pipe.wb.valid   = 0;
    pipe.fetch_pc   = cpu.pc;
    pipe.mem_stall_rem = 0;

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
 * When a cache miss is detected in stage_MEM, pipe.mem_stall_rem is set to
 * the remaining wait cycles.  For those cycles the pipeline is fully frozen
 * (no stage runs) — the completed load sits in the WB latch until the stall
 * expires, at which point stage_WB retires it normally.
 */
void pipeline_cycle(void)
{
    if (pipe.mem_stall_rem > 0) {
        pipe.mem_stall_rem--;
        pipe_stats.cycles++;
        g_sim_cycles++;
        pipeline_trace_cycle();
        return;
    }
    stage_WB();
    stage_MEM();
    stage_EX();
    stage_ID();
    stage_IF();
    pipe_stats.cycles++;
    g_sim_cycles++;
    g_sim_instret = pipe_stats.insts;
    pipeline_trace_cycle();
}

/* ── stage_WB ───────────────────────────────────────────────────────────── */

static void stage_WB(void)
{
    if (!pipe.wb.valid)
        return;

    IR_Inst *ir = &pipe.wb.ir;

    /* Commit GPR result */
    ir_writeback(ir, &cpu);

    /* Update architectural PC to the retired instruction's successor */
    cpu.pc = ir->dnpc;

    pipe_stats.insts++;
    pipe.wb.valid = 0;
}

/* ── stage_MEM ──────────────────────────────────────────────────────────── */

static void stage_MEM(void)
{
    /* Transfer EX→MEM latch into WB latch before processing MEM */
    pipe.wb = pipe.mem;

    if (pipe.mem.valid) {
        int lat = 0;
        ir_mem_access(&pipe.mem.ir, &lat);   /* fills ir->result for loads */
        /* Copy the result (may be a loaded value) into the WB latch */
        pipe.wb.ir.result = pipe.mem.ir.result;

        /* If this is a load that missed the L1, stall the pipeline for the
         * remaining cache latency.  The WB latch is already populated with
         * the correct result; it will be committed after the stall expires. */
        if (lat > 1 && pipe.mem.ir.type == ITYPE_LOAD) {
            pipe.mem_stall_rem = lat - 1;
            pipe_stats.stall_cache_miss += (uint64_t)(lat - 1);
        }
    }

    pipe.mem.valid = 0;
}

/* ── stage_EX ───────────────────────────────────────────────────────────── */

static void stage_EX(void)
{
    /* Pass ID→EX latch to EX→MEM */
    pipe.mem = pipe.ex;

    if (pipe.ex.valid) {
        IR_Inst *ir = &pipe.ex.ir;

        ir_execute(ir, &cpu);   /* ALU + address compute (no mem access) */
        if (ir->fault) INV(ir->pc);  /* in-order: raise invalid-instruction immediately */

        /* Propagate result into MEM latch */
        pipe.mem.ir = *ir;

        if (g_bpred_mode) {
            /* ── Branch predictor update + misprediction recovery ────────
             * bpred_update() records the actual outcome and updates all
             * predictor structures.  A misprediction occurs when the
             * resolved PC differs from whatever the IF stage predicted
             * (bp_predicted_pc was set to snpc for non-taken predictions
             * or to the BTB target for taken predictions).
             */
            bpred_update(&bpred, ir);
            if (ir->dnpc != ir->bp_predicted_pc) {
                pipe.id.valid = 0;          /* flush wrongly-speculated IF/ID */
                pipe.fetch_pc = ir->dnpc;   /* redirect to correct PC         */
            }
        } else {
            /* ── Control hazard (no predictor) ──────────────────────────
             * Any instruction whose resolved PC differs from pc+4 requires
             * flushing the ID latch (one wrongly-fetched instruction).
             */
            if (ir->dnpc != ir->snpc) {
                pipe.id.valid  = 0;
                pipe.fetch_pc  = ir->dnpc;
                pipe_stats.stall_control++;
            }
        }
    }

    pipe.ex.valid = 0;
}

static void stage_ID(void)
{
    if (!pipe.id.valid) {
        pipe.ex.valid = 0;
        return;
    }

    IR_Inst *ir = &pipe.id.ir;

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
     *   pipe.mem = instruction that was in EX (just executed by stage_EX)
     *   pipe.wb  = instruction that was in MEM (just memory-accessed by stage_MEM)
     *
     * Load-use hazard: if the EX instruction (now pipe.mem) is a LOAD writing
     * a register ID needs, we cannot forward (result not yet available) → stall.
     */
    if (pipe.mem.valid
        && pipe.mem.ir.type == ITYPE_LOAD
        && pipe.mem.ir.rd > 0
        && (pipe.mem.ir.rd == ir->rs1 || pipe.mem.ir.rd == ir->rs2)) {
        pipe_stats.stall_load_use++;
        return;   /* id.valid == 1: stage_IF stalls too */
    }

    /*
     * Forwarding (overrides register-file read above):
     *   pipe.wb  → 2-cycle-ahead result (was in MEM: includes load results)
     *   pipe.mem → 1-cycle-ahead result (was in EX: ALU/JAL/etc, not LOAD)
     * Apply WB first (older), then MEM (newer, takes priority).
     */
    if (pipe.wb.valid && pipe.wb.ir.rd > 0) {
        if (pipe.wb.ir.rd == ir->rs1) ir->src1_val = pipe.wb.ir.result;
        if (pipe.wb.ir.rd == ir->rs2) ir->src2_val = pipe.wb.ir.result;
    }
    if (pipe.mem.valid && pipe.mem.ir.rd > 0
        && pipe.mem.ir.type != ITYPE_LOAD) {
        if (pipe.mem.ir.rd == ir->rs1) ir->src1_val = pipe.mem.ir.result;
        if (pipe.mem.ir.rd == ir->rs2) ir->src2_val = pipe.mem.ir.result;
    }

    /* Move ID → EX */
    pipe.ex      = pipe.id;
    pipe.id.valid = 0;
}

/* ── stage_IF ───────────────────────────────────────────────────────────── */

static void stage_IF(void)
{
    /* If the ID latch is still occupied (load-use stall), we cannot
     * place a new instruction there — stall the fetch stage. */
    if (pipe.id.valid)
        return;

    /* Normal fetch: read instruction from memory and decode into ID latch */
    vaddr_t  pc  = pipe.fetch_pc;
    uint32_t raw = (uint32_t)vaddr_ifetch(pc, 4);
    ir_decode(raw, pc, pc + 4, &pipe.id.ir);
    pipe.id.valid  = 1;
    pipe.fetch_pc  = pc + 4;   /* default sequential; EX may override on mispredict */

    if (g_bpred_mode) {
        /* Query predictor and annotate the instruction for EX to verify */
        BPredResult r = bpred_predict(&bpred, pc);
        pipe.id.ir.bp_predict_taken = r.taken;
        pipe.id.ir.bp_predicted_pc  = (r.taken && r.btb_hit) ? r.target : pc + 4;
        /* Speculative redirect: if we predict taken and have a BTB target,
         * fetch from the predicted target next cycle. */
        if (r.taken && r.btb_hit)
            pipe.fetch_pc = r.target;
    } else {
        /* No predictor: always predict not-taken (pc+4) */
        pipe.id.ir.bp_predict_taken = 0;
        pipe.id.ir.bp_predicted_pc  = pc + 4;
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
    if ((reg).valid)                                                  \
        fprintf(log_fp, "%08x/%-4s  ",                              \
                (unsigned)(reg).ir.pc,                               \
                (reg).ir.name ? (reg).ir.name : "??");               \
    else                                                              \
        fprintf(log_fp, "--            ");                           \
} while (0)

    fprintf(log_fp, "[C=%6" PRIu64 "]", pipe_stats.cycles);
    TP("ID",  pipe.id);
    TP("EX",  pipe.ex);
    TP("MEM", pipe.mem);
    TP("WB",  pipe.wb);
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

/* ── Multi-core entry points ─────────────────────────────────────────────── */
/*
 * pipeline_init_core — initialise this core's Pipeline/PipeStats,
 * temporarily installing them as the globals so that pipeline_init()
 * can reference cpu.pc.
 */
void pipeline_init_core(Core *c)
{
    /* Swap globals to this core's state */
    Pipeline  save_pipe  = pipe;
    PipeStats save_stats = pipe_stats;

    pipe       = c->pipe;
    pipe_stats = c->pipe_stats;

    /* pipeline_init() reads cpu.pc (already loaded by core_cycle) */
    /* Init latches and fetch_pc */
    pipe.id.valid   = 0;
    pipe.ex.valid   = 0;
    pipe.mem.valid  = 0;
    pipe.wb.valid   = 0;
    pipe.fetch_pc   = cpu.pc;
    pipe.mem_stall_rem = 0;

    pipe_stats.cycles          = 0;
    pipe_stats.insts           = 0;
    pipe_stats.stall_load_use  = 0;
    pipe_stats.stall_control   = 0;
    pipe_stats.stall_cache_miss = 0;

    if (c->bpred_enabled) bpred_init(&c->bpred);

    c->pipe       = pipe;
    c->pipe_stats = pipe_stats;

    pipe       = save_pipe;
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
    Pipeline       save_pipe  = pipe;
    PipeStats      save_stats = pipe_stats;
    BranchPredictor save_bp   = bpred;

    pipe       = c->pipe;
    pipe_stats = c->pipe_stats;
    bpred      = c->bpred;

    /* Update caches used by vmem (core_cycle already set L1I/L1D) */

    /* Run the 5 stages (with cache-miss stall support) */
    if (pipe.mem_stall_rem > 0) {
        pipe.mem_stall_rem--;
    } else {
        stage_WB();
        stage_MEM();
        stage_EX();
        stage_ID();
        stage_IF();
    }
    pipe_stats.cycles++;
    g_sim_cycles++;
    c->sim_cycles = pipe_stats.cycles;
    g_sim_instret = pipe_stats.insts;
    c->sim_instret = pipe_stats.insts;

    /* Save back to core */
    c->pipe       = pipe;
    c->pipe_stats = pipe_stats;
    c->bpred      = bpred;

    /* Restore single-core globals */
    pipe       = save_pipe;
    pipe_stats = save_stats;
    bpred      = save_bp;
}
