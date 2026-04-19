#include "csr_perf.h"

/* 数组大小：65536 个 int = 256KB BSS，远大于 32KB L1D */
#define SCAN_N 65536

typedef unsigned long uint64_t;

#define UART_THR (*(volatile char *)0xa00003f8ULL)

static void uart_putc(char c) { UART_THR = c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void uart_put_uint(uint64_t v)
{
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[--i] = '0' + (v % 10); v /= 10; }
    uart_puts(buf + i);
}

static volatile int arr[SCAN_N];

static void init_arr(void) {
    for (int i = 0; i < SCAN_N; i++)
        arr[i] = i + 1;
}

__attribute__((noinline))
static long scan(void) {
    long sum = 0;
    for (int i = 0; i < SCAN_N; i++)
        sum += arr[i];
    return sum;
}

static void report(const char *label, long h0, long m0)
{
    long hits   = rdl1d_hits()   - h0;
    long misses = rdl1d_misses() - m0;
    long total  = hits + misses;

    uart_puts(label);
    uart_puts(": misses="); uart_put_uint((uint64_t)misses);
    uart_puts("  hits=");   uart_put_uint((uint64_t)hits);
    uart_puts("  total=");  uart_put_uint((uint64_t)total);
    uart_puts("\r\n");
}

int main(void)
{
    long h0, m0;

    uart_puts("=== Sequential Scan Prefetch Demo ===\r\n");

    /* 热身：初始化数据（不纳入统计） */
    init_arr();

    /* scan1：首次扫描（L1D + L2 均冷） */
    h0 = rdl1d_hits(); m0 = rdl1d_misses();
    volatile long s1 = scan();
    report("scan1", h0, m0);

    /* scan2：二次扫描（L1D 冷，L2 热） */
    h0 = rdl1d_hits(); m0 = rdl1d_misses();
    volatile long s2 = scan();
    report("scan2", h0, m0);

    uart_puts("=====================================\r\n");

    /* 防止 DCE */
    if ((s1 + s2) & 1) uart_putc('!');

    return 0;
}
