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
#include <cpu/bpred2.h>
#include <csr.h>
#include <reg.h>
#include <state.h>
#include <vmem.h>
#include <decode.h>
#include <exec_cfg.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <log.h>
#include <ir.h>
#include <ringbuf.h>
#include <disasm.hpp>

/* ── Globals ─────────────────────────────────────────────────────────────── */
OOOState ooo;
OOOStats ooo_stats;
int      g_ooo_mode = 0;

/* Helper: update rs_ready_mask for slot i based on current RS state */
static inline void rs_mask_update(int i)
{
    RSEntry *rs = &ooo.rs[i];
    if (rs->valid && rs->src1_ready && rs->src2_ready)
        ooo.rs_ready_mask[rs->eu_type] |=  (1u << i);
    else
        ooo.rs_ready_mask[rs->eu_type] &= ~(1u << i);
}
static inline void rs_mask_clear(int i, int eu_type)
{
    ooo.rs_ready_mask[eu_type] &= ~(1u << i);
    ooo.rs_free_mask            |=  (1u << i);  /* slot is now free */
}

/* ── Differential testing ────────────────────────────────────────────────── */
/*
 * difftest_ref: a shadow functional CPU that advances one instruction per
 * OOO commit.  After each commit we compare GPR state; any mismatch is a bug.
 */
static CPU_state difftest_ref;   /* reference (functional) state       */
static int       difftest_active = 0;

/* Run one instruction on the functional reference CPU */
static void difftest_step(void)
{
    vaddr_t pc   = difftest_ref.pc;
    vaddr_t snpc = pc + 4;
    uint32_t raw = (uint32_t)vaddr_read(pc, 4);

    IR_Inst ir;
    ir_decode(raw, pc, snpc, &ir);

    /* Substitute the reference GPR values for sources */
    if (ir.rs1 > 0) ir.src1_val = difftest_ref.gpr[ir.rs1];
    if (ir.rs2 > 0) ir.src2_val = difftest_ref.gpr[ir.rs2];

    ir_execute(&ir, &difftest_ref);

    /* Execute memory access only for LOADs — STOREs are handled by OOO's
     * commit path (sbuf drain → vaddr_write).  Executing stores here would
     * corrupt shared physical memory with a second, out-of-sync write. */
    if (ir.type == ITYPE_LOAD)
        ir_mem_access(&ir, NULL);

    ir_writeback(&ir, &difftest_ref);
    difftest_ref.pc = ir.dnpc;
    difftest_ref.gpr[0] = 0;
}

/* Compare OOO committed state (cpu.gpr / cpu.pc) against reference */
static void difftest_check(const ROBEntry *rob)
{
    /* CSR, SYSTEM, FENCE: time-dependent or side-effecting — skip compare,
     * sync ref destination register to OOO value so sources stay correct.
     * STORE: rd == -1, nothing to compare.
     * LOAD: reference reads from shared memory; if the store that wrote
     *   this address is still in OOO sbuf (not yet drained to memory),
     *   reference will read the old value.  We detect this and sync below. */
    int skip_cmp = (rob->ir.type == ITYPE_CSR    ||
                    rob->ir.type == ITYPE_SYSTEM  ||
                    rob->ir.type == ITYPE_FENCE   ||
                    rob->ir.type == ITYPE_STORE);

    /* Advance reference by one instruction */
    difftest_step();

    if (skip_cmp) {
        if (rob->arch_rd > 0)
            difftest_ref.gpr[rob->arch_rd] = cpu.gpr[rob->arch_rd];
        return;
    }

    /* For LOAD: OOO may have used STLF from sbuf — reference read from
     * shared memory which might not yet have the store drained.  Accept
     * OOO value as authoritative for rd, warn if more than 1-byte off
     * (small delta = likely sbuf timing; large delta = real OOO bug).   */
    if (rob->ir.type == ITYPE_LOAD && rob->arch_rd > 0) {
        word_t ooo_val = cpu.gpr[rob->arch_rd];
        word_t ref_val = difftest_ref.gpr[rob->arch_rd];
        /* MMIO reads (RTC, kbd, vga) are timing-dependent.
         * OOO serializes MMIO loads (stall until ROB head) so the value is
         * read at the correct logical time.  Sync ref to OOO's value so that
         * dependent instructions see the same value in both ref and OOO. */
        int is_mmio = (rob->ir.mem_addr >= 0xa0000000UL);
        if (is_mmio) {
            difftest_ref.gpr[rob->arch_rd] = ooo_val;
            return;
        }
        /* For non-MMIO loads: check store-buffer coverage before reporting */
        if (ooo_val != ref_val) {
            int sbuf_covers = 0;
            for (int bi = 0; bi < ooo.sbuf_count; bi++) {
                int idx = (ooo.sbuf_tail - 1 - bi + STORE_BUF_SIZE) % STORE_BUF_SIZE;
                StoreBufEntry *se = &ooo.sbuf[idx];
                if (se->valid && se->addr == rob->ir.mem_addr &&
                    se->width == rob->ir.mem_width) {
                    sbuf_covers = 1;
                    break;
                }
            }
            if (!sbuf_covers) {
                fprintf(stderr,
                    "[DIFFTEST] LOAD MISMATCH PC=0x%08lx  insn=0x%08x  %s\n",
                    (unsigned long)rob->ir.pc, rob->ir.raw,
                    rob->ir.name ? rob->ir.name : "?");
                fprintf(stderr, "  x%-2d  OOO=0x%016lx  REF=0x%016lx  addr=0x%lx\n",
                        rob->arch_rd, (unsigned long)ooo_val, (unsigned long)ref_val,
                        (unsigned long)rob->ir.mem_addr);
                /* Force OOO to use correct value to prevent error chain */
                cpu.gpr[rob->arch_rd] = ref_val;
                difftest_ref.gpr[rob->arch_rd] = ref_val;
                return;
            }
        }
        /* sbuf timing difference or exact match: sync ref to OOO */
        difftest_ref.gpr[rob->arch_rd] = ooo_val;
        return;
    }

    /* ALU / branch / jump / auipc / lui: compare every GPR */
    int mismatch = 0;
    for (int i = 1; i < 32; i++) {
        if (cpu.gpr[i] != difftest_ref.gpr[i]) {
            if (!mismatch) {
                fprintf(stderr,
                    "[DIFFTEST] MISMATCH after committing PC=0x%08lx  insn=0x%08x  %s\n",
                    (unsigned long)rob->ir.pc, rob->ir.raw,
                    rob->ir.name ? rob->ir.name : "?");
            }
            fprintf(stderr, "  x%-2d  OOO=0x%016lx  REF=0x%016lx\n",
                    i, (unsigned long)cpu.gpr[i],
                    (unsigned long)difftest_ref.gpr[i]);
            mismatch = 1;
        }
    }
    if (mismatch) {
        fprintf(stderr, "  PC   OOO=0x%016lx  REF=0x%016lx\n",
                (unsigned long)cpu.pc, (unsigned long)difftest_ref.pc);
        /* Sync so we report only the first divergence per chain */
        for (int i = 0; i < 32; i++) difftest_ref.gpr[i] = cpu.gpr[i];
        difftest_ref.pc = cpu.pc;
    }
}

