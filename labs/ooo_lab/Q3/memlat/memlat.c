/* Q3/memlat/memlat.c — 内存延迟隐藏实验（Memory-Level Parallelism）
 *
 * 对比两种访存模式：
 *
 *   bench_parallel — 每次迭代从两个独立数组各加载一个元素：
 *       sumA += arrA[i]
 *       sumB += arrB[i]
 *
 *   bench_serial — 每次迭代只加载一个元素：
 *       sum += arr[i]
 *
 * 数组大小超过 L1D，落在 L2，保证稳定的 L2 访问延迟。
 * 两种模式总访存量相同，便于公平对比。
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

/* ── 实验参数 ─────────────────────────────────────────────────────────────── */
#define N_HALF 2560   /* arrA, arrB 各 2560 元素 = 20KB */
#define N_FULL 5120   /* arr 5120 元素 = 40KB           */

static volatile long arrA[N_HALF];
static volatile long arrB[N_HALF];
static volatile long arr[N_FULL];

/* ── bench_parallel ─────────────────────────────────────────────────────── */
static void bench_parallel(long *cyc_out, long *inst_out) {
    long sumA = 0, sumB = 0;
    long c0 = rdcycle();
    long r0 = rdinstret();

    for (int i = 0; i < N_HALF; i++) {
        sumA += arrA[i];
        sumB += arrB[i];
    }

    *cyc_out  = rdcycle()   - c0;
    *inst_out = rdinstret() - r0;
    __asm__ volatile("" : : "r"(sumA), "r"(sumB));
}

/* ── bench_serial ───────────────────────────────────────────────────────── */
static void bench_serial(long *cyc_out, long *inst_out) {
    long sum = 0;
    long c0 = rdcycle();
    long r0 = rdinstret();

    for (int i = 0; i < N_FULL; i++) {
        sum += arr[i];
    }

    *cyc_out  = rdcycle()   - c0;
    *inst_out = rdinstret() - r0;
    __asm__ volatile("" : : "r"(sum));
}

static void report(const char *name, long cyc, long inst) {
    uart_puts(name);
    uart_puts(": cycles=");
    uart_putlong(cyc);
    uart_puts("  insts=");
    uart_putlong(inst);
    uart_puts("  CPI×100=");
    uart_putlong(cyc * 100 / inst);
    uart_putchar('\n');
}

int main(void) {
    for (int i = 0; i < N_HALF; i++) { arrA[i] = (long)i + 1; arrB[i] = (long)i + 2; }
    for (int i = 0; i < N_FULL;  i++) arr[i]  = (long)i + 1;

    uart_puts("=== Q3: 内存延迟隐藏实验 ===\n");
    uart_puts("N_HALF="); uart_putlong(N_HALF);
    uart_puts("  N_FULL="); uart_putlong(N_FULL);
    uart_puts("\n");

    long cyc, inst;

    /* 预热：将数组驱出 L1，确保测量时为 L2 hit */
    bench_serial(&cyc, &inst);
    bench_parallel(&cyc, &inst);

    bench_parallel(&cyc, &inst);
    report("bench_parallel", cyc, inst);

    bench_serial(&cyc, &inst);
    report("bench_serial  ", cyc, inst);

    uart_puts("done\n");
    return 0;
}
