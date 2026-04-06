#include <cpu/bpred2.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

BranchPredictor bpred2_state;
int g_bpred2_mode = 0;

static uint32_t spec_lht[LHT_SIZE];

BPredResult bpred2_predict(BranchPredictor *bp, vaddr_t pc) {
    BPredResult r = {0, 0, 0};

    uint32_t bi  = (pc >> 2) & (BTB_SIZE - 1);
    uint32_t tag = pc >> 11;
    r.btb_hit = bp->btb[bi].valid && (bp->btb[bi].tag == tag);
    r.target  = r.btb_hit ? bp->btb[bi].target : 0;

    uint32_t li        = (pc >> 2) & (LHT_SIZE - 1);
    uint32_t spec_hist = spec_lht[li];
    r.taken            = (bp->lpht[spec_hist & (LPHT_SIZE - 1)] >= 2);

    // TODO: speculatively update spec_lht[li] here (shift in predicted outcome)

    return r;
}

void bpred2_update(BranchPredictor *bp, IR_Inst *ir) {
    vaddr_t pc      = ir->pc;
    int     actual  = (ir->dnpc != ir->snpc);
    int     mispred = (ir->dnpc != ir->bp_predicted_pc);

    bp->predictions++;
    if (mispred) bp->mispredictions++;

    uint32_t bi  = (pc >> 2) & (BTB_SIZE - 1);
    uint32_t tag = pc >> 11;
    if (actual) {
        if (!bp->btb[bi].valid || bp->btb[bi].tag != tag) bp->btb_misses++;
        bp->btb[bi].valid  = 1;
        bp->btb[bi].tag    = tag;
        bp->btb[bi].target = ir->dnpc;
    }

    uint32_t li   = (pc >> 2) & (LHT_SIZE - 1);
    uint32_t hist = bp->lht[li];
    uint32_t lphi = hist & (LPHT_SIZE - 1);
    if (actual  && bp->lpht[lphi] < 3) bp->lpht[lphi]++;
    if (!actual && bp->lpht[lphi] > 0) bp->lpht[lphi]--;
    bp->lht[li] = ((hist << 1) | actual) & ((1 << LHT_BITS) - 1);

    // TODO: on misprediction, resync spec_lht[li] to committed lht[li]

    bp->ghr = ((bp->ghr << 1) | actual) & ((1 << GHR_BITS) - 1);
}

void bpred2_report(BranchPredictor *bp) {
    printf("=== Branch Prediction Statistics (bpred2) ===\n");
    printf("Predictions     : %" PRIu64 "\n", bp->predictions);
    printf("Mispredictions  : %" PRIu64 "\n", bp->mispredictions);
    if (bp->predictions > 0)
        printf("Accuracy        : %.2f%%\n",
               100.0 * (bp->predictions - bp->mispredictions) / bp->predictions);
    printf("BTB cold misses : %" PRIu64 "\n", bp->btb_misses);
    printf("=============================================\n");
}