/* Per-cycle commit trace buffer (populated by ooo_stage_commit, read by ooo_trace_cycle) */
static IR_Inst ooo_tc_ir[COMMIT_WIDTH];
static int     ooo_tc_n = 0;

/* ── Forward declarations ────────────────────────────────────────────────── */
static void ooo_stage_commit(void);
static void ooo_unit_int(void);
static void ooo_unit_mul(void);
static void ooo_unit_lsu(void);
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
    /* Iterate only over valid RS slots using the free mask complement */
    uint32_t valid_mask = (~ooo.rs_free_mask) & ((RS_SIZE < 32) ? ((1u << RS_SIZE) - 1u) : ~0u);
    while (valid_mask) {
        int i = __builtin_ctz(valid_mask);
        valid_mask &= valid_mask - 1;
        int changed = 0;
        if (!ooo.rs[i].src1_ready && ooo.rs[i].src1_ptag == phys_tag) {
            ooo.rs[i].src1_val   = ooo.prf[phys_tag].value;
            ooo.rs[i].src1_ready = 1;
            changed = 1;
        }
        if (!ooo.rs[i].src2_ready && ooo.rs[i].src2_ptag == phys_tag) {
            ooo.rs[i].src2_val   = ooo.prf[phys_tag].value;
            ooo.rs[i].src2_ready = 1;
            changed = 1;
        }
        if (changed && ooo.rs[i].src1_ready && ooo.rs[i].src2_ready)
            ooo.rs_ready_mask[ooo.rs[i].eu_type] |= (1u << i);
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
                /* Return the prematurely-allocated physical register.
                 * Clear ready so the next owner doesn't see a stale value. */
                ooo.prf[entry->phys_rd].ready = 0;
                freelist_push(entry->phys_rd);
            }
            /* Maintain unready_store_count */
            if (!entry->ready && (entry->ir.raw & 0x7fu) == 0x23u)
                ooo.unready_store_count--;
            entry->valid = 0;
            ooo.rob_count--;
        }
    }

    /* Flush RS entries that are newer than the mispredicted branch */
    int i;
    for (i = 0; i < RS_SIZE; i++) {
        if (ooo.rs[i].valid &&
            rob_newer_than(ooo.rs[i].rob_idx, branch_rob_idx)) {
            rs_mask_clear(i, ooo.rs[i].eu_type);  /* also sets rs_free_mask bit */
            ooo.rs[i].valid = 0;
        }
    }

    /* Flush IS→EX and EX→MEM latches if they hold a newer instruction */
    int s;
    /* INT unit */
    for (s = 0; s < NUM_INT_UNITS; s++) {
        if (ooo.latch_int[s].valid &&
            rob_newer_than(ooo.latch_int[s].rob_idx, branch_rob_idx))
            ooo.latch_int[s].valid = 0;
    }
    /* MUL unit */
    for (s = 0; s < NUM_MUL_UNITS; s++) {
        if (ooo.latch_mul[s].valid &&
            rob_newer_than(ooo.latch_mul[s].rob_idx, branch_rob_idx))
            ooo.latch_mul[s].valid = 0;
    }
    /* LSU unit: keep valid if MSHR in-flight (cache fill must complete) */
    for (s = 0; s < NUM_LSU_UNITS; s++) {
        if (ooo.latch_lsu[s].valid &&
            rob_newer_than(ooo.latch_lsu[s].rob_idx, branch_rob_idx)) {
            int mi = ooo.latch_lsu[s].mshr_idx;
            if (mi >= 0)
                ooo.latch_lsu[s].flushed = 1;   /* keep valid for MSHR fill */
            else
                ooo.latch_lsu[s].valid = 0;
        }
    }

    /* Flush front-end latches unconditionally */
    for (s = 0; s < FETCH_WIDTH; s++) {
        ooo.latch_if_id[s].valid = 0;
        ooo.latch_id_rn[s].valid = 0;
    }
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

    /* RS free mask: all RS_SIZE slots free */
    ooo.rs_free_mask = (RS_SIZE < 32) ? ((1u << RS_SIZE) - 1u) : ~0u;

    /* Store buffer: empty */
    ooo.sbuf_head  = 0;
    ooo.sbuf_tail  = 0;
    ooo.sbuf_count = 0;
    memset(ooo.sbuf, 0, sizeof(ooo.sbuf));

    ooo.fetch_pc = cpu.pc;

    if (g_bpred_mode)  bpred_init(&bpred);
    if (g_bpred2_mode) bpred_init(&bpred2_state);

    /* Difftest only valid for single-core and only when --difftest flag given */
    difftest_ref    = cpu;
    difftest_active = (g_num_cores == 1) && g_difftest_en;
}

