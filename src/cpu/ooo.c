/*
 * src/cpu/ooo.c — Tomasulo Out-of-Order Engine (Phase 4)
 *
 * 7-stage pipeline executed in reverse order per cycle (same reasoning as
 * pipeline.c — later stages run first so upstream stages see the correct
 * latch state written by the previous cycle):
 *
 *   ooo_stage_commit  ← ROB head; in-order commit
 *   ooo_stage_mem     ← EX→MEM latch; memory access
 *   ooo_stage_ex      ← IS→EX latch; execute + CDB broadcast
 *   ooo_stage_is      ← scan RS; select oldest ready entry to issue
 *   ooo_stage_rn      ← ID→RN latch; register rename, ROB/RS allocation
 *   ooo_stage_id      ← IF→ID latch; pass decoded instruction to RN
 *   ooo_stage_if      ← fetch from fetch_pc; annotate with branch prediction
 *
 * Key invariants:
 *   - x0 always maps to p0; p0 is always ready with value 0; never renamed.
 *   - Serializing instructions (CSR/ecall/mret/ebreak) stall RN until
 *     rob_count == 0, then enter the ROB alone.
 *   - STOREs are deferred: data is saved in ROB.store_data at MEM stage
 *     and written to memory only at ROB commit (store ordering preserved).
 *   - On misprediction: RAT is restored by replaying the ROB entries from
 *     rob_tail back to branch_rob_idx+1.
 */

#include <ooo.h>
#include <core.h>
#include <bpred.h>
#include <csr.h>
#include <reg.h>
#include <state.h>
#include <vmem.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* ── Globals ─────────────────────────────────────────────────────────────── */
OOOState ooo;
OOOStats ooo_stats;
int      g_ooo_mode = 0;

/* ── Forward declarations ────────────────────────────────────────────────── */
static void ooo_stage_commit(void);
static void ooo_stage_mem(void);
static void ooo_stage_ex(void);
static void ooo_stage_is(void);
static void ooo_stage_rn(void);
static void ooo_stage_id(void);
static void ooo_stage_if(void);
static void cdb_broadcast(int phys_tag);
static void ooo_flush_after(int branch_rob_idx);

/* ── Free list helpers ────────────────────────────────────────────────────── */

static int freelist_pop(void)
{
    int reg = ooo.freelist.regs[ooo.freelist.head];
    ooo.freelist.head = (ooo.freelist.head + 1) % FREE_LIST_SIZE;
    ooo.freelist.count--;
    return reg;
}

static void freelist_push(int reg)
{
    ooo.freelist.regs[ooo.freelist.tail] = reg;
    ooo.freelist.tail = (ooo.freelist.tail + 1) % FREE_LIST_SIZE;
    ooo.freelist.count++;
}

/* ── ROB ordering helper ─────────────────────────────────────────────────── */
/*
 * Returns 1 if ROB slot 'a' is strictly newer than slot 'b'.
 * "Newer" means it was allocated later (further from rob_head in the
 * circular buffer).
 */
static int rob_newer_than(int a, int b)
{
    int da = (a - ooo.rob_head + ROB_SIZE) % ROB_SIZE;
    int db = (b - ooo.rob_head + ROB_SIZE) % ROB_SIZE;
    return da > db;
}

/* ── CDB broadcast ───────────────────────────────────────────────────────── */
/*
 * Wake up any RS entries that are waiting for physical register phys_tag.
 * Called after a PRF entry becomes ready (ALU result or load result).
 */
static void cdb_broadcast(int phys_tag)
{
    int i;
    for (i = 0; i < RS_SIZE; i++) {
        if (!ooo.rs[i].valid) continue;
        if (!ooo.rs[i].src1_ready && ooo.rs[i].src1_ptag == phys_tag) {
            ooo.rs[i].src1_val   = ooo.prf[phys_tag].value;
            ooo.rs[i].src1_ready = 1;
        }
        if (!ooo.rs[i].src2_ready && ooo.rs[i].src2_ptag == phys_tag) {
            ooo.rs[i].src2_val   = ooo.prf[phys_tag].value;
            ooo.rs[i].src2_ready = 1;
        }
    }
}

