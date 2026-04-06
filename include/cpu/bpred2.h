/*
 * include/cpu/bpred2.h — 2-bit history local predictor
 *
 * Reuses the same BranchPredictor struct as bpred.
 * Key difference: uses only 2 LSBs of per-PC LHT history to index the
 * GPHT array repurposed as a 2-bit-history PHT:
 *
 *   gphi = (hist2 << 10) | (pc>>2 & 0x3FF)
 *
 * With only 4 history states per branch (vs 1024 for 10-bit),
 * each PHT slot is visited ~256x more often, saturating in ~3 visits
 * instead of ~14.  On Q5 bench this roughly halves total mispredictions.
 * lpht[], meta[], ghr are unused for prediction.
 */

#ifndef __CPU_BPRED2_H__
#define __CPU_BPRED2_H__

#include <common.h>
#include <ir.h>
#include <cpu/bpred.h>   /* reuse BranchPredictor struct unchanged */

/* bpred2 uses the identical BranchPredictor struct; meta[] is simply unused */
extern BranchPredictor bpred2_state;
extern int g_bpred2_mode;

BPredResult bpred2_predict(BranchPredictor *bp, vaddr_t pc);
void        bpred2_update (BranchPredictor *bp, IR_Inst *ir);
void        bpred2_report (BranchPredictor *bp);

#endif /* __CPU_BPRED2_H__ */