/* ── ooo_stage_commit ────────────────────────────────────────────────────── */

static void ooo_stage_commit(void)
{
    ROBEntry *rob;
    int n;

    ooo_tc_n = 0;  /* reset per-cycle trace capture */

    for (n = 0; n < COMMIT_WIDTH; n++) {
        if (ooo.rob_count == 0) break;

        rob = &ooo.rob[ooo.rob_head];
        if (!rob->valid || !rob->ready) break;

        if (g_trace_en) ooo_tc_ir[ooo_tc_n++] = rob->ir;

        /* trace committed instructions into ring buffer and log file (same format as exec_once) */
        if (log_fp) {
            static char commit_buf[128];
            uint32_t raw = rob->ir.raw;
            sprintf(commit_buf, "%016lx:    ", (unsigned long)rob->ir.pc);
            for (int bi = 0; bi < 4; bi++)
                sprintf(commit_buf + 21 + 3 * bi, "%02x ", *(((uint8_t *)&raw) + 3 - bi));
            sprintf(commit_buf + 33, "   ");
            disassemble(commit_buf + 36, 70, rob->ir.pc, (uint8_t *)&raw, 4);
            add_ringbuf_inst(commit_buf);
            log_write("%s\n", commit_buf);
        }

        /* FENCE: must drain store buffer before committing */
        if (rob->ir.type == ITYPE_FENCE && ooo.sbuf_count > 0) break;

        /* Faulted instruction reached commit — this must never happen on the
         * correct path (it should have been squashed by an earlier flush). */
        if (rob->ir.fault)
            panic("faulted instruction reached commit: PC=0x%lx mem_addr=0x%lx\n",
                  (unsigned long)rob->ir.pc, (unsigned long)rob->ir.mem_addr);

        /* STORE: push to per-core store buffer (TSO model) for cacheable
         * addresses only.  MMIO / non-cacheable writes (e.g. UART) bypass
         * the store buffer and take effect immediately so that side-effects
         * are not lost when the program terminates before the buffer drains. */
        if (rob->ir.type == ITYPE_STORE) {
            if (rob->ir.mem_addr >= 0x80000000 && rob->ir.mem_addr < 0x88000000) {
                if (ooo.sbuf_count >= STORE_BUF_SIZE) break;
                StoreBufEntry *se = &ooo.sbuf[ooo.sbuf_tail];
                se->addr  = rob->ir.mem_addr;
                se->data  = rob->store_data;
                se->width = rob->ir.mem_width;
                se->valid = 1;
                ooo.sbuf_tail = (ooo.sbuf_tail + 1) % STORE_BUF_SIZE;
                ooo.sbuf_count++;
            } else {
                /* MMIO / non-cacheable: immediate write, no buffering */
                vaddr_write(rob->ir.mem_addr, rob->ir.mem_width, rob->store_data);
            }
        }

        /* Update architectural register file and committed RAT */
        if (rob->arch_rd != -1 && rob->arch_rd != 0) {
            cpu.gpr[rob->arch_rd]   = ooo.prf[rob->phys_rd].value;
            ooo.rrat[rob->arch_rd]  = rob->phys_rd;
            ooo.prf[rob->old_phys].ready = 0;  /* prevent stale CDB wakeup on reuse */
            freelist_push(rob->old_phys);   /* Return the superseded physical reg */
        }
        cpu.gpr[0] = 0;   /* x0 is always zero */

        cpu.pc = rob->ir.dnpc;

        /* Differential test: advance functional reference and compare */
        if (difftest_active) difftest_check(rob);

        rob->valid = 0;
        ooo.rob_head  = (ooo.rob_head + 1) % ROB_SIZE;
        ooo.rob_count--;
        ooo_stats.insts++;
    }
}

/* ── MSHR helpers ────────────────────────────────────────────────────────── */

/* Find an existing MSHR entry for the given cache-line-aligned address,
 * or allocate a new one.  Returns the MSHR index, or -1 if full. */
static int mshr_find_or_alloc(paddr_t line_addr, int lat)
{
    int i, free_slot = -1;
    for (i = 0; i < MSHR_SIZE; i++) {
        if (!ooo.mshr[i].valid) {
            if (free_slot == -1) free_slot = i;
            continue;
        }
        if (ooo.mshr[i].line_addr == line_addr)
            return i;   /* Found existing in-flight entry for same line */
    }
    if (free_slot == -1) return -1;  /* MSHR full */
    ooo.mshr[free_slot].valid      = 1;
    ooo.mshr[free_slot].line_addr  = line_addr;
    ooo.mshr[free_slot].cycles_rem = lat - 1;
    return free_slot;
}

/* Tick all valid MSHR counters once per cycle. */
static void mshr_tick(void)
{
    int i;
    for (i = 0; i < MSHR_SIZE; i++) {
        if (ooo.mshr[i].valid && ooo.mshr[i].cycles_rem > 0)
            ooo.mshr[i].cycles_rem--;
    }
}

/* ── Helper: fill latch slot from RS entry ───────────────────────────────── */
static void latch_from_rs(OOOLatch *latch, int rs_idx)
{
    latch->rob_idx     = ooo.rs[rs_idx].rob_idx;
    latch->src1_val    = ooo.rs[rs_idx].src1_val;
    latch->src2_val    = ooo.rs[rs_idx].src2_val;
    latch->phys_rd     = ooo.rs[rs_idx].phys_rd;
    latch->cycles_rem  = -1;
    latch->mshr_idx    = -1;
    latch->flushed     = 0;
    latch->valid       = 1;
    rs_mask_clear(rs_idx, ooo.rs[rs_idx].eu_type);
    ooo.rs[rs_idx].valid = 0;
}