/* ── ooo_flush_after ─────────────────────────────────────────────────────── */
/*
 * On misprediction at branch_rob_idx: discard all ROB entries newer than
 * branch_rob_idx (i.e., rob_idx from branch_rob_idx+1 to rob_tail-1),
 * restore the RAT to the state at the branch, return physical registers
 * to the free list, and flush all downstream pipeline stages.
 */
static void ooo_flush_after(int branch_rob_idx)
{
    int new_tail = (branch_rob_idx + 1) % ROB_SIZE;

    /*
     * Shrink rob_tail backwards one slot at a time, undoing each entry's
     * rename.  We stop when rob_tail reaches new_tail (i.e., there is
     * nothing newer than the branch left in the ROB).
     */
    while (ooo.rob_tail != new_tail) {
        ooo.rob_tail = (ooo.rob_tail - 1 + ROB_SIZE) % ROB_SIZE;
        ROBEntry *entry = &ooo.rob[ooo.rob_tail];
        if (entry->valid) {
            if (entry->arch_rd != -1 && entry->arch_rd != 0) {
                /* Restore speculative RAT entry */
                ooo.rat[entry->arch_rd] = entry->old_phys;
                /* Return the prematurely-allocated physical register */
                freelist_push(entry->phys_rd);
            }
            entry->valid = 0;
            ooo.rob_count--;
        }
    }

    /* Flush RS entries that are newer than the mispredicted branch */
    int i;
    for (i = 0; i < RS_SIZE; i++) {
        if (ooo.rs[i].valid &&
            rob_newer_than(ooo.rs[i].rob_idx, branch_rob_idx))
            ooo.rs[i].valid = 0;
    }

    /* Flush IS→EX and EX→MEM latches if they hold a newer instruction */
    if (ooo.latch_is_ex.valid &&
        rob_newer_than(ooo.latch_is_ex.rob_idx, branch_rob_idx))
        ooo.latch_is_ex.valid = 0;
    if (ooo.latch_ex_mem.valid &&
        rob_newer_than(ooo.latch_ex_mem.rob_idx, branch_rob_idx))
        ooo.latch_ex_mem.valid = 0;

    /* Flush front-end latches unconditionally */
    ooo.latch_if_id.valid = 0;
    ooo.latch_id_rn.valid = 0;
}

/* ── ooo_init ────────────────────────────────────────────────────────────── */

void ooo_init(void)
{
    int i;

    memset(&ooo, 0, sizeof(ooo));
    memset(&ooo_stats, 0, sizeof(ooo_stats));

    /* Initialise physical register file.
     * p0..p31 hold the committed architectural state; p0 = x0 = 0 always. */
    for (i = 0; i < NUM_PHYS_REGS; i++) {
        ooo.prf[i].value = 0;
        ooo.prf[i].ready = 1;
    }
    for (i = 0; i < 32; i++) {
        ooo.prf[i].value = cpu.gpr[i];
        ooo.rat[i]  = i;
        ooo.rrat[i] = i;
    }
    ooo.prf[0].value = 0;   /* x0 is always zero */

    /* Free list: p32..p63 */
    ooo.freelist.head  = 0;
    ooo.freelist.tail  = 0;
    ooo.freelist.count = 0;
    for (i = 32; i < NUM_PHYS_REGS; i++)
        freelist_push(i);

    /* ROB: empty */
    ooo.rob_head  = 0;
    ooo.rob_tail  = 0;
    ooo.rob_count = 0;

    ooo.fetch_pc = cpu.pc;

    if (g_bpred_mode) bpred_init(&bpred);
}

/* ── ooo_stage_commit ────────────────────────────────────────────────────── */

static void ooo_stage_commit(void)
{
    ROBEntry *rob;

    if (ooo.rob_count == 0) return;

    rob = &ooo.rob[ooo.rob_head];
    if (!rob->valid || !rob->ready) return;

    /* STORE: deferred memory write — happens here, not in MEM stage */
    if (rob->ir.type == ITYPE_STORE)
        vaddr_write(rob->ir.mem_addr, rob->ir.mem_width, rob->store_data);

    /* Update architectural register file and committed RAT */
    if (rob->arch_rd != -1 && rob->arch_rd != 0) {
        cpu.gpr[rob->arch_rd]   = ooo.prf[rob->phys_rd].value;
        ooo.rrat[rob->arch_rd]  = rob->phys_rd;
        freelist_push(rob->old_phys);   /* Return the superseded physical reg */
    }
    cpu.gpr[0] = 0;   /* x0 is always zero */

    cpu.pc = rob->ir.dnpc;

    rob->valid = 0;
    ooo.rob_head  = (ooo.rob_head + 1) % ROB_SIZE;
    ooo.rob_count--;
    ooo_stats.insts++;
}

