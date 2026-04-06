/* predictor_lab/Q1/bimodal/bench.c
 *
 * Q1: Bimodal 分支模式与预测器准确率
 * ─────────────────────────────────────────────────────────────────────────
 *
 * 背景：2-bit 饱和计数器对"强偏向"分支（如 95% taken）能稳定预测，
 * 对 50/50 交替分支则每次都预测错。
 *
 * 本实验给出两个函数：
 *   branch_regular  —— 已实现，每次 taken/not-taken 严格交替（50/50）
 *   branch_biased   —— 【TODO】强偏向 not-taken（约 12.5% taken）
 *
 * ── Question 1 & 2 ───────────────────────────────────────────────────────
 *
 * (a) 实现 branch_biased：对每个数组元素，当 arr[i] % 8 == 0 时执行
 *     一条"taken"路径（sum++），否则走 not-taken 路径（sum--）。
 *     函数签名与 branch_regular 相同，返回 sum。
 *
 * (b) 分别用以下两种方式运行：
 *       make run-nobpred   （无预测器，所有分支默认 not-taken）
 *       make run-bpred     （开启 tournament 预测器）
 *     观察两种模式下 "Control flushes" 数量的差异，
 *     以及 branch_regular 与 branch_biased 的周期数差异。
 *
 * (c) 解释：为什么对 branch_biased 而言，有/无预测器的周期数相近，
 *     而对 branch_regular 差异更大？
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
#define NELEM  1024
#define PASSES 20

static long arr[NELEM];

/* ── branch_regular：严格交替（50% taken） ───────────────────────────── */
static long branch_regular(void) {
    long sum = 0;
    for (int p = 0; p < PASSES; p++) {
        for (int i = 0; i < NELEM; i++) {
            if (arr[i] % 2 == 0)   /* taken 50%，not-taken 50% */
                sum++;
            else
                sum--;
        }
    }
    return sum;
}

/* ── branch_biased：强偏向 not-taken（约 12.5% taken） ──────────────── */
static long branch_biased(void) {
    long sum = 0;
    for (int p = 0; p < PASSES; p++) {
        for (int i = 0; i < NELEM; i++) {
            if (arr[i] % 8 == 0)   /* taken ~12.5%，not-taken ~87.5% */
                sum++;
            else
                sum--;
        }
    }
    return sum;
}

/* ── 测量辅助 ─────────────────────────────────────────────────────────── */
static void measure(const char *name, long (*fn)(void)) {
    long c0 = rdcycle();
    long r0 = rdinstret();
    long result = fn();
    long c1 = rdcycle();
    long r1 = rdinstret();

    long cycles = c1 - c0;
    long insts  = r1 - r0;

    uart_puts(name);
    uart_puts(": cycles=");    uart_putlong(cycles);
    uart_puts("  insts=");     uart_putlong(insts);
    uart_puts("  IPC*100=");   uart_putlong(insts * 100 / cycles);
    uart_puts("  result=");    uart_putlong(result);
    uart_puts("\n");
}

int main(void) {
    for (int i = 0; i < NELEM; i++)
        arr[i] = i;

    uart_puts("=== Q1: Bimodal Branch Predictor ===\n");
    uart_puts("NELEM="); uart_putlong(NELEM);
    uart_puts("  PASSES="); uart_putlong(PASSES);
    uart_puts("\n");

    measure("branch_regular (50/50)  ", branch_regular);
    measure("branch_biased  (12.5%T) ", branch_biased);

    uart_puts("done\n");
    return 0;
}