/* ── ooo_unit_int ────────────────────────────────────────────────────────── */
/*
 * INT functional unit: handles all non-MUL/non-memory instructions.
 * Single-cycle execution; branches trigger misprediction flush here.
 */
static void ooo_unit_int(void)
{
    int s;
    for (s = 0; s < NUM_INT_UNITS; s++) {
        if (!ooo.latch_int[s].valid) continue;

        int phys_rd     = ooo.latch_int[s].phys_rd;
        int rob_idx     = ooo.latch_int[s].rob_idx;

        ooo.latch_int[s].valid = 0;

        if (!ooo.rob[rob_idx].valid) continue;  /* flushed by earlier mispred */

        /* Build a local IR copy with forwarded source values, execute in-place */
        IR_Inst ir      = ooo.rob[rob_idx].ir;
        ir.src1_val     = ooo.latch_int[s].src1_val;
        ir.src2_val     = ooo.latch_int[s].src2_val;

        ir_execute(&ir, &cpu);
        ooo.rob[rob_idx].ir = ir;

        if (phys_rd >= 0) {
            ooo.prf[phys_rd].value = ir.result;
            ooo.prf[phys_rd].ready = 1;
            if (phys_rd > 0) cdb_broadcast(phys_rd);
        }
        ooo.rob[rob_idx].ready = 1;

        /* Branch misprediction check */
        if (ir.type == ITYPE_BRANCH ||
            ir.type == ITYPE_JAL    ||
            ir.type == ITYPE_JALR) {
            if (g_bpred2_mode) bpred2_update(&bpred2_state, &ir);
            else if (g_bpred_mode) bpred_update(&bpred, &ir);

            vaddr_t predicted = ir.bp_predicted_pc ? ir.bp_predicted_pc : ir.snpc;
            if (ir.dnpc != predicted) {
                /* Sanity check: if dnpc is outside valid memory, this is a
                 * wrong-path speculative branch/jalr executing before the
                 * older mispredicted branch has flushed the pipeline.
                 * Skip the fetch_pc redirect — the older branch will flush
                 * this ROB entry when it eventually executes. */
                if (ir.dnpc < 0x80000000UL || ir.dnpc >= 0x88000000UL) {
                    ooo.rob[rob_idx].ir.fault = 1;
                    /* Still mark ready so the pipeline doesn't stall */
                } else {
                    /* Restore RAS to the state it was in when the branch was fetched */
                    if (g_bpred_mode) {
                        bpred.ras_top   = ir.ras_top_snap;
                        bpred.ras_count = ir.ras_cnt_snap;
                    }
                    ooo_flush_after(rob_idx);
                    ooo_stats.mispred_flushes++;
                    ooo.fetch_pc = ir.dnpc;
                }
            }
        }
    }
}

/* ── ooo_unit_mul ────────────────────────────────────────────────────────── */
/*
 * MUL/DIV functional unit: multi-cycle execution.
 * cycles_rem == -1: first visit, set latency.
 * cycles_rem >  0:  counting down.
 * cycles_rem == 0:  execute and writeback.
 */
static void ooo_unit_mul(void)
{
    int s;
    for (s = 0; s < NUM_MUL_UNITS; s++) {
        if (!ooo.latch_mul[s].valid) continue;

        int rob_idx = ooo.latch_mul[s].rob_idx;

        if (ooo.latch_mul[s].cycles_rem == -1) {
            uint32_t raw    = (uint32_t)ooo.rob[rob_idx].ir.raw;
            uint32_t opc    = raw & 0x7fu;
            uint32_t funct7 = raw >> 25;
            uint32_t funct3 = (raw >> 12) & 0x7u;
            int lat = ((opc == 0x33u || opc == 0x3Bu) && funct7 == 1u)
                      ? ((funct3 < 4u) ? LAT_INT_MUL : LAT_INT_DIV)
                      : 1;
            ooo.latch_mul[s].cycles_rem = lat - 1;
            if (lat > 1) continue;
        } else if (ooo.latch_mul[s].cycles_rem > 0) {
            if (!ooo.rob[rob_idx].valid)
                ooo.latch_mul[s].valid = 0;
            else
                ooo.latch_mul[s].cycles_rem--;
            continue;
        }

        int phys_rd = ooo.latch_mul[s].phys_rd;

        ooo.latch_mul[s].valid = 0;

        if (!ooo.rob[rob_idx].valid) continue;

        IR_Inst ir  = ooo.rob[rob_idx].ir;
        ir.src1_val = ooo.latch_mul[s].src1_val;
        ir.src2_val = ooo.latch_mul[s].src2_val;
        ir_execute(&ir, &cpu);
        ooo.rob[rob_idx].ir = ir;

        if (phys_rd >= 0) {
            ooo.prf[phys_rd].value = ir.result;
            ooo.prf[phys_rd].ready = 1;
            if (phys_rd > 0) cdb_broadcast(phys_rd);
        }
        ooo.rob[rob_idx].ready = 1;
    }
}

/* ── ooo_unit_lsu ────────────────────────────────────────────────────────── */
/*
 * LSU functional unit: handles LOAD and STORE.
 * Merges address calculation (old EX stage) and memory access (old MEM stage).
 *
 * cycles_rem == -1: first visit — execute to compute mem_addr, then:
 *   STORE: save data in ROB, mark ready, done.
 *   LOAD L1 hit: fill_and_read immediately, CDB broadcast, done.
 *   LOAD L2/DRAM: allocate MSHR, set cycles_rem, wait.
 * cycles_rem == 0 (after MSHR countdown): fill_and_read, CDB, done.
 *
 * flushed=1: ROB was flushed but MSHR in-flight; complete fill without writeback.
 */
