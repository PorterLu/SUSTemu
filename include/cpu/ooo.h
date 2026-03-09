/*
 * include/cpu/ooo.h — Tomasulo OOO Engine (Phase 4)
 *
 * 7-stage out-of-order pipeline:
 *   IF → ID → RN → IS → EX → MEM → ROB_Commit
 *
 * Explicit register renaming via RAT + physical register file (PRF).
 * Reservation stations (RS) for out-of-order issue.
 * Re-Order Buffer (ROB) for in-order commit.
 * Single-issue, single CDB per cycle.
 */

#ifndef __CPU_OOO_H__
#define __CPU_OOO_H__

#include <common.h>
#include <ir.h>
#include <pipeline.h>   /* PipeReg */
#include <stdint.h>

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
    IR_Inst ir;
    int     phys_rd;    /* Target physical register (-1 = none)             */
    int     rob_idx;    /* Corresponding ROB slot                           */
    int     src1_ready; word_t src1_val; int src1_ptag;
    int     src2_ready; word_t src2_val; int src2_ptag;
} RSEntry;

/* ── IS→EX and EX→MEM latches (carry phys_rd and rob_idx alongside IR) ──── */
typedef struct {
    IR_Inst ir;
    int     phys_rd;
    int     rob_idx;
    int     valid;
} OOOLatch;

/* ── Statistics ──────────────────────────────────────────────────────────── */
typedef struct {
    uint64_t cycles;
    uint64_t insts;              /* Committed instruction count             */
    uint64_t rob_full_stalls;    /* Cycles stalled due to ROB full          */
    uint64_t rs_full_stalls;     /* Cycles stalled due to RS full           */
    uint64_t mispred_flushes;    /* Misprediction-induced pipeline flushes  */
    uint64_t serializing_stalls; /* Cycles stalled waiting for ROB drain    */
} OOOStats;

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

    /* Pipeline latches */
    PipeReg  latch_if_id;   /* IF → ID   (instruction fetched, to be decoded) */
    PipeReg  latch_id_rn;   /* ID → RN   (decoded, to be renamed)             */
    OOOLatch latch_is_ex;   /* IS → EX   (issued, to be executed)             */
    OOOLatch latch_ex_mem;  /* EX → MEM  (executed, awaiting mem access)      */

    vaddr_t  fetch_pc;      /* PC of the next instruction to fetch            */
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
