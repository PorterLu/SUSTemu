#ifndef __CPU_PIPELINE_H__
#define __CPU_PIPELINE_H__

#include <common.h>
#include <ir.h>

/*
 * pipeline.h — in-order 5-stage pipeline data structures (Phase 2)
 *
 * Stages: IF → ID → EX → MEM → WB
 *
 * PipeReg holds the pipeline latch between two adjacent stages.
 * A bubble is represented by valid == 0; the ir.type field is
 * irrelevant when valid == 0.
 */

/* ── Per-latch register ─────────────────────────────────────────────────── */
typedef struct {
    IR_Inst ir;     /* Instruction snapshot held in this latch             */
    int     valid;  /* 0 = bubble / empty slot, 1 = live instruction       */
} PipeReg;

/* ── Whole-pipeline state ───────────────────────────────────────────────── */
typedef struct {
    PipeReg  id;             /* IF→ID latch  (instruction fetched, awaiting decode) */
    PipeReg  ex;             /* ID→EX latch  (decoded, awaiting execution)           */
    PipeReg  mem;            /* EX→MEM latch (executed, awaiting memory access)      */
    PipeReg  wb;             /* MEM→WB latch (memory done, awaiting writeback)        */
    vaddr_t  fetch_pc;       /* IF stage: address of the next instruction to fetch    */
    int      mem_stall_rem;  /* Remaining cache-miss stall cycles (0 = not stalling)  */
} Pipeline;

/* ── Statistics ─────────────────────────────────────────────────────────── */
typedef struct {
    uint64_t cycles;            /* Total clock cycles elapsed                     */
    uint64_t insts;             /* Retired (committed) instructions                */
    uint64_t stall_load_use;    /* Load-use hazard stalls (1 cycle each)           */
    uint64_t stall_control;     /* Control-hazard flushes (2 cycles each)          */
    uint64_t stall_cache_miss;  /* Cache-miss stall cycles (L2/DRAM latency)       */
} PipeStats;

/* ── Globals (defined in pipeline.c) ───────────────────────────────────── */
extern Pipeline  cpu_pipe;
extern PipeStats pipe_stats;

/* ── API ────────────────────────────────────────────────────────────────── */
void pipeline_init(void);    /* Reset pipeline to initial state before run      */
void pipeline_cycle(void);   /* Advance one clock cycle through all 5 stages    */
void pipeline_report(void);  /* Print IPC and hazard statistics to stdout        */

#endif /* __CPU_PIPELINE_H__ */