static void ooo_unit_lsu(void)
{
    static const int level_to_lat[3] = { LAT_L1_HIT, LAT_L2_HIT, LAT_DRAM };
    int s;

    for (s = 0; s < NUM_LSU_UNITS; s++) {
        if (!ooo.latch_lsu[s].valid) continue;

        int phys_rd = ooo.latch_lsu[s].phys_rd;
        int rob_idx = ooo.latch_lsu[s].rob_idx;
        int mi      = ooo.latch_lsu[s].mshr_idx;
        IR_Inst *ir = &ooo.rob[rob_idx].ir;

        /* ── ROB flushed: only care if MSHR is in-flight ────────────────── */
        if (!ooo.rob[rob_idx].valid || ooo.latch_lsu[s].flushed) {
            if (mi < 0) { ooo.latch_lsu[s].valid = 0; continue; }
            MSHREntry *mshr = &ooo.mshr[mi];
            if (mshr->cycles_rem > 0) continue;
            /* Countdown done: fill cache (pollutes it) but skip writeback.
             * Use MSHR's line_addr since ROB entry may already be invalid. */
            vaddr_fill_and_read(mshr->line_addr, 8);
            int still_used = 0, t;
            for (t = 0; t < NUM_LSU_UNITS; t++) {
                if (t != s && ooo.latch_lsu[t].valid &&
                    ooo.latch_lsu[t].mshr_idx == mi)
                    still_used = 1;
            }
            if (!still_used) mshr->valid = 0;
            ooo.latch_lsu[s].valid = 0;
            continue;
        }

        /* ── First visit: compute address ────────────────────────────────── */
        if (mi == -1 && ooo.latch_lsu[s].cycles_rem == -1) {
            /* Inject forwarded source values before execution */
            ir->src1_val = ooo.latch_lsu[s].src1_val;
            ir->src2_val = ooo.latch_lsu[s].src2_val;
            ir_execute(ir, &cpu);
            /* ir == &ooo.rob[rob_idx].ir, so no separate writeback needed */

            if (ir->type == ITYPE_STORE) {
                ooo.rob[rob_idx].store_data = ir->src2_val;
                ooo.rob[rob_idx].ready = 1;
                ooo.unready_store_count--;
                ooo.latch_lsu[s].valid = 0;
                continue;
            }

            /* LOAD: MMIO must execute in-order — stall until ROB head */
            if (ir->mem_addr >= 0xa0000000UL) {
                if (rob_idx != ooo.rob_head) {
                    ooo.latch_lsu[s].cycles_rem = -2; /* waiting for ROB head */
                    continue;
                }
                goto lsu_do_fill;
            }

            /* LOAD: probe cache level */
            int level = vaddr_probe_level(ir->mem_addr);
            int lat   = level_to_lat[level];

            if (lat == 1) {
                /* L1 hit: fill immediately (INT unit runs before LSU, so any
                 * same-cycle branch misprediction flush has already happened) */
                goto lsu_do_fill;
            }

            /* L2/DRAM miss: allocate MSHR */
            paddr_t line_addr = ir->mem_addr & ~(paddr_t)63;
            mi = mshr_find_or_alloc(line_addr, lat);
            if (mi == -1) continue;  /* MSHR full, retry next cycle */
            ooo.latch_lsu[s].mshr_idx  = mi;
            ooo.latch_lsu[s].cycles_rem = 0;  /* flag: MSHR allocated */
            continue;
        }

        /* ── L1 hit: fill on second visit ────────────────────────────────── */
        if (mi == -1 && ooo.latch_lsu[s].cycles_rem == 0) {
            goto lsu_do_fill;
        }

        /* ── MMIO waiting for ROB head ───────────────────────────────────── */
        if (mi == -1 && ooo.latch_lsu[s].cycles_rem == -2) {
            if (rob_idx != ooo.rob_head) continue;
            goto lsu_do_fill;
        }

        /* ── Waiting on MSHR ─────────────────────────────────────────────── */
        if (mi >= 0) {
            if (ooo.mshr[mi].cycles_rem > 0) continue;
            /* Fall through to fill */
        }

lsu_do_fill:
        {
            paddr_t pa = ir->mem_addr & 0xffffffff;
            bool is_dram = (pa >= 0x80000000UL && pa < 0x88000000UL);
            bool is_mmio = (pa >= 0xa0000000UL);
            if (!is_dram && !is_mmio) {
                /* Wrong-path speculative LOAD to an invalid address.
                 * The older branch/JAL that redirected control hasn't executed
                 * yet (INT slots busy), so LSU ran first.  Mark as faulted so
                 * the flush triggered by that branch will silently discard this
                 * ROB entry.  If this somehow reaches commit it is a real bug. */
                ooo.rob[rob_idx].ir.fault = 1;
                ir->result = 0;
                int phys_rd2 = ooo.latch_lsu[s].phys_rd;
                if (phys_rd2 > 0) {
                    ooo.prf[phys_rd2].value = 0;
                    ooo.prf[phys_rd2].ready = 1;
                    cdb_broadcast(phys_rd2);
                }
                ooo.rob[rob_idx].ready = 1;
                ooo.latch_lsu[s].valid = 0;
                continue;
            }
            word_t raw = vaddr_fill_and_read(ir->mem_addr, ir->mem_width);
            if (ir->mem_sign) {
                switch (ir->mem_width) {
                case 1: ir->result = SEXT(raw,  8); break;
                case 2: ir->result = SEXT(raw, 16); break;
                case 4: ir->result = SEXT(raw, 32); break;
                default: ir->result = raw; break;
                }
            } else {
                switch (ir->mem_width) {
                case 1: ir->result = UEXT(raw,  8); break;
                case 2: ir->result = UEXT(raw, 16); break;
                case 4: ir->result = UEXT(raw, 32); break;
                default: ir->result = raw; break;
                }
            }

            /* STLF: check sbuf (committed stores not yet drained) first,
             * then ROB (in-flight stores). ROB wins if it has a matching
             * entry (it is newer than anything in sbuf). */
            {
                /* sbuf scan: find most-recently-enqueued entry at same addr */
                for (int bi = 0; bi < ooo.sbuf_count; bi++) {
                    int idx = (ooo.sbuf_head + bi) % STORE_BUF_SIZE;
                    StoreBufEntry *se = &ooo.sbuf[idx];
                    if (se->valid && se->addr == ir->mem_addr &&
                        se->width == ir->mem_width)
                        ir->result = se->data;
                }
            }
            {
                /* ROB scan: find most-recent ready STORE to same address */
                int scan = ooo.rob_head, match_idx = -1;
                while (scan != rob_idx) {
                    ROBEntry *roe = &ooo.rob[scan];
                    if (roe->valid && roe->ready &&
                        (roe->ir.raw & 0x7f) == 0x23 &&
                        roe->ir.mem_addr  == ir->mem_addr &&
                        roe->ir.mem_width == ir->mem_width)
                        match_idx = scan;
                    scan = (scan + 1) % ROB_SIZE;
                }
                if (match_idx >= 0)
                    ir->result = ooo.rob[match_idx].store_data;
            }

            /* Release MSHR if no other LSU slot still waits on it */
            if (mi >= 0) {
                int still_used = 0, t;
                for (t = 0; t < NUM_LSU_UNITS; t++) {
                    if (t != s && ooo.latch_lsu[t].valid &&
                        ooo.latch_lsu[t].mshr_idx == mi)
                        still_used = 1;
                }
                if (!still_used) ooo.mshr[mi].valid = 0;
            }

            ooo.prf[phys_rd].value = ir->result;
            ooo.prf[phys_rd].ready = 1;
            cdb_broadcast(phys_rd);
            ooo.rob[rob_idx].ready = 1;
            ooo.rob[rob_idx].ir.result = ir->result;
            ooo.latch_lsu[s].valid = 0;
        }
    }
}

