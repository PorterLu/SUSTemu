/* predictor_lab/Q3/bench.c — Cold Start */

#include "csr_perf.h"

#define UART_THR (*(volatile char *)0xa00003f8)
static void uart_putchar(char c) { UART_THR = c; }
static void uart_puts(const char *s) { while (*s) uart_putchar(*s++); }
static void uart_putlong(long v) {
    if (v < 0) { uart_putchar('-'); v = -v; }
    if (v >= 10) uart_putlong(v / 10);
    uart_putchar('0' + (int)(v % 10));
}

#define ALWAYS_TAKEN(sum)  do {                                  \
    int _t = 1;                                                  \
    asm volatile("bnez %1, 1f\n\t"                              \
                 "addi %0, %0, 0\n\t"                           \
                 "1:\n\t"                                        \
                 "addi %0, %0, 1\n"                             \
                 : "+r"(sum) : "r"(_t));                        \
} while (0)

#define ALWAYS_NT  do {                                          \
    int _z = 0;                                                  \
    asm volatile("bnez %0, 1f\n\t"                              \
                 "1:\n\t"                                        \
                 : : "r"(_z));                                   \
} while (0)

/* 16 次 always-NT 分支，将 12-bit GHR 清零 */
__attribute__((noinline))
static void reset_ghr(void) {
    ALWAYS_NT; ALWAYS_NT; ALWAYS_NT; ALWAYS_NT;
    ALWAYS_NT; ALWAYS_NT; ALWAYS_NT; ALWAYS_NT;
    ALWAYS_NT; ALWAYS_NT; ALWAYS_NT; ALWAYS_NT;
    ALWAYS_NT; ALWAYS_NT; ALWAYS_NT; ALWAYS_NT;
}

/* 32 个 always-taken 分支，各占独立 PC */
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

int main(void) {
    uart_puts("=== Q3: Cold Start ===\n");

    volatile long result;
    long c0, c1;

    reset_ghr();
    c0 = rdcycle();
    result = probe_branches();
    c1 = rdcycle();
    uart_puts("cold: cycles="); uart_putlong(c1 - c0);
    uart_puts("  result="); uart_putlong(result); uart_puts("\n");

    for (int w = 0; w < 8; w++) { reset_ghr(); result = probe_branches(); }

    reset_ghr();
    c0 = rdcycle();
    result = probe_branches();
    c1 = rdcycle();
    uart_puts("warm: cycles="); uart_putlong(c1 - c0);
    uart_puts("  result="); uart_putlong(result); uart_puts("\n");

    uart_puts("done\n");
    return 0;
}

