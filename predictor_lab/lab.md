<style>
    table {
        width: 100%;
    }
</style>

# Lab 5: Branch Predictor Lab

In this lab, you will use the SUSTemu RISC-V simulator to explore how branch prediction affects out-of-order processor performance. By the end of this lab, you will be able to:

1. Understand how branch bias affects misprediction rate and IPC
2. Identify BTB aliasing and explain how function alignment causes it
3. Measure the cold-start penalty of the gshare predictor and explain the effect of GHR width

**Default predictor configuration (`include/cpu/bpred.h`):**
- BTB: **512** entries · index = `PC[10:2]` · tag = `PC >> 11`
- Local predictor: LHT **1024** entries × **10-bit** history → LPHT **1024** 2-bit saturating counters
- Global predictor (gshare): GHR **12 bits** XOR PC → GPHT **4096** 2-bit saturating counters
- Meta selector: **4096** 2-bit saturating counters; ≥ 2 → prefer global, < 2 → prefer local

Each benchmark directory provides two (or three) make targets:

```bash
make run-nobpred   # OOO pipeline, always predict not-taken
make run-bpred     # OOO pipeline, tournament predictor enabled
make run-ooo       # OOO pipeline, tournament predictor enabled (Q3)
make disasm        # Generate annotated disassembly bench.dis
```

> **Note:** after modifying any file under `include/`, always run `make clean && make` from the repository root.

<div style="page-break-after: always;"></div>

# Step 1 — Branch Bias and Prediction Accuracy

## Environment Setup

**Question 1:** Clone the repository and build the simulator:

```bash
git clone git@github.com:Compass-All/SUSTemu.git
cd SUSTemu
make clean && make
```

Run `whoami` in the same terminal after a successful build.

**Submit:** a screenshot showing both the build output ending with `over` and the `whoami` result.

## Background

`predictor_lab/Q1/` contains two kernels that differ only in the fraction of taken branches:

- `branch_regular` — **50/50**: `if (arr[i] % 2 == 0) sum++; else sum--;` — taken and not-taken alternate strictly, so a 2-bit saturating counter oscillates and mispredicts every other branch
- `branch_biased` — **~12.5% taken**: `if (arr[i] % 8 == 0) sum++; else sum--;` — taken is rare, so the counter converges quickly to "strongly not-taken" and mispredicts infrequently

Both kernels iterate over `NELEM = 1024` elements for `PASSES = 20` passes. The working set (8 KB) fits in L1D, so all loads are L1 hits — performance differences are purely due to branch behavior.

Run both kernels in both prediction modes:

```bash
cd predictor_lab/Q1
make run-nobpred
make run-bpred
```

| | IPC (no predictor) | IPC (with predictor) |
|-|--------------------|----------------------|
| branch_regular (50/50) | | |
| branch_biased  (12.5% taken) | | |

**Question 2:** Based on the table above, explain why the IPC gap between `run-nobpred` and `run-bpred` is much larger for `branch_regular` than for `branch_biased`. Consider the misprediction rate of each kernel under each prediction mode, and explain what the 2-bit saturating counter does differently for the two patterns.

**Submit:** completed table and written answer to Q2.

<div style="page-break-after: always;"></div>

# Step 2 — BTB Aliasing

## Background

`predictor_lab/Q2/` contains two functions that both traverse arrays of the same size:

- `loop_A` — sums `aa[N]`; compiled with `__attribute__((noinline))`
- `loop_B` — sums `bb[N]`; compiled with `__attribute__((noinline, aligned(2048)))`

The BTB has **512** entries indexed by `PC[10:2]` (9 bits). When two branches share the same index bits but differ in tag (`PC >> 11`), they **alias**: each commit overwrites the other's BTB entry, causing a BTB miss every time the other branch executes next.

Because `loop_B` is aligned to a 2048-byte boundary and both back-edge branches sit at roughly the same offset within their respective functions, the difference between their PCs is a multiple of 2048 — making `PC[10:2]` identical for both.

