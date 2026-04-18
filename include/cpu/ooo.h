/*
 * include/cpu/ooo.h — Tomasulo OOO Engine (Phase 4)
 *
 * 6-stage out-of-order pipeline:
 *   IF → ID → RN → IS → [INT|MUL|LSU units] → ROB_Commit
 *
 * Explicit register renaming via RAT + physical register file (PRF).
 * Reservation stations (RS) for out-of-order issue.
 * Re-Order Buffer (ROB) for in-order commit.
 * Issue width and execution unit latencies configured via exec_cfg.h.
 *
 * Back-end functional units run in parallel:
 *   - INT unit (NUM_INT_UNITS slots): ALU, branch, jump, CSR
 *   - MUL unit (NUM_MUL_UNITS slots): MUL/DIV (multi-cycle)
 *   - LSU unit (NUM_LSU_UNITS slots): LOAD/STORE (addr calc + memory access)
 */

#ifndef __CPU_OOO_H__
#define __CPU_OOO_H__

#include <common.h>
#include <ir.h>
#include <pipeline.h>   /* PipeReg */
#include <stdint.h>
#include <exec_cfg.h>

/* ── Physical register file ─────────────────────────────────────────────── */
#define NUM_PHYS_REGS 64

typedef struct {
    word_t value;
    int    ready;   /* 1 = value is valid, 0 = waiting for CDB */
} PhysReg;

/* ── Free list (circular queue of free physical register indices) ────────── */
#define FREE_LIST_SIZE NUM_PHYS_REGS

typedef struct {
    int regs[FREE_LIST_SIZE];
    int head;
    int tail;
    int count;
} FreeList;

/* ── Re-Order Buffer ─────────────────────────────────────────────────────── */
#define ROB_SIZE 32

typedef struct {
    IR_Inst ir;         /* Instruction snapshot (pc, type, mem_addr, etc.) */
    int     arch_rd;    /* Architectural dest register; -1 = none          */
    int     phys_rd;    /* Allocated physical dest register; -1 = none     */
    int     old_phys;   /* Previous physical reg for arch_rd (restored on flush) */
    word_t  store_data; /* STORE data value; committed to memory at ROB commit */
    int     ready;      /* 1 = EX/MEM completed, instruction may commit    */
    int     valid;      /* 1 = slot is occupied                            */
} ROBEntry;

/* ── Reservation Station ─────────────────────────────────────────────────── */
#define RS_SIZE 16

typedef struct {
    int     valid;
    int     phys_rd;    /* Target physical register (-1 = none)             */
    int     rob_idx;    /* Corresponding ROB slot (IR is read from rob[])   */
    int     src1_ready; word_t src1_val; int src1_ptag;
    int     src2_ready; word_t src2_val; int src2_ptag;
    int     cycles_rem; /* Remaining execution cycles (counts down to 0)   */
    int     eu_type;    /* Execution unit type: EU_INT / EU_MUL / EU_LSU   */
} RSEntry;

/* ── Miss Status Holding Registers (MSHR) ────────────────────────────────── */
/* Tracks in-flight cache misses.  Multiple loads to the same cache line
 * share one MSHR entry and wait for a single fill operation. */
#define MSHR_SIZE 8

typedef struct {
    int     valid;
    paddr_t line_addr;   /* cache-line-aligned address: addr & ~63            */
    int     cycles_rem;  /* countdown to fill (set from LAT_L2_HIT/LAT_DRAM) */
} MSHREntry;

/* ── IS→EX and EX→MEM latches (carry phys_rd and rob_idx alongside IR) ──── */
typedef struct {
    int     rob_idx;   /* ROB slot; IR is read via ooo.rob[rob_idx].ir (no copy) */
    int     phys_rd;
    int     valid;
    int     cycles_rem; /* countdown for multi-cycle MEM stage (loads only) */
    int     mshr_idx;   /* index into ooo.mshr[], or -1 if not using MSHR   */
    int     flushed;    /* 1 = ROB was flushed but MSHR fill should complete */
    word_t  src1_val;   /* forwarded source values (overrides ROB ir.src*_val) */
    word_t  src2_val;
} OOOLatch;