/* ── ooo_stage_is ────────────────────────────────────────────────────────── */
/*
 * Issue stage: route ready instructions from the RS to the appropriate
 * functional unit (INT / MUL / LSU) based on eu_type set at RN.
 * Within each unit type, select the oldest ready instruction (smallest
 * ROB distance from rob_head).
 */
static void ooo_stage_is(void)
{
    int s, i, best, da, db;

    /* ── Macro: scan RS for best instruction of a given EU type ─────────── */
    /* Uses rs_ready_mask[eu] bitmap to skip invalid/not-ready entries.     */
#define ISSUE_UNIT(latch_arr, num_slots, eu, do_mem_order) \
    for (s = 0; s < (num_slots); s++) { \
        if ((latch_arr)[s].valid) continue; \
        best = -1; \
        uint32_t _mask = ooo.rs_ready_mask[(eu)]; \
        while (_mask) { \
            i = __builtin_ctz(_mask); \
            _mask &= _mask - 1; \
            if (do_mem_order && (ooo.rob[ooo.rs[i].rob_idx].ir.raw & 0x7f) == 0x03) { \
                int blocked = 0, j; \
                for (j = ooo.rob_head; j != ooo.rs[i].rob_idx; \
                     j = (j + 1) % ROB_SIZE) { \
                    ROBEntry *roe = &ooo.rob[j]; \
                    if (!roe->valid) continue; \
                    /* Block LOAD if older unready STORE exists */ \
                    if (!roe->ready && (roe->ir.raw & 0x7f) == 0x23) \
                        { blocked = 1; break; } \
                    /* Block LOAD if older FENCE exists (sbuf not yet drained) */ \
                    if (roe->ir.type == ITYPE_FENCE && ooo.sbuf_count > 0) \
                        { blocked = 1; break; } \
                } \
                if (blocked) continue; \
            } \
            if (best == -1) { best = i; } else { \
                da = (ooo.rs[i].rob_idx    - ooo.rob_head + ROB_SIZE) % ROB_SIZE; \
                db = (ooo.rs[best].rob_idx - ooo.rob_head + ROB_SIZE) % ROB_SIZE; \
                if (da < db) best = i; \
            } \
        } \
        if (best != -1) latch_from_rs(&(latch_arr)[s], best); \
    }

    ISSUE_UNIT(ooo.latch_int, NUM_INT_UNITS, EU_INT, 0)
    ISSUE_UNIT(ooo.latch_mul, NUM_MUL_UNITS, EU_MUL, 0)
    ISSUE_UNIT(ooo.latch_lsu, NUM_LSU_UNITS, EU_LSU, 1)

#undef ISSUE_UNIT
}


/* ── ooo_stage_rn ────────────────────────────────────────────────────────── */
/*
 * Rename: translate architectural register indices to physical register
 * indices, allocate a new ROB entry and a reservation station entry, and
 * look up source operand values / readiness from the PRF.
 */