The `main` function calls `loop_A` and `loop_B` alternately for `PASSES = 100` rounds, maximizing the aliasing effect.

Run the benchmark:

```bash
cd predictor_lab/Q2
make run-bpred
```

**Question 3:** Generate the disassembly and locate the back-edge branch of each loop:

```bash
make disasm   # writes bench.dis
```

Open `bench.dis`, find the `bnez`/`bne` instruction at the bottom of `loop_A` and `loop_B` respectively, and record their addresses. Compute the BTB index for each: `index = (PC >> 2) & 0x1FF`.

Fill in:
- `loop_A` back-edge PC = `0x________`, BTB index = `____`
- `loop_B` back-edge PC = `0x________`, BTB index = `____`
- Are the two indices the same?

Then explain: the BTB tag (`PC >> 11`) differs between the two branches, so using the wrong target is prevented — but what performance cost does this correctness guarantee impose?

**Submit:** filled-in addresses/indices and written answer to Q3.

**Question 4:** Remove the alignment attribute from `loop_B`. In `Q2/bench.c`, change:

```c
__attribute__((noinline, aligned(2048)))
static long loop_B(void) {
```

to:

```c
__attribute__((noinline))
static long loop_B(void) {
```

Recompile and run `make run-bpred`. Report the **BTB cold misses** count before and after the change, and explain in one sentence why removing the alignment eliminates the aliasing.

**Submit:** BTB cold miss counts (before and after) and one-sentence explanation.

<div style="page-break-after: always;"></div>

# Step 3 — Cold Start and GHR Width

## Background

`predictor_lab/Q3/` contains two experiments that target different aspects of the gshare predictor:

**Cold-start penalty:** All 2-bit saturating counters are initialized to 0 (strongly not-taken). For an always-taken branch, the counter must see two taken outcomes before it reaches "weakly taken" (state 2) and predicts correctly. Until then, every execution is a misprediction. In OOO mode, each misprediction triggers a full ROB flush (~16 cycles of wasted work).

The benchmark measures `probe_branches()` — 32 always-taken forward branches — twice: first **cold** (PHT slots all 0) and then **warm** (PHT slots trained to state 3 by 8 prior runs). Between each run, `reset_ghr()` flushes the 12-bit GHR with 16 always-not-taken branches, ensuring the same gshare index for both cold and warm measurements.

**GHR width:** `branch_periodic()` generates a taken/not-taken pattern with period `PERIOD = 512`. When `GHR_BITS = 12` (history length 4096 > 512), gshare can distinguish 512 distinct history contexts and learns the pattern. When `GHR_BITS = 8` (history length 256 < 512), the history wraps and different cycle positions collide in the same PHT slot, degrading accuracy.

Run the benchmark:

```bash
cd predictor_lab/Q3
make run-ooo
```

**Question 5 (warm-up observation):** Record the `cold` and `warm` cycle counts printed by the benchmark. Explain why the cold pass takes significantly more cycles than the warm pass. If the processor were in-order (misprediction penalty = 1 cycle instead of ~16), would the cold/warm gap grow or shrink? Why?

**Submit:** cold and warm cycle counts and written answer.

**Question 6 (GHR width):** Edit `include/cpu/bpred.h` and change:

```c
#define GHR_BITS   12
#define GPHT_SIZE  4096
```

to:

```c
#define GHR_BITS   8
#define GPHT_SIZE  256
```

From the repository root, run `make clean && make`, then re-enter `predictor_lab/Q3` and run `make run-ooo`. Record the **Mispredictions** and **Accuracy** values printed by the simulator for both configurations:

| GHR_BITS | Mispredictions | Accuracy |
|----------|----------------|----------|
| 12 (default) | | |
| 8 | | |

Explain why reducing `GHR_BITS` from 12 to 8 degrades accuracy for the `PERIOD = 512` workload.

**Restore `bpred.h` to the original values and run `make clean && make` before submitting.**

**Submit:** completed table and written explanation.
