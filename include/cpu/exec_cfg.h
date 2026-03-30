/*
 * include/cpu/exec_cfg.h — Execution unit and timing configuration
 *
 * Modify ISSUE_WIDTH to control the number of instructions issued per cycle.
 * All latency values are in pipeline cycles.
 */

#ifndef __CPU_EXEC_CFG_H__
#define __CPU_EXEC_CFG_H__

/* ── Issue width (1 = single-issue, 2 = dual-issue, 4 = four-issue) ──────── */
#define ISSUE_WIDTH     2

/* ── Fetch width (instructions fetched/decoded/renamed per cycle) ────────── */
#define FETCH_WIDTH     2   /* Must be >= 1, typically same as ISSUE_WIDTH */

/* ── Commit width (instructions retired per cycle) ───────────────────────── */
#define COMMIT_WIDTH    2

/* ── Functional unit counts ──────────────────────────────────────────────── */
#define NUM_INT_UNITS   2   /* Integer ALU units */
#define NUM_MUL_UNITS   1   /* Multiply/divide unit */
#define NUM_LSU_UNITS   2   /* Load/store units (2 for MLP demo) */

/* ── Instruction execution latencies (cycles) ────────────────────────────── */
#define LAT_INT_ADD     1   /* add, sub, and, or, xor, shifts, compare, lui, auipc */
#define LAT_INT_MUL     3   /* mul, mulh, mulhsu, mulhu */
#define LAT_INT_DIV    20   /* div, divu, rem, remu */

/* ── Cache hit latencies (cycles, used by OOO MEM stage countdown) ────────── */
#define LAT_L1_HIT      1    /* L1 hit: no added stall (fast path)      */
#define LAT_L2_HIT      8    /* L2 hit: 8 cycles (7 extra stalls) */
#define LAT_DRAM       40    /* DRAM miss: 40 cycles total              */

/* ── Execution unit type identifiers ──────────────────────────────────────── */
#define EU_INT          0
#define EU_MUL          1
#define EU_LSU          2

#endif /* __CPU_EXEC_CFG_H__ */
