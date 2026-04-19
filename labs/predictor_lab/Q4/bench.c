/* predictor_lab/Q5/bench.c — Tournament vs Confidence-Fusion Predictor
 *
 * 测试场景：每次条件分支前有 6 个 always-taken "噪声"分支。
 *   - 全局(gshare)：GHR 被 111111 污染，条件分支映射到同一 PHT 槽，
 *     无法区分 T/NT → global_conf 维持 1~2（不确定）
 *   - 局部(local)：LHT 是 per-PC，噪声分支不影响条件分支的本地历史，
 *     经过训练后 local_conf = 0 或 3（强预测）
 *
 * 置信度选择：local_conf=3 vs global_conf=1 → 选 local → 正确
 * tournament meta：慢慢学习，可能继续选 global（偶尔"走运"的那个）
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

#define NELEM  4096
#define PASSES 20

static long arr[NELEM];

/* always-taken 分支，每条独占独立 PC，用于污染 GHR */
#define NOISE(s) do { int _t=1; \
    asm volatile("bnez %1,1f\naddi %0,%0,0\n1:\n":"+r"(s):"r"(_t)); \
} while(0)

/* 6 个 always-taken 噪声分支污染 GHR，随后是一个 50/50 条件分支 */
static long branch(void) {
    long sum = 0;
    for (int p = 0; p < PASSES; p++)
        for (int i = 0; i < NELEM; i++) {
            if (arr[i] % 2 == 0) sum++;
            else                 sum--;
        }
    return sum;
}

static void measure(const char *name, long (*fn)(void)) {
    long c0 = rdcycle(), r0 = rdinstret();
    long result = fn();
    long c1 = rdcycle(), r1 = rdinstret();
    long cycles = c1 - c0, insts = r1 - r0;
    uart_puts(name);
    uart_puts(": cycles=");  uart_putlong(cycles);
    uart_puts("  IPC*100="); uart_putlong(insts * 100 / cycles);
    uart_puts("  result=");  uart_putlong(result);
    uart_puts("\n");
}

int main(void) {
    for (int i = 0; i < NELEM; i++) arr[i] = i;
    measure("branch", branch);
    uart_puts("done\n");
    return 0;
}