/* ── Statistics ──────────────────────────────────────────────────────────── */
typedef struct {
    uint64_t cycles;
    uint64_t insts;                 /* Committed instruction count              */
    uint64_t rob_full_stalls;       /* Cycles stalled due to ROB full           */
    uint64_t rs_full_stalls;        /* Cycles stalled due to RS full            */
    uint64_t mispred_flushes;       /* Misprediction-induced pipeline flushes   */
    uint64_t serializing_stalls;    /* Cycles stalled waiting for ROB drain     */
    uint64_t eu_contention_stalls;  /* Cycles stalled due to execution unit full */
    uint64_t branch_penalty_cycles; /* Total ROB entries flushed on mispredicts */
} OOOStats;

/* ── Per-core store buffer (TSO model) ──────────────────────────────────── */
/* Committed stores are buffered here and drain to cache one per cycle.
 * Other cores cannot see the store until it drains (globally visible).
 * Same-core LOADs check this buffer first (STLF for committed stores).
 * FENCE stalls commit until the buffer is empty. */
#define STORE_BUF_SIZE 16

typedef struct {
    vaddr_t addr;
    word_t  data;
    int     width;
    int     valid;
} StoreBufEntry;

/* ── Top-level OOO engine state ──────────────────────────────────────────── */
typedef struct {
    /* Physical register file and renaming tables */
    PhysReg  prf[NUM_PHYS_REGS];
    int      rat[32];    /* Speculative RAT: rat[arch] = phys              */
    int      rrat[32];   /* Committed RAT (for misprediction recovery)     */
    FreeList freelist;

    /* ROB: circular buffer, rob_head = oldest, rob_tail = next free slot */
    ROBEntry rob[ROB_SIZE];
    int      rob_head;
    int      rob_tail;
    int      rob_count;

    /* Reservation stations */
    RSEntry  rs[RS_SIZE];
    int      unready_store_count; /* # of STOREs in ROB with ready=0; maintained
                                   * incrementally to avoid O(ROB) scan per LOAD */
    /* Per-EU-type bitmask of RS slots that are valid AND have both sources ready.
     * Bit i set ↔ rs[i].valid && src1_ready && src2_ready && eu_type == EU_x.
     * Maintained by CDB broadcast, issue, flush, and rename. Lets IS stage
     * skip invalid/not-ready entries without scanning all RS_SIZE slots. */
    uint32_t rs_ready_mask[3];  /* [EU_INT], [EU_MUL], [EU_LSU] */
    /* Bitmask of FREE RS slots (bit i set ↔ rs[i].valid == 0).
     * Maintained incrementally; replaces O(RS_SIZE) scan in RN and cdb_broadcast. */
    uint32_t rs_free_mask;

    /* Pipeline latches: front-end FETCH_WIDTH-wide, back-end per functional unit */
    PipeReg  latch_if_id[FETCH_WIDTH];          /* IF → ID                   */
    PipeReg  latch_id_rn[FETCH_WIDTH];          /* ID → RN                   */
    OOOLatch latch_int[NUM_INT_UNITS];           /* INT unit slots            */
    OOOLatch latch_mul[NUM_MUL_UNITS];           /* MUL/DIV unit slots        */
    OOOLatch latch_lsu[NUM_LSU_UNITS];           /* LSU unit slots            */

    /* Functional unit busy counters */
    int      eu_int_busy;   /* Number of INT units currently in use  */
    int      eu_mul_busy;   /* Number of MUL units currently in use  */
    int      eu_lsu_busy;   /* Number of LSU units currently in use  */

    /* Miss Status Holding Registers: track in-flight cache misses */
    MSHREntry mshr[MSHR_SIZE];

    vaddr_t  fetch_pc;      /* PC of the next instruction to fetch            */

    /* Per-core store buffer (TSO) */
    StoreBufEntry sbuf[STORE_BUF_SIZE];
    int           sbuf_head;
    int           sbuf_tail;
    int           sbuf_count;
} OOOState;

/* ── Globals ──────────────────────────────────────────────────────────────── */
extern OOOState ooo;
extern OOOStats ooo_stats;
extern int      g_ooo_mode;

/* ── API ──────────────────────────────────────────────────────────────────── */
void ooo_init(void);
void ooo_cycle(void);
void ooo_report(void);

#endif /* __CPU_OOO_H__ */