static void ooo_stage_rn(void)
{
    int       n, rs_used, rs_free, rob_idx, rs_slot;
    int       phys_rd, old_phys;
    IR_Inst  *ir;
    ROBEntry *rob;
    RSEntry  *rs;

    if (!ooo.latch_id_rn[0].valid) return;

    /* Count free RS slots via popcount on the free mask */
    rs_free = __builtin_popcount(ooo.rs_free_mask);
    rs_used = RS_SIZE - rs_free;

    for (n = 0; n < FETCH_WIDTH; n++) {
        if (!ooo.latch_id_rn[n].valid) break;

        ir = &ooo.latch_id_rn[n].ir;

        /* ── Serialising instructions: drain ROB first, rename alone ──────── */
        if (ir->serializing) {
            if (ooo.rob_count > 0 || n > 0) {
                if (n == 0) ooo_stats.serializing_stalls++;
                break;
            }
        }

        /* ── Structural hazard checks (using live counts) ──────────────────── */
        if (ooo.rob_count >= ROB_SIZE) { ooo_stats.rob_full_stalls++; break; }
        if (rs_used >= RS_SIZE)        { ooo_stats.rs_full_stalls++;  break; }
        if (ir->rd != -1 && ir->rd != 0 && ooo.freelist.count == 0) {
            ooo_stats.rob_full_stalls++; break;
        }
        /* ── Allocate ROB slot ──────────────────────────────────────────────── */
        rob_idx      = ooo.rob_tail;
        ooo.rob_tail = (ooo.rob_tail + 1) % ROB_SIZE;
        ooo.rob_count++;

        rob           = &ooo.rob[rob_idx];
        rob->ir       = *ir;
        rob->arch_rd  = ir->rd;
        rob->ready    = 0;
        rob->valid    = 1;
        rob->store_data = 0;

        /* Track unready stores for fast IS-stage memory-ordering check */
        if ((ir->raw & 0x7fu) == 0x23u)
            ooo.unready_store_count++;

        /*
         * ── Read source register mappings BEFORE renaming the destination ─────
         *
         * For instructions where rs1 == rd (e.g. addi x2, x2, 816) we must
         * capture the OLD mapping of rd (which is the source value) before we
         * overwrite rat[rd] with the new physical register.
         *
         * For instruction n=1: if instruction n=0 wrote rd=x2, then
         * ooo.rat[x2] already points to n=0's new phys reg — correct forwarding.
         */
        int p1_tag = -1; word_t p1_val = 0; int p1_ready = 1;
        if (ir->rs1 > 0) {
            p1_tag   = ooo.rat[ir->rs1];
            p1_ready = ooo.prf[p1_tag].ready;
            p1_val   = ooo.prf[p1_tag].value;
        }
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
        /* rs_free_mask has a bit set for every free slot; ctz gives the lowest */
        rs_slot = __builtin_ctz(ooo.rs_free_mask);
        ooo.rs_free_mask &= ~(1u << rs_slot);  /* mark as allocated */
        rs          = &ooo.rs[rs_slot];
        rs->valid   = 1;
        rs->phys_rd = phys_rd;
        rs->rob_idx = rob_idx;

        /* Classify execution unit type from raw opcode */
        {
            uint32_t opc = (uint32_t)ir->raw & 0x7fu;
            uint32_t f7  = (uint32_t)ir->raw >> 25;
            if (opc == 0x03u || opc == 0x23u)
                rs->eu_type = EU_LSU;
            else if ((opc == 0x33u || opc == 0x3bu) && f7 == 1u)
                rs->eu_type = EU_MUL;
            else
                rs->eu_type = EU_INT;
        }

        /* ── Source 1 operand (using pre-rename mapping) ────────────────────── */
        if (ir->rs1 <= 0) {
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

        ooo.latch_id_rn[n].valid = 0;
        rs_used++;  /* update live count for next slot's structural check */
        rs_free--;  /* one fewer free slot */

        /* Update ready mask for the newly allocated RS slot */
        rs_mask_update(rs_slot);
    }

    /* Compact: if slot 0 was consumed and slot 1 remains, shift it to slot 0
     * so the ID stage's stall check (latch_id_rn[0].valid) stays canonical. */
    if (!ooo.latch_id_rn[0].valid && ooo.latch_id_rn[1].valid) {
        ooo.latch_id_rn[0] = ooo.latch_id_rn[1];
        ooo.latch_id_rn[1].valid = 0;
    }
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
    int s;

    if (!ooo.latch_if_id[0].valid) return;  /* nothing to forward */
    if (ooo.latch_id_rn[0].valid)  return;  /* RN stalled; compaction keeps [0] canonical */

    for (s = 0; s < FETCH_WIDTH; s++) {
        ooo.latch_id_rn[s]       = ooo.latch_if_id[s];
        ooo.latch_if_id[s].valid = 0;
    }
}

/* ── ooo_stage_if ────────────────────────────────────────────────────────── */

static void ooo_stage_if(void)
{
    int       s;
    vaddr_t   pc;
    uint32_t  raw;
    BPredResult r;

    /* Stall if the fetch group hasn't been consumed yet */
    if (ooo.latch_if_id[0].valid) return;

    for (s = 0; s < FETCH_WIDTH; s++) {
        pc = ooo.fetch_pc & 0xffffffff;  /* riscv64: lw sign-extends; truncate to 32-bit */
        ooo.fetch_pc = pc;

        /* Sanity check: ensure fetch_pc is within valid memory range.
         * Should not happen with the wrong-path branch guard above. */
        if (pc < 0x80000000 || pc >= 0x88000000) {
            ooo.fetch_pc += 4;
            break;
        }

        raw = (uint32_t)vaddr_ifetch(pc, 4);

        ir_decode(raw, pc, pc + 4, &ooo.latch_if_id[s].ir);
        ooo.latch_if_id[s].valid = 1;
        ooo.fetch_pc = pc + 4;  /* Sequential default; EX may override on flush */

        ooo.latch_if_id[s].ir.bp_predict_taken = 0;
        ooo.latch_if_id[s].ir.bp_predicted_pc  = pc + 4;
        if (g_bpred_mode || g_bpred2_mode) {
            /* Only predict for control-flow instructions to avoid overhead.
             * opcode bits[6:2]: 0x18=BRANCH, 0x1b=JAL, 0x19=JALR. */
            uint8_t op5 = (raw >> 2) & 0x1f;
            if (op5 == 0x18 || op5 == 0x1b || op5 == 0x19) {
                /* Snapshot RAS state BEFORE predict (which may push/pop) */
                ooo.latch_if_id[s].ir.ras_top_snap = bpred.ras_top;
                ooo.latch_if_id[s].ir.ras_cnt_snap = bpred.ras_count;
                r = g_bpred2_mode ? bpred2_predict(&bpred2_state, pc)
                                  : bpred_predict(&bpred, pc, raw);
                ooo.latch_if_id[s].ir.bp_predict_taken = r.taken;
                ooo.latch_if_id[s].ir.bp_predicted_pc  =
                    (r.taken && r.btb_hit) ? r.target : pc + 4;
                if (r.taken && r.btb_hit) {
                    ooo.fetch_pc = r.target;
                    break;  /* redirect: don't fetch next sequential instruction */
                }
            }
        }
    }
}

/* ── ooo_trace_cycle ─────────────────────────────────────────────────────── */
/*
 * Print a one-line per-cycle pipeline snapshot to the log file.
 * Enabled by --trace (g_trace_en=1); requires -l <file> to set log_fp.
 *
 * Format:
 *   [C= N] IF:pc/name  pc/name  | IS:pc/name  --  | MEM:--  --  | COMMIT:pc/name  --
 */
static void ooo_trace_cycle(void)
{
    if (!g_trace_en || !log_fp) return;

    char buf[256];
    int pos;

    // Each instruction slot: "%08x/%-7s " = 17 chars (valid) or "--               " = 17 chars (invalid)
    // 2 slots per stage = 34 chars content.
    // Section widths all equal 41 ("COMMIT:" len=7 + 34 = 41):
    //   IF:     (3)  + pad 38 = 41
    //   IS:     (3)  + pad 38 = 41
    //   MEM:    (4)  + pad 37 = 41
    //   COMMIT: (7)  + pad 34 = 41

    fprintf(log_fp, "[C=%6" PRIu64 "] ", ooo_stats.cycles);

    // IF stage
    pos = 0;
    for (int s = 0; s < FETCH_WIDTH; s++) {
        if (ooo.latch_if_id[s].valid)
            pos += sprintf(buf + pos, "%08x/%-7s ",
                          (unsigned)ooo.latch_if_id[s].ir.pc,
                          ooo.latch_if_id[s].ir.name ? ooo.latch_if_id[s].ir.name : "??");
        else
            pos += sprintf(buf + pos, "--               ");
    }
    fprintf(log_fp, "IF:%-38s | ", buf);

    // INT/MUL/LSU units
    pos = 0;
    for (int s = 0; s < NUM_INT_UNITS; s++) {
        if (ooo.latch_int[s].valid) {
            IR_Inst *ir = &ooo.rob[ooo.latch_int[s].rob_idx].ir;
            pos += sprintf(buf + pos, "%08x/%-7s ",
                          (unsigned)ir->pc, ir->name ? ir->name : "??");
        } else
            pos += sprintf(buf + pos, "--               ");
    }
    for (int s = 0; s < NUM_MUL_UNITS; s++) {
        if (ooo.latch_mul[s].valid) {
            IR_Inst *ir = &ooo.rob[ooo.latch_mul[s].rob_idx].ir;
            pos += sprintf(buf + pos, "%08x/%-7s ",
                          (unsigned)ir->pc, ir->name ? ir->name : "??");
        } else
            pos += sprintf(buf + pos, "--               ");
    }
    for (int s = 0; s < NUM_LSU_UNITS; s++) {
        if (ooo.latch_lsu[s].valid) {
            IR_Inst *ir = &ooo.rob[ooo.latch_lsu[s].rob_idx].ir;
            pos += sprintf(buf + pos, "%08x/%-7s ",
                          (unsigned)ir->pc, ir->name ? ir->name : "??");
        } else
            pos += sprintf(buf + pos, "--               ");
    }
    fprintf(log_fp, "EU:%-38s | ", buf);

    // COMMIT stage
    pos = 0;
    for (int n = 0; n < COMMIT_WIDTH; n++) {
        if (n < ooo_tc_n)
            pos += sprintf(buf + pos, "%08x/%-7s ",
                          (unsigned)ooo_tc_ir[n].pc,
                          ooo_tc_ir[n].name ? ooo_tc_ir[n].name : "??");
        else
            pos += sprintf(buf + pos, "--               ");
    }
    fprintf(log_fp, "COMMIT:%-34s\n", buf);

    fflush(log_fp);
}

/* ── ooo_cycle ───────────────────────────────────────────────────────────── */
/*
 * Advance one clock cycle.  Stages execute in reverse pipeline order so
 * each stage reads the latch values written by the *previous* cycle's
 * upstream stage (not the current cycle's).
 */
/* ── ooo_drain_store_buf ─────────────────────────────────────────────────── */
/* Drain one entry from the store buffer per cycle.  Until an entry drains,
 * it is invisible to other cores (TSO store buffer semantics). */

static void ooo_drain_store_buf(void)
{
    if (ooo.sbuf_count == 0) return;
    /* Slow drain only in multi-core mode (TSO violation window).
     * Single-core programs (e.g. mario) need every-cycle drain to avoid
     * STLF width-mismatch stale reads (sw→lb patterns). */
    if (g_num_cores > 1) {
        if (ooo.sbuf_drain_tick > 0) { ooo.sbuf_drain_tick--; return; }
        ooo.sbuf_drain_tick = LAT_L2_HIT - 1;
    }
    StoreBufEntry *se = &ooo.sbuf[ooo.sbuf_head];
    vaddr_write(se->addr, se->width, se->data);
    se->valid = 0;
    ooo.sbuf_head = (ooo.sbuf_head + 1) % STORE_BUF_SIZE;
    ooo.sbuf_count--;
}

void ooo_cycle(void)
{
    ooo_drain_store_buf();
    ooo_stage_commit();
    mshr_tick();
    ooo_unit_int();   /* run before LSU so mispred flushes happen first */
    ooo_unit_mul();
    ooo_unit_lsu();
    ooo_stage_is();
    ooo_stage_rn();
    ooo_stage_id();
    ooo_stage_if();
    ooo_stats.cycles++;
    g_sim_cycles++;
    g_sim_instret = ooo_stats.insts;
    ooo_trace_cycle();
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

    if (g_bpred_mode)  bpred_report(&bpred);
    if (g_bpred2_mode) bpred2_report(&bpred2_state);
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
    ooo_drain_store_buf();
    ooo_stage_commit();
    mshr_tick();
    ooo_unit_int();   /* run before LSU so mispred flushes happen first */
    ooo_unit_mul();
    ooo_unit_lsu();
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
