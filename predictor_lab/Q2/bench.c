/* predictor_lab/Q2/bench.c — BTB Aliasing */

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
#define N      8
#define PASSES 2000

static long aa[N], bb[N];

/* loop_A：对 aa[] 求和（单次遍历） */
__attribute__((noinline))
static long loop_A(void) {
    long sum = 0;
    for (int i = 0; i < N; i++)
        sum += aa[i];
    return sum;
}

/* loop_B：对 bb[] 求和（单次遍历） */
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

    long c0, c1, sa = 0, sb = 0;

    c0 = rdcycle();
    for (int r = 0; r < PASSES; r++) {
        asm volatile("" ::: "memory");
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
