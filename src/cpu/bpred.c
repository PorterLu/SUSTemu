/*
 * src/cpu/bpred.c — BTB + Tournament Branch Predictor (Phase 3)
 *
 * Index conventions:
 *   BTB  : btb_idx = (pc >> 2) & (BTB_SIZE - 1)   tag = pc >> 11
 *   LHT  : lht_idx = (pc >> 2) & (LHT_SIZE - 1)
 *   GPHT : gpht_idx = (ghr ^ (pc >> 2)) & (GPHT_SIZE - 1)   [gshare]
 *   Meta : same index as GPHT
 *
 * Saturating counter convention: 0=strongly NT, 1=weakly NT, 2=weakly T, 3=strongly T
 * Predict taken when counter >= 2.
 * Meta: >= 2 → prefer global predictor; < 2 → prefer local predictor.
 */

#include <bpred.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* ── Global predictor state ─────────────────────────────────────────────── */
BranchPredictor bpred;

/* ── bpred_init ──────────────────────────────────────────────────────────── */
void bpred_init(void)
{
    memset(&bpred, 0, sizeof(bpred));
    /* All 2-bit counters start at 0 (weakly not-taken / prefer local).
     * All BTB entries start invalid (valid == 0).
     * GHR starts at 0. */
}

/* ── bpred_predict ───────────────────────────────────────────────────────── */
BPredResult bpred_predict(vaddr_t pc)
{
    BPredResult r = {0, 0, 0};

    /* 1. BTB lookup ──────────────────────────────────────────────────────── */
    uint32_t bi = (pc >> 2) & (BTB_SIZE - 1);
    if (bpred.btb[bi].valid && bpred.btb[bi].tag == (pc >> 11)) {
        r.btb_hit = 1;
        r.target  = bpred.btb[bi].target;
    }

    /* 2. Local predictor ────────────────────────────────────────────────── */
    uint32_t hist      = bpred.lht[(pc >> 2) & (LHT_SIZE - 1)];
    int      local_pred = (bpred.lpht[hist] >= 2);

    /* 3. Global predictor (gshare) ──────────────────────────────────────── */
    uint32_t gi         = (bpred.ghr ^ (uint32_t)(pc >> 2)) & (GPHT_SIZE - 1);
    int      global_pred = (bpred.gpht[gi] >= 2);

    /* 4. Meta selector ──────────────────────────────────────────────────── */
    r.taken = (bpred.meta[gi] >= 2) ? global_pred : local_pred;

    return r;
}

/* ── bpred_update ────────────────────────────────────────────────────────── */
void bpred_update(IR_Inst *ir)
{
    /* Only track branch / jump instructions */
    if (ir->type != ITYPE_BRANCH && ir->type != ITYPE_JAL && ir->type != ITYPE_JALR)
        return;

    vaddr_t  pc     = ir->pc;
    int      actual = (ir->dnpc != ir->snpc);   /* 1 = taken, 0 = not-taken */

    bpred.predictions++;

    /* Count misprediction (direction or target wrong) */
    if (ir->dnpc != ir->bp_predicted_pc)
        bpred.mispredictions++;

    /* Update BTB (only on taken; store resolved target) ──────────────────── */
    uint32_t bi = (pc >> 2) & (BTB_SIZE - 1);
    if (actual) {
        if (!bpred.btb[bi].valid || bpred.btb[bi].tag != (pc >> 11))
            bpred.btb_misses++;
        bpred.btb[bi].valid  = 1;
        bpred.btb[bi].tag    = pc >> 11;
        bpred.btb[bi].target = ir->dnpc;
    }

    /* Local predictor update ────────────────────────────────────────────── */
    uint32_t li         = (pc >> 2) & (LHT_SIZE - 1);
    uint32_t hist       = bpred.lht[li];
    int      local_pred = (bpred.lpht[hist] >= 2);

    /* Update Local PHT saturating counter */
    if (actual  && bpred.lpht[hist] < 3) bpred.lpht[hist]++;
    if (!actual && bpred.lpht[hist] > 0) bpred.lpht[hist]--;

    /* Shift new outcome into Local History Table */
    bpred.lht[li] = ((hist << 1) | actual) & ((1 << LHT_BITS) - 1);

    /* Global predictor (gshare) update ──────────────────────────────────── */
    uint32_t gi          = (bpred.ghr ^ (uint32_t)(pc >> 2)) & (GPHT_SIZE - 1);
    int      global_pred = (bpred.gpht[gi] >= 2);

    if (actual  && bpred.gpht[gi] < 3) bpred.gpht[gi]++;
    if (!actual && bpred.gpht[gi] > 0) bpred.gpht[gi]--;

    /* Meta selector update (only when predictors disagree) ─────────────── */
    if (local_pred != global_pred) {
        if (actual == global_pred && bpred.meta[gi] < 3) bpred.meta[gi]++;
        if (actual == local_pred  && bpred.meta[gi] > 0) bpred.meta[gi]--;
    }

    /* Update Global History Register ────────────────────────────────────── */
    bpred.ghr = ((bpred.ghr << 1) | actual) & ((1 << GHR_BITS) - 1);
}

/* ── bpred_report ────────────────────────────────────────────────────────── */
void bpred_report(void)
{
    double acc = (bpred.predictions > 0)
                 ? 100.0 * (1.0 - (double)bpred.mispredictions
                                  / (double)bpred.predictions)
                 : 0.0;

    printf("=== Branch Prediction Statistics ===\n");
    printf("Predictions     : %" PRIu64 "\n", bpred.predictions);
    printf("Mispredictions  : %" PRIu64 "\n", bpred.mispredictions);
    printf("Accuracy        : %.2f%%\n",       acc);
    printf("BTB cold misses : %" PRIu64 "\n", bpred.btb_misses);
    printf("=====================================\n\n");
}