/* ── ooo_stage_mem ───────────────────────────────────────────────────────── */

static void ooo_stage_mem(void)
{
    IR_Inst *ir;
    int phys_rd, rob_idx;

    if (!ooo.latch_ex_mem.valid) return;

    ir      = &ooo.latch_ex_mem.ir;
    phys_rd = ooo.latch_ex_mem.phys_rd;
    rob_idx = ooo.latch_ex_mem.rob_idx;

    /* The ROB entry may have been flushed if a prior misprediction was
     * detected in the same cycle before stage_mem ran.  Discard silently. */
    if (!ooo.rob[rob_idx].valid) {
        ooo.latch_ex_mem.valid = 0;
        return;
    }

    if (ir->type == ITYPE_LOAD) {
        ir_mem_access(ir);                      /* Fills ir->result */
        ooo.prf[phys_rd].value = ir->result;
        ooo.prf[phys_rd].ready = 1;
        cdb_broadcast(phys_rd);
        ooo.rob[rob_idx].ready = 1;
        /* Propagate loaded value into ROB snapshot for completeness */
        ooo.rob[rob_idx].ir.result = ir->result;
    } else if (ir->type == ITYPE_STORE) {
        /* Save store data in ROB; actual memory write at commit */
        ooo.rob[rob_idx].store_data = ir->src2_val;
        ooo.rob[rob_idx].ready = 1;
    }

    ooo.latch_ex_mem.valid = 0;
}

/* ── ooo_stage_ex ────────────────────────────────────────────────────────── */

static void ooo_stage_ex(void)
{
    IR_Inst  ir;
    int      phys_rd, rob_idx;
    vaddr_t  predicted;

    if (!ooo.latch_is_ex.valid) return;

    ir      = ooo.latch_is_ex.ir;
    phys_rd = ooo.latch_is_ex.phys_rd;
    rob_idx = ooo.latch_is_ex.rob_idx;

    ooo.latch_is_ex.valid = 0;

    /* Skip if the ROB entry was flushed by a preceding misprediction */
    if (!ooo.rob[rob_idx].valid) return;

    /* Execute: compute ALU result, branch outcome, memory address, etc. */
    ir_execute(&ir, &cpu);

    /* Forward to EX→MEM latch */
    ooo.latch_ex_mem.ir      = ir;
    ooo.latch_ex_mem.phys_rd = phys_rd;
    ooo.latch_ex_mem.rob_idx = rob_idx;
    ooo.latch_ex_mem.valid   = 1;

    /* Update ROB snapshot with execution results */
    ooo.rob[rob_idx].ir = ir;

    if (ir.type != ITYPE_LOAD && ir.type != ITYPE_STORE) {
        /* ALU/branch/jump/system: result is ready now; write PRF and CDB */
        if (phys_rd >= 0) {
            ooo.prf[phys_rd].value = ir.result;
            ooo.prf[phys_rd].ready = 1;
            if (phys_rd > 0) cdb_broadcast(phys_rd);
        }
        ooo.rob[rob_idx].ready = 1;
    }
    /* LOAD/STORE: rob becomes ready in stage_mem */

    /* ── Control hazard / branch misprediction check ───────────────────── */
    if (ir.type == ITYPE_BRANCH ||
        ir.type == ITYPE_JAL    ||
        ir.type == ITYPE_JALR) {

        if (g_bpred_mode) bpred_update(&bpred, &ir);

        predicted = ir.bp_predicted_pc ? ir.bp_predicted_pc : ir.snpc;
        if (ir.dnpc != predicted) {
            ooo_flush_after(rob_idx);
            ooo_stats.mispred_flushes++;
            ooo.fetch_pc = ir.dnpc;
        }
    }
}

/* ── ooo_stage_is ────────────────────────────────────────────────────────── */
/*
 * Issue: scan the reservation station for the oldest entry whose both
 * source operands are ready.  "Oldest" is defined as smallest distance
 * from rob_head in the circular ROB (in-order age).
 */
