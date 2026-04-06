/* predictor_lab/Q2/alias/bench.c
 *
 * Q2: BTB 别名（Aliasing）与消除 — Question 3 & 4
 * ─────────────────────────────────────────────────────────────────────────
 *
 * BTB 结构：512 项，index = PC[10:2]（低 9 位），tag = PC >> 11。
 * 当两个 branch 的 PC 满足 (pcA ^ pcB) & 0x7FC == 0 时（即 bit[10:2] 完全
 * 相同），它们映射到同一个 BTB slot——每次其中一个被提交，就会覆盖另一个
 * 留下的 target，导致对方下次 BTB miss（tag 不匹配 → btb_misses++）。
 *
 * ── Question 3 ───────────────────────────────────────────────────────────
 *
 * 运行：make run-bpred
 * 观察 "BTB cold misses" 和 "Mispredictions" 数量。
 *
 * 回答：
 *   1. make disasm 后找 loop_A 和 loop_B 的 back-edge branch 地址，
 *      分别计算 BTB index = (pc >> 2) & 511。它们相同吗？
 *   2. BTB tag（pc >> 11）不同能防止使用错误的预测 target——
 *      但这等价于每次都 BTB miss。tag 保证了正确性，代价是什么？
 *
 * ── Question 4 ───────────────────────────────────────────────────────────
 *
 * 取消 loop_B 的 aligned(2048) 属性（改为普通 __attribute__((noinline))），
 * 然后 make run-bpred，观察 BTB cold misses 是否下降。
 * 解释：去掉对齐后 index 为何不同，aliasing 如何消失。
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
#define N      512
#define PASSES 100

static long aa[N], bb[N];

/* loop_A：对 aa[] 求和（单次遍历） */
__attribute__((noinline))
static long loop_A(void) {
    long sum = 0;
    for (int i = 0; i < N; i++)
        sum += aa[i];
    return sum;
}

/* loop_B：对 bb[] 求和（单次遍历）
 *
 * aligned(2048) 使 loop_B 起始地址是 2048 的整数倍。
 * loop_A 的 back-edge branch 偏移约 +0x20，loop_B 同样约 +0x20，
 * 两者 PC 之差恰好是 2048 的整数倍 → BTB index 完全相同 → aliasing。
 *
 * Question 4：将下面的 aligned(2048) 去掉（只留 noinline），
 * 重新编译后 loop_B 紧接 loop_A，index 不同，aliasing 消失。
 */
__attribute__((noinline, aligned(2048)))
static long loop_B(void) {
    long sum = 0;
    for (int i = 0; i < N; i++)
        sum += bb[i];
    return sum;
}

int main(void) {
    for (int i = 0; i < N; i++) { aa[i] = i + 1; bb[i] = i + 2; }

    uart_puts("=== Q2: BTB Aliasing ===\n");
    uart_puts("(make disasm and check loop_A / loop_B back-edge PC)\n");

    long c0, c1, sa = 0, sb = 0;

    /* 交错调用：每轮先 loop_A 再 loop_B，两个 back-edge 交替覆盖同一 BTB slot */
    c0 = rdcycle();
    for (int r = 0; r < PASSES; r++) {
        sa += loop_A();
        sb += loop_B();
    }
    c1 = rdcycle();
    uart_puts("interleaved: cycles="); uart_putlong(c1 - c0);
    uart_puts("  sa="); uart_putlong(sa);
    uart_puts("  sb="); uart_putlong(sb);
    uart_puts("\n");

    uart_puts("done\n");
    return 0;
}
