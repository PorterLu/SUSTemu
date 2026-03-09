/*
 * include/cpu/core.h — Per-core abstraction (Phase 5: multi-core)
 *
 * Each Core object encapsulates all per-hart state:
 *   - Architectural registers (CPU_state) and CSRs
 *   - Private L1 I/D caches (shared L2 lives in cache.c)
 *   - In-order pipeline OR OOO engine state (only one active per core)
 *   - Branch predictor
 *
 * The exec() main loop iterates over cores[], calling core_cycle() for each.
 * core_cycle() loads this core's context into the globals that callees
 * (vmem, csr, ir_execute, …) depend on, dispatches to the appropriate
 * engine, then saves the updated state back.
 */

#ifndef __CPU_CORE_H__
#define __CPU_CORE_H__

#include <common.h>
#include <reg.h>
#include <csr.h>
#include <pipeline.h>
#include <ooo.h>
#include <bpred.h>
#include <cache.h>

#define MAX_CORES 2

typedef enum {
    CORE_MODE_FUNCTIONAL = 0,
    CORE_MODE_INORDER,
    CORE_MODE_OOO,
} CoreMode;

typedef struct {
    int      core_id;
    CoreMode mode;

    /* ── Per-core architectural state ─────────────────────── */
    CPU_state cpu;
    CSR       csr;
    uint64_t  priv_level;
    uint8_t   pmpcfg[PMP_NUMBER_IN_SUSTEMU];
    word_t    pmpaddr[PMP_NUMBER_IN_SUSTEMU];
    uint8_t   pmp_on_count;

    /* ── Per-core performance counters ────────────────────── */
    uint64_t  sim_cycles;
    uint64_t  sim_instret;

    /* ── Per-core caches (share L2) ───────────────────────── */
    Cache    *l1i;
    Cache    *l1d;

    /* ── Per-core in-order pipeline state ─────────────────── */
    Pipeline  pipe;
    PipeStats pipe_stats;

    /* ── Per-core out-of-order engine state ───────────────── */
    OOOState  ooo;
    OOOStats  ooo_stats;

    /* ── Per-core branch predictor ────────────────────────── */
    BranchPredictor bpred;
    int             bpred_enabled;
} Core;

/* ── Global array of cores ──────────────────────────────────────────────── */
extern Core cores[MAX_CORES];
extern int  g_num_cores;       /* 1 or 2 */
extern int  g_current_hartid;  /* index of the currently-executing core */

/* ── API ────────────────────────────────────────────────────────────────── */
void core_create(int core_id, CoreMode mode, int bpred_enabled);
void core_cycle (Core *c);
void core_report(Core *c);

/* ── Per-core engine entry points (defined in pipeline.c / ooo.c) ─────── */
void pipeline_init_core (Core *c);
void pipeline_cycle_core(Core *c);

void ooo_init_core (Core *c);
void ooo_cycle_core(Core *c);

#endif /* __CPU_CORE_H__ */