static void ooo_stage_is(void)
{
    int i, best, da, db;

    if (ooo.latch_is_ex.valid) return;  /* EX is occupied; cannot issue */

    best = -1;
    for (i = 0; i < RS_SIZE; i++) {
        if (!ooo.rs[i].valid) continue;
        if (!ooo.rs[i].src1_ready || !ooo.rs[i].src2_ready) continue;
        if (best == -1) {
            best = i;
        } else {
            da = (ooo.rs[i].rob_idx  - ooo.rob_head + ROB_SIZE) % ROB_SIZE;
            db = (ooo.rs[best].rob_idx - ooo.rob_head + ROB_SIZE) % ROB_SIZE;
            if (da < db) best = i;
        }
    }

    if (best == -1) return;

    ooo.latch_is_ex.ir          = ooo.rs[best].ir;
    /* Override src vals with the values captured at issue time */
    ooo.latch_is_ex.ir.src1_val = ooo.rs[best].src1_val;
    ooo.latch_is_ex.ir.src2_val = ooo.rs[best].src2_val;
    ooo.latch_is_ex.phys_rd     = ooo.rs[best].phys_rd;
    ooo.latch_is_ex.rob_idx     = ooo.rs[best].rob_idx;
    ooo.latch_is_ex.valid       = 1;
    ooo.rs[best].valid          = 0;
}

/* ── ooo_stage_rn ────────────────────────────────────────────────────────── */
/*
 * Rename: translate architectural register indices to physical register
 * indices, allocate a new ROB entry and a reservation station entry, and
 * look up source operand values / readiness from the PRF.
 */
