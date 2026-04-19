/* predictor_lab/Q1/bench.c
 *
 * Q1: 分支预测器效果演示
 *
 * branch_regular：taken/not-taken 严格交替（50/50）
 * 分别用 make run-nobpred 和 make run-bpred 运行，观察 IPC 和误预测次数的差异。
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

#define NELEM  1024
#define PASSES 20

static long arr[NELEM];

static long branch_regular(void) {
    long sum = 0;
    for (int p = 0; p < PASSES; p++) {
        for (int i = 0; i < NELEM; i++) {
            if (arr[i] % 2 == 0)
                sum++;
            else
                sum--;
        }
    }
    return sum;
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
    uart_puts(": cycles=");    uart_putlong(cycles);
    uart_puts("  insts=");     uart_putlong(insts);
    uart_puts("  IPC*100=");   uart_putlong(insts * 100 / cycles);
    uart_puts("  result=");    uart_putlong(result);
    uart_puts("\n");
}

int main(void) {
    for (int i = 0; i < NELEM; i++)
        arr[i] = i;

    uart_puts("=== Q1: Branch Predictor ===\n");
    measure("branch_regular (50/50)", branch_regular);
    uart_puts("done\n");
    return 0;
}
