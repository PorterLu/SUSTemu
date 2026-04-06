/* predictor_lab/Q3/bench.c
 *
 * Q3: 预测器冷启动 & GHR 宽度的影响 — Question 5
 * ─────────────────────────────────────────────────────────────────────────
 *
 * ── Question 5（前置观察）：冷启动 vs 稳态 ───────────────────────────
 *
 * 所有 2-bit 饱和计数器初始值为 0（strongly not-taken）。
 * 对于高度 taken 的分支，冷启动时计数器为 0 → 预测 not-taken → mispred。
 * 之后计数器爬升到 2/3，稳定预测正确。
 * OOO 模式下每次 mispred 触发 ROB flush（~16 周期），差异非常明显。
 *
 * 任务：
 *   make run-ooo（OOO + bpred）
 *   观察输出的 cold/warm cycles 差异。
 *   解释：为什么 cold pass 的 cycles 显著多于 warm pass？
 *         如果改用 in-order（mispred penalty=1 cycle），差异会变大还是变小？
 *
 * ── Question 5（主实验）：GHR 宽度与长周期分支模式 ───────────────────
 *
 * gshare 的全局历史寄存器宽度为 GHR_BITS（默认 12）。
 * 若分支模式的周期长度 > 2^GHR_BITS，gshare 无法区分不同历史状态，
 * 会退化为与局部预测器相当甚至更差的随机行为。
 *
 * 本文件中的 branch_periodic() 函数产生周期为 PERIOD 的分支序列：
 *   当 (pass * N + i) % PERIOD < PERIOD/2 时 taken，否则 not-taken。
 *
 * 任务：
 *   1. 用默认 GHR_BITS=12 运行，记录 Accuracy。
 *   2. 修改 include/cpu/bpred.h，将 GHR_BITS 从 12 改为 8，
 *      同时将 GPHT_SIZE 从 4096 改为 256（= 1 << 8），
 *      然后在仓库根目录执行 make clean && make，再 make run-ooo。
 *   3. 对比两次的 Mispredictions 和 Accuracy，解释变化原因。
 *      （改完后记得把 bpred.h 还原，避免影响其他 lab）
 */

#include "csr_perf.h"

#define UART_THR (*(volatile char *)0xa00003f8)
static void uart_putchar(char c) { UART_THR = c; }
static void uart_puts(const char *s) { while (*s) uart_putchar(*s++); }
static void uart_putlong(long v) {
    if (v < 0) { uart_putchar('-'); v = -v; }
    if (v >= 10) uart_putlong(v / 10);
    uart_putchar('0' + (int)(v % 10));
}

/* ── 实验参数 ──────────────────────────────────────────────────────────── */
#define N       512    /* Q3b：每次遍历的迭代次数 */
#define PERIOD  512    /* Q3b：分支模式周期（> 2^GHR_BITS=8=256 时 gshare 退化） */

/* ── Q3a 宏 ───────────────────────────────────────────────────────────── */

/* 一个 always-taken 前向分支：bnez _t, 1f；_t=1 所以总是跳转 */
#define ALWAYS_TAKEN(sum)  do {                                  \
    int _t = 1;                                                  \
    asm volatile("bnez %1, 1f\n\t"                              \
                 "addi %0, %0, 0\n\t"                           \
                 "1:\n\t"                                        \
                 "addi %0, %0, 1\n"                             \
                 : "+r"(sum) : "r"(_t));                        \
} while (0)

/* 一个 always-not-taken 分支：bnez _z, 1f；_z=0 所以从不跳转
 * 用于向 GHR 移入 0，执行 16 次后 12-bit GHR 全部归零 */
#define ALWAYS_NT  do {                                          \
    int _z = 0;                                                  \
    asm volatile("bnez %0, 1f\n\t"                              \
                 "1:\n\t"                                        \
                 : : "r"(_z));                                   \
} while (0)

/* reset_ghr：执行 16 次 always-NT 分支，把 12-bit GHR 清零 */
__attribute__((noinline))
static void reset_ghr(void) {
    ALWAYS_NT; ALWAYS_NT; ALWAYS_NT; ALWAYS_NT;
    ALWAYS_NT; ALWAYS_NT; ALWAYS_NT; ALWAYS_NT;
    ALWAYS_NT; ALWAYS_NT; ALWAYS_NT; ALWAYS_NT;
    ALWAYS_NT; ALWAYS_NT; ALWAYS_NT; ALWAYS_NT;
}

/* probe_branches：32 个 always-taken 分支，每条在独立 PC
 * cold：PHT 槽全为 0（SNT）→ 全部 mispred（每次 ~16 cycle ROB flush）
 * warm：PHT 槽升至 3（ST）→ 全部正确预测，零额外开销 */
__attribute__((noinline))
static long probe_branches(void) {
    long sum = 0;
    ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum);
    ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum);
    ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum);
    ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum);
    ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum);
    ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum);
    ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum);
    ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum); ALWAYS_TAKEN(sum);
    return sum;
}

/* ── Q3a：测量冷启动 vs 稳态 ─────────────────────────────────────────── */
static void measure_warmup(void) {
    long c0, c1;
    volatile long result;

    uart_puts("--- Question 5 (warm-up observation): cold vs warm ---\n");

    /* cold：reset GHR=0 → PHT 槽全 0 → 32 次 mispred */
    reset_ghr();
    c0 = rdcycle();
    result = probe_branches();
    c1 = rdcycle();
    uart_puts("cold: cycles="); uart_putlong(c1 - c0);
    uart_puts("  result="); uart_putlong(result);
    uart_puts("\n");

    /* 预热 8 次：每次 reset GHR 后执行，训练同一批 PHT 槽到 ST(3) */
    for (int w = 0; w < 8; w++) {
        reset_ghr();
        result = probe_branches();
    }

    /* warm：reset GHR=0（与 cold 相同起点）→ PHT 槽已为 3 → 0 mispred */
    reset_ghr();
    c0 = rdcycle();
    result = probe_branches();
    c1 = rdcycle();
    uart_puts("warm: cycles="); uart_putlong(c1 - c0);
    uart_puts("  result="); uart_putlong(result);
    uart_puts("\n");
}

/* ── Q3b：周期性分支模式 ──────────────────────────────────────────────── */
static long branch_periodic(void) {
    long sum = 0;
    for (int pass = 0; pass < 64; pass++) {
        for (int i = 0; i < N; i++) {
            long idx = (long)pass * N + i;
            if (idx % PERIOD < PERIOD / 2)
                sum++;
            else
                sum--;
        }
    }
    return sum;
}

int main(void) {
    uart_puts("=== Q3: Warmup & GHR Width ===\n");

    measure_warmup();

    uart_puts("--- Question 5 (GHR width): periodic branch (PERIOD=");
    uart_putlong(PERIOD);
    uart_puts(") ---\n");

    long c0 = rdcycle();
    long result = branch_periodic();
    long c1 = rdcycle();
    uart_puts("cycles="); uart_putlong(c1 - c0);
    uart_puts("  result="); uart_putlong(result);
    uart_puts("\n");

    uart_puts("done\n");
    return 0;
}