static void ooo_stage_rn(void)
{
    IR_Inst  *ir;
    int       rs_count, rob_idx, rs_slot;
    int       phys_rd, old_phys;
    int       i;
    ROBEntry *rob;
    RSEntry  *rs;

    if (!ooo.latch_id_rn.valid) return;

    ir = &ooo.latch_id_rn.ir;

    /* ── Serialising instructions: drain ROB first ──────────────────────── */
    if (ir->serializing && ooo.rob_count > 0) {
        ooo_stats.serializing_stalls++;
        return;
    }

    /* ── Structural hazard checks ───────────────────────────────────────── */
    rs_count = 0;
    for (i = 0; i < RS_SIZE; i++) if (ooo.rs[i].valid) rs_count++;

    if (ooo.rob_count >= ROB_SIZE) { ooo_stats.rob_full_stalls++; return; }
    if (rs_count >= RS_SIZE)       { ooo_stats.rs_full_stalls++;  return; }
    if (ir->rd != -1 && ir->rd != 0 && ooo.freelist.count == 0) {
        /* Need a physical register but none available */
        ooo_stats.rob_full_stalls++;
        return;
    }

    /* ── Allocate ROB slot ──────────────────────────────────────────────── */
    rob_idx        = ooo.rob_tail;
    ooo.rob_tail   = (ooo.rob_tail + 1) % ROB_SIZE;
    ooo.rob_count++;

    rob           = &ooo.rob[rob_idx];
    rob->ir       = *ir;
    rob->arch_rd  = ir->rd;
    rob->ready    = 0;
    rob->valid    = 1;
    rob->store_data = 0;

    /*
     * ── Read source register mappings BEFORE renaming the destination ─────
     *
     * For instructions where rs1 == rd (e.g. addi x2, x2, 816) we must
     * capture the OLD mapping of rd (which is the source value) before we
     * overwrite rat[rd] with the new physical register.
     */
    /* Source 1 */
    int p1_tag = -1; word_t p1_val = 0; int p1_ready = 1;
    if (ir->rs1 > 0) {
        p1_tag   = ooo.rat[ir->rs1];
        p1_ready = ooo.prf[p1_tag].ready;
        p1_val   = ooo.prf[p1_tag].value;
    }
    /* Source 2 */
    int p2_tag = -1; word_t p2_val = 0; int p2_ready = 1;
    if (ir->rs2 > 0) {
        p2_tag   = ooo.rat[ir->rs2];
        p2_ready = ooo.prf[p2_tag].ready;
        p2_val   = ooo.prf[p2_tag].value;
    }

    /* ── Physical register allocation ───────────────────────────────────── */
    if (ir->rd != -1 && ir->rd != 0) {
        phys_rd  = freelist_pop();
        old_phys = ooo.rat[ir->rd];
        ooo.rat[ir->rd]        = phys_rd;  /* Now safe: sources already read */
        ooo.prf[phys_rd].ready = 0;
        ooo.prf[phys_rd].value = 0;
    } else if (ir->rd == 0) {
        phys_rd  = 0;   /* p0 = x0; never rename */
        old_phys = 0;
    } else {
        phys_rd  = -1;  /* No destination (stores, branches, system) */
        old_phys = -1;
    }
    rob->phys_rd  = phys_rd;
    rob->old_phys = old_phys;

    /* ── Find a free RS slot ────────────────────────────────────────────── */
    rs_slot = -1;
    for (i = 0; i < RS_SIZE; i++) {
        if (!ooo.rs[i].valid) { rs_slot = i; break; }
    }
    /* rs_slot must exist: we checked rs_count < RS_SIZE above */
    rs          = &ooo.rs[rs_slot];
    rs->valid   = 1;
    rs->ir      = *ir;
    rs->phys_rd = phys_rd;
    rs->rob_idx = rob_idx;

    /* ── Source 1 operand (using pre-rename mapping) ────────────────────── */
    if (ir->rs1 <= 0) {
        /*
         * rs1 == -1: no register source (TYPE_U, TYPE_J, TYPE_N) — use the
         *            immediate already in src1_val from ir_decode.
         * rs1 ==  0: source is x0, value is always 0.
         */
        rs->src1_ready = 1;
        rs->src1_val   = (ir->rs1 == 0) ? (word_t)0 : ir->src1_val;
        rs->src1_ptag  = 0;
    } else {
        rs->src1_ptag  = p1_tag;
        if (p1_ready) {
            rs->src1_ready = 1;
            rs->src1_val   = p1_val;
        } else {
            rs->src1_ready = 0;
            rs->src1_val   = 0;
        }
    }

    /* ── Source 2 operand (using pre-rename mapping) ────────────────────── */
    if (ir->rs2 == -1) {
        /*
         * rs2 == -1: I-type immediate already encoded in src2_val, or there
         *            is no second source at all (TYPE_U, TYPE_J, TYPE_N).
         */
        rs->src2_ready = 1;
        rs->src2_val   = ir->src2_val;
        rs->src2_ptag  = 0;
    } else if (ir->rs2 == 0) {
        rs->src2_ready = 1;
        rs->src2_val   = 0;
        rs->src2_ptag  = 0;
    } else {
        rs->src2_ptag  = p2_tag;
        if (p2_ready) {
            rs->src2_ready = 1;
            rs->src2_val   = p2_val;
        } else {
            rs->src2_ready = 0;
            rs->src2_val   = 0;
        }
    }

    ooo.latch_id_rn.valid = 0;
}

/* ── ooo_stage_id ────────────────────────────────────────────────────────── */
/*
 * Decode / pipeline stage ID→RN:
 * The instruction is already fully decoded in the IF→ID latch (ir_decode
 * was called in stage_if).  All this stage does is transfer the latch
 * forward when RN is ready to accept.
 */
static void ooo_stage_id(void)
{
    if (!ooo.latch_if_id.valid) {
        /* No instruction in IF→ID; nothing to forward */
        return;
    }

    if (ooo.latch_id_rn.valid) {
        /* RN is stalled (structural hazard or serialising wait); ID stalls */
        return;
    }

    ooo.latch_id_rn   = ooo.latch_if_id;
    ooo.latch_if_id.valid = 0;
}

/* ── ooo_stage_if ────────────────────────────────────────────────────────── */

