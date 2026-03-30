/*
 * src/cpu/core.c — Per-core lifecycle (Phase 5: multi-core)
 *
 * core_create()  — allocate and initialise a Core slot
 * core_cycle()   — advance one clock for the given core
 * core_report()  — print per-core statistics
 *
 * core_cycle() works by:
 *  1. Loading this core's state into the global variables that deep callees
 *     (vmem, csr, ir_execute, …) depend on.
 *  2. Dispatching to the appropriate engine (functional/inorder/ooo).
 *  3. Saving the updated state back into the Core struct.
 */

#include <core.h>
#include <reg.h>
#include <csr.h>
#include <state.h>
#include <vmem.h>
#include <cache.h>
#include <pipeline.h>
#include <ooo.h>
#include <bpred.h>
#include <ir.h>
#include <exec.h>
#include <pmem.h>
#include <log.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* ── Global core array ──────────────────────────────────────────────────── */
Core cores[MAX_CORES];
int  g_num_cores = 1;
/* g_current_hartid is defined in csr.c (exported via csr.h) */

/* ── core_create ─────────────────────────────────────────────────────────── */

void core_create(int core_id, CoreMode mode, int bpred_enabled)
{
    Core *c = &cores[core_id];
    memset(c, 0, sizeof(*c));

    c->core_id       = core_id;
    c->mode          = mode;
    c->bpred_enabled = bpred_enabled;
    c->priv_level    = MACHINE;  /* start in machine mode */

    /* Copy initial architectural state from the already-loaded global
     * (init_regs() and load_img() have already set cpu/csr by this point). */
    c->cpu        = cpu;
    c->csr        = csr;
    c->priv_level = priv_level;
    memcpy(c->pmpcfg,  pmpcfg,  sizeof(pmpcfg));
    memcpy(c->pmpaddr, pmpaddr, sizeof(pmpaddr));

    /* Allocate per-core L1 caches; L2 is shared and already allocated. */
    c->l1i = init_cache(6, 8, "L1I");
    c->l1d = init_cache(L1D_S, 8, "L1D");

    if (bpred_enabled)
        bpred_init(&c->bpred);
}

/* ── exec_once_core ──────────────────────────────────────────────────────── */
/*
 * Functional (single-cycle) step for a core.  Mirrors exec_once() in exec.c
 * but reads/writes the Core struct rather than raw globals.
 * NOTE: The globals (cpu, csr, L1I_cache, L1D_cache, g_current_hartid) have
 * already been loaded by core_cycle() before this is called.
 */
static void exec_once_core(Core *c)
{
    /* Re-use the global exec_once via the public exec() call would run too
     * many instructions.  Instead duplicate the per-instruction logic here,
     * operating on the already-installed globals. */
    vaddr_t pc   = cpu.pc;
    vaddr_t snpc = pc + 4;

    pmp_check(pc, true, false);
    uint32_t raw = (uint32_t)vaddr_ifetch(pc, 4);

    IR_Inst ir;
    ir_decode(raw, pc, snpc, &ir);
    ir_execute(&ir, &cpu);
    ir_mem_access(&ir, NULL);
    ir_writeback(&ir, &cpu);

    cpu.pc = ir.dnpc;

    g_sim_cycles++;
    g_sim_instret++;
    c->sim_cycles++;
    c->sim_instret++;

    (void)c;  /* suppress unused-parameter warning if counters removed */
}

/* ── core_cycle ──────────────────────────────────────────────────────────── */

void core_cycle(Core *c)
{
    /* 1. Install this core's context into the legacy globals ─────────────── */
    g_current_hartid = c->core_id;
    cpu       = c->cpu;
    csr       = c->csr;
    priv_level = c->priv_level;
    memcpy(pmpcfg,  c->pmpcfg,  sizeof(pmpcfg));
    memcpy(pmpaddr, c->pmpaddr, sizeof(pmpaddr));

    /* Route vmem through this core's private L1 caches */
    L1I_cache = c->l1i;
    L1D_cache = c->l1d;
    /* L2_cache is shared: leave the global as-is */

    /* 2. Dispatch to execution engine ─────────────────────────────────────── */
    switch (c->mode) {
    case CORE_MODE_FUNCTIONAL:
        exec_once_core(c);
        break;
    case CORE_MODE_INORDER:
        pipeline_cycle_core(c);
        break;
    case CORE_MODE_OOO:
        ooo_cycle_core(c);
        break;
    }

    /* 3. Save updated architectural state back into the Core struct ───────── */
    c->cpu        = cpu;
    c->csr        = csr;
    c->priv_level = priv_level;
    memcpy(c->pmpcfg,  pmpcfg,  sizeof(pmpcfg));
    memcpy(c->pmpaddr, pmpaddr, sizeof(pmpaddr));
}

/* ── core_report ─────────────────────────────────────────────────────────── */

void core_report(Core *c)
{
    printf("\n=== Core %d Statistics ===\n", c->core_id);
    printf("Mode            : %s\n",
           c->mode == CORE_MODE_FUNCTIONAL ? "functional" :
           c->mode == CORE_MODE_INORDER    ? "in-order"   : "OOO");

    switch (c->mode) {
    case CORE_MODE_FUNCTIONAL:
        printf("Instructions    : %" PRIu64 "\n", c->sim_instret);
        printf("Cycles          : %" PRIu64 "\n", c->sim_cycles);
        break;
    case CORE_MODE_INORDER: {
        PipeStats *ps = &c->pipe_stats;
        double ipc = ps->cycles > 0
                     ? (double)ps->insts / (double)ps->cycles : 0.0;
        printf("Cycles          : %" PRIu64 "\n", ps->cycles);
        printf("Instructions    : %" PRIu64 "\n", ps->insts);
        printf("IPC             : %.3f\n", ipc);
        printf("Load-use stalls : %" PRIu64 "\n", ps->stall_load_use);
        printf("Control flushes : %" PRIu64 "\n", ps->stall_control);
        break;
    }
    case CORE_MODE_OOO: {
        OOOStats *os = &c->ooo_stats;
        double ipc = os->cycles > 0
                     ? (double)os->insts / (double)os->cycles : 0.0;
        printf("Cycles              : %" PRIu64 "\n", os->cycles);
        printf("Instructions        : %" PRIu64 "\n", os->insts);
        printf("IPC                 : %.3f\n", ipc);
        printf("ROB full stalls     : %" PRIu64 "\n", os->rob_full_stalls);
        printf("RS full stalls      : %" PRIu64 "\n", os->rs_full_stalls);
        printf("Mispred flushes     : %" PRIu64 "\n", os->mispred_flushes);
        printf("Serializing stalls  : %" PRIu64 "\n", os->serializing_stalls);
        break;
    }
    }

    if (c->bpred_enabled)
        bpred_report(&c->bpred);

    printf("=========================\n\n");
}
