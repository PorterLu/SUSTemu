/* Q2/tomasulo/tomasulo.c — Tomasulo 调度实验
 *
 * 对比两种不同依赖结构的计算序列：
 *
 *   bench_diamond — 菱形依赖：
 *       a = load(arr[i])
 *       b = a + 1
 *       c = a * a
 *       store = b + c
 *
 *   bench_chain — 串行链：
 *       a = load(arr[i])
 *       b = a + 1
 *       c = b * b
 *       store = b + c
 *
 * 数组大小：N × 8B，完全在 L1D 内，排除 cache miss 干扰。
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
#define N      256
#define PASSES 5

static long arr[N];
static long out_diamond[N];
static long out_chain[N];

/* ── bench_diamond ──────────────────────────────────────────────────────── */
static void bench_diamond(void) {
    for (int p = 0; p < PASSES; p++) {
        for (int i = 0; i < N; i++) {
            long a = arr[i];
            long b = a + 1;
            long c = a * a;
            long d = b + c;
            out_diamond[i] = d;
        }
    }
}

/* ── bench_chain ────────────────────────────────────────────────────────── */
static void bench_chain(void) {
    for (int p = 0; p < PASSES; p++) {
        for (int i = 0; i < N; i++) {
            long a = arr[i];
            long b = a + 1;
            long c = b * b;
            long d = b + c;
            out_chain[i] = d;
        }
    }
}

static void measure(const char *name, void (*fn)(void), long *out_arr) {
    fn();  /* warm-up */

    long c0 = rdcycle();
    long r0 = rdinstret();
    fn();
    long c1 = rdcycle();
    long r1 = rdinstret();

    long cycles = c1 - c0;
    long insts  = r1 - r0;

    long checksum = 0;
    for (int i = 0; i < N; i++) checksum += out_arr[i];

    uart_puts(name);
    uart_puts(": cycles=");
    uart_putlong(cycles);
    uart_puts("  insts=");
    uart_putlong(insts);
    uart_puts("  IPC×100=");
    uart_putlong(insts * 100 / cycles);
    uart_puts("  (checksum=");
    uart_putlong(checksum);
    uart_puts(")\n");
}

int main(void) {
    for (int i = 0; i < N; i++) arr[i] = i + 1;

    uart_puts("=== Q2: Tomasulo Scheduling ===\n");
    uart_puts("N=");  uart_putlong(N);
    uart_puts("  PASSES="); uart_putlong(PASSES);
    uart_puts("\n\n");

    measure("bench_diamond", bench_diamond, out_diamond);
    measure("bench_chain  ", bench_chain, out_chain);

    uart_puts("\ndone\n");
    return 0;
}