static void ooo_stage_if(void)
{
    vaddr_t   pc;
    uint32_t  raw;
    BPredResult r;

    /* Stall if the IF→ID latch is still occupied (ID or RN stalled) */
    if (ooo.latch_if_id.valid) return;

    pc  = ooo.fetch_pc;
    raw = (uint32_t)vaddr_ifetch(pc, 4);

    ir_decode(raw, pc, pc + 4, &ooo.latch_if_id.ir);
    ooo.latch_if_id.valid = 1;
    ooo.fetch_pc = pc + 4;  /* Sequential default; EX may override on flush */

    if (g_bpred_mode) {
        r = bpred_predict(&bpred, pc);
        ooo.latch_if_id.ir.bp_predict_taken = r.taken;
        ooo.latch_if_id.ir.bp_predicted_pc  =
            (r.taken && r.btb_hit) ? r.target : pc + 4;
        if (r.taken && r.btb_hit)
            ooo.fetch_pc = r.target;
    } else {
        ooo.latch_if_id.ir.bp_predict_taken = 0;
        ooo.latch_if_id.ir.bp_predicted_pc  = pc + 4;
    }
}

/* ── ooo_cycle ───────────────────────────────────────────────────────────── */
/*
 * Advance one clock cycle.  Stages execute in reverse pipeline order so
 * each stage reads the latch values written by the *previous* cycle's
 * upstream stage (not the current cycle's).
 */
void ooo_cycle(void)
{
    ooo_stage_commit();
    ooo_stage_mem();
    ooo_stage_ex();
    ooo_stage_is();
    ooo_stage_rn();
    ooo_stage_id();
    ooo_stage_if();
    ooo_stats.cycles++;
    g_sim_cycles++;
    g_sim_instret = ooo_stats.insts;
}

/* ── ooo_report ──────────────────────────────────────────────────────────── */

void ooo_report(void)
{
    double ipc = (ooo_stats.cycles > 0)
                 ? (double)ooo_stats.insts / (double)ooo_stats.cycles
                 : 0.0;

    printf("\n=== OOO Engine Statistics ===\n");
    printf("Cycles              : %" PRIu64 "\n", ooo_stats.cycles);
    printf("Instructions        : %" PRIu64 "\n", ooo_stats.insts);
    printf("IPC                 : %.3f\n", ipc);
    printf("ROB full stalls     : %" PRIu64 "\n", ooo_stats.rob_full_stalls);
    printf("RS full stalls      : %" PRIu64 "\n", ooo_stats.rs_full_stalls);
    printf("Mispred flushes     : %" PRIu64 "\n", ooo_stats.mispred_flushes);
    printf("Serializing stalls  : %" PRIu64 "\n", ooo_stats.serializing_stalls);
    printf("=============================\n\n");

    if (g_bpred_mode) bpred_report(&bpred);
}

/* ── Multi-core entry points ─────────────────────────────────────────────── */

void ooo_init_core(Core *c)
{
    /* Save single-core globals */
    OOOState  save_ooo       = ooo;
    OOOStats  save_ooo_stats = ooo_stats;
    BranchPredictor save_bp  = bpred;

    /* Install this core's state */
    ooo       = c->ooo;
    ooo_stats = c->ooo_stats;
    bpred     = c->bpred;

    /* ooo_init() uses cpu.gpr (already loaded by core_cycle) */
    ooo_init();

    /* Save back */
    c->ooo       = ooo;
    c->ooo_stats = ooo_stats;
    c->bpred     = bpred;

    /* Restore single-core globals */
    ooo       = save_ooo;
    ooo_stats = save_ooo_stats;
    bpred     = save_bp;
}

void ooo_cycle_core(Core *c)
{
    /* Save single-core globals */
    OOOState  save_ooo       = ooo;
    OOOStats  save_ooo_stats = ooo_stats;
    BranchPredictor save_bp  = bpred;

    /* Install this core's state */
    ooo       = c->ooo;
    ooo_stats = c->ooo_stats;
    bpred     = c->bpred;

    /* Run OOO stages */
    ooo_stage_commit();
    ooo_stage_mem();
    ooo_stage_ex();
    ooo_stage_is();
    ooo_stage_rn();
    ooo_stage_id();
    ooo_stage_if();
    ooo_stats.cycles++;
    g_sim_cycles++;
    c->sim_cycles = ooo_stats.cycles;
    g_sim_instret = ooo_stats.insts;
    c->sim_instret = ooo_stats.insts;

    /* Save back */
    c->ooo       = ooo;
    c->ooo_stats = ooo_stats;
    c->bpred     = bpred;

    /* Restore single-core globals */
    ooo       = save_ooo;
    ooo_stats = save_ooo_stats;
    bpred     = save_bp;
}
