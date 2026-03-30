/* Q1/ilp/ilp.c — 指令级并行（ILP）实验
 *
 * 对比两个 kernel：
 *
 *   kernel_serial   — pointer-chasing：sum += aa[sum & (NELEM-1)]
 *   kernel_parallel — 两条独立累加链：sa += aa[i]; sb += bb[i]
 *
 * 在 --inorder 和 --ooo 两种模式下运行，观察 IPC 差异。
 *
 * 数组大小：2 × NELEM × 8B = 8KB，完全在 L1D（32KB）内。
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
#define NELEM  512    /* 每个数组元素数 */
#define PASSES 40     /* 遍历轮数 */

static long aa[NELEM], bb[NELEM];

/* ── kernel_serial ──────────────────────────────────────────────────────── */
static long kernel_serial(void) {
    long sum = 0;
    for (int p = 0; p < PASSES; p++) {
        for (int i = 0; i < NELEM; i++) {
            sum += aa[sum & (NELEM - 1)];
        }
    }
    return sum;
}

/* ── kernel_parallel ────────────────────────────────────────────────────── */
static long kernel_parallel(void) {
    long sa = 0, sb = 0;
    for (int p = 0; p < PASSES; p++) {
        for (int i = 0; i < NELEM; i++) {
            sa += aa[i];
            sb += bb[i];
        }
    }
    return sa + sb;
}

static void measure(const char *name, long (*fn)(void)) {
    long c0 = rdcycle();
    long r0 = rdinstret();
    long result = fn();
    long c1 = rdcycle();
    long r1 = rdinstret();

    long cycles = c1 - c0;
    long insts  = r1 - r0;

    uart_puts(name);
    uart_puts(": cycles=");
    uart_putlong(cycles);
    uart_puts("  insts=");
    uart_putlong(insts);
    uart_puts("  IPC×100=");
    uart_putlong(insts * 100 / cycles);
    uart_puts("  (result=");
    uart_putlong(result);
    uart_puts(")\n");
}

int main(void) {
    for (int i = 0; i < NELEM; i++) {
        aa[i] = i + 1;
        bb[i] = i + 2;
    }

    uart_puts("=== Q1: ILP 实验 ===\n");
    uart_puts("NELEM=");  uart_putlong(NELEM);
    uart_puts("  PASSES="); uart_putlong(PASSES);
    uart_puts("\n");

    volatile long warmup = 0;
    for (int i = 0; i < NELEM; i++) warmup += aa[i] + bb[i];

    measure("kernel_serial  ", kernel_serial);
    measure("kernel_parallel", kernel_parallel);

    uart_puts("done\n");
    return 0;
}
