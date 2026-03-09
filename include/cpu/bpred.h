/*
 * include/cpu/bpred.h — BTB + Tournament Branch Predictor (Phase 3)
 *
 * Structures:
 *   BranchPredictor  — top-level predictor state (BTB + local + global + meta)
 *   BPredResult      — output of bpred_predict()
 *
 * Tournament design:
 *   Local predictor  : per-PC 10-bit history → Local PHT (2-bit saturating)
 *   Global predictor : 12-bit GHR xor PC → Global PHT (2-bit saturating / gshare)
 *   Meta selector    : 2-bit saturating counter per global PHT index;
 *                      >= 2 → choose global, < 2 → choose local
 *   BTB              : 512 entries, indexed by PC[10:2], tagged by PC[high]
 */

#ifndef __CPU_BPRED_H__
#define __CPU_BPRED_H__

#include <common.h>
#include <ir.h>

/* ── BTB parameters ────────────────────────────────────────────────────── */
#define BTB_SIZE   512          /* 9-bit index = PC[10:2] */

typedef struct {
    vaddr_t tag;        /* High bits of PC (pc >> 11) for aliasing detection */
    vaddr_t target;     /* Last observed taken-target address                */
    int     valid;      /* 1 = entry holds a valid prediction                */
} BTBEntry;

/* ── Tournament predictor parameters ───────────────────────────────────── */
#define LHT_SIZE   1024         /* Local History Table entries, index PC[11:2] */
#define LHT_BITS   10           /* Bits of local history per PC                */
#define LPHT_SIZE  1024         /* Local PHT = 1 << LHT_BITS                   */
#define GHR_BITS   12           /* Global History Register width               */
#define GPHT_SIZE  4096         /* Global PHT = 1 << GHR_BITS                  */
#define META_SIZE  4096         /* Choice table, same index as Global PHT       */

/* ── Main predictor state ───────────────────────────────────────────────── */
typedef struct {
    /* Branch Target Buffer */
    BTBEntry btb[BTB_SIZE];

    /* Local predictor */
    uint32_t lht[LHT_SIZE];    /* Per-PC history shift register (LHT_BITS wide) */
    uint8_t  lpht[LPHT_SIZE];  /* Local PHT: 2-bit saturating counters           */

    /* Global predictor (gshare) */
    uint32_t ghr;              /* Global History Register                        */
    uint8_t  gpht[GPHT_SIZE];  /* Global PHT: 2-bit saturating counters          */

    /* Meta selector */
    uint8_t  meta[META_SIZE];  /* 2-bit sat. counter; >= 2 → prefer global       */

    /* Statistics */
    uint64_t predictions;      /* Total predicted branches (BRANCH/JAL/JALR)     */
    uint64_t mispredictions;   /* Predictions where dnpc != bp_predicted_pc      */
    uint64_t btb_misses;       /* BTB cold misses (taken but no valid BTB entry)  */
} BranchPredictor;

/* ── Prediction result returned by bpred_predict() ─────────────────────── */
typedef struct {
    int     taken;     /* Predicted direction: 1=taken, 0=not-taken  */
    vaddr_t target;    /* Predicted target (only valid when btb_hit)  */
    int     btb_hit;   /* 1 = BTB had a valid matching entry          */
} BPredResult;

/* ── Globals ────────────────────────────────────────────────────────────── */
extern BranchPredictor bpred;   /* single-core legacy instance */
extern int g_bpred_mode;        /* set to 1 by --bpred command-line flag */

/* ── API (all functions take explicit BranchPredictor pointer) ──────────── */
void        bpred_init   (BranchPredictor *bp);
BPredResult bpred_predict(BranchPredictor *bp, vaddr_t pc);
void        bpred_update (BranchPredictor *bp, IR_Inst *ir);
void        bpred_report (BranchPredictor *bp);

#endif /* __CPU_BPRED_H__ */
