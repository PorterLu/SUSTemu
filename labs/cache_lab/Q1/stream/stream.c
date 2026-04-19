#include "csr_perf.h"

/* ── Minimal runtime ─────────────────────────────────────────────────── */

typedef unsigned long  uint64_t;
typedef unsigned int   uint32_t;

#define UART_THR_ADDR 0xa00003f8ULL
#define UART_THR      (*(volatile char *)UART_THR_ADDR)

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

/* ── Benchmark parameters ────────────────────────────────────────────── */

#define ARRAY_LINES  320        /* 320 cache lines × 64 B = 20 KB working set */
#define STRIDE       16         /* stride in uint32 units = 64 B = 1 cache line */
#define ARRAY_ELEMS  (ARRAY_LINES * STRIDE)   /* 5120 uint32s = 20 KB */
#define PASSES       10         /* measured passes after warm-up */

static volatile uint32_t arr[ARRAY_ELEMS];

/* ── main ────────────────────────────────────────────────────────────── */

int main(void)
{
    uart_puts("Stream (stride-16) cache benchmark\r\n");
    uart_puts("Working set: ");
    uart_put_uint(ARRAY_LINES);
    uart_puts(" lines (20 KB)\r\n\r\n");

    /* Initialise array */
    for (int i = 0; i < ARRAY_ELEMS; i++)
        arr[i] = (uint32_t)i;

    /* Warm-up: one full pass, not counted */
    uint64_t dummy = 0;
    for (int i = 0; i < ARRAY_ELEMS; i += STRIDE)
        dummy += arr[i];
    (void)dummy;

    /* ── Measured region ─────────────────────────────────────────────── */
    uint64_t h0 = rdl1d_hits();
    uint64_t m0 = rdl1d_misses();

    uint64_t sum = 0;
    for (int p = 0; p < PASSES; p++)
        for (int i = 0; i < ARRAY_ELEMS; i += STRIDE)
            sum += arr[i];
    (void)sum;

    uint64_t hits   = rdl1d_hits()   - h0;
    uint64_t misses = rdl1d_misses() - m0;
    uint64_t total  = hits + misses;
    uint64_t pct    = total ? hits * 100 / total : 0;

    /* ── Results ─────────────────────────────────────────────────────── */
    uart_puts("Passes         : ");
    uart_put_uint(PASSES);
    uart_puts("\r\nAccesses/pass  : ");
    uart_put_uint(ARRAY_LINES);
    uart_puts("\r\nTotal accesses : ");
    uart_put_uint(total);
    uart_puts("\r\n\r\n");

    uart_puts("=== L1D Cache Stats ===\r\n");
    uart_puts("  hits   : "); uart_put_uint(hits);   uart_puts("\r\n");
    uart_puts("  misses : "); uart_put_uint(misses); uart_puts("\r\n");
    uart_puts("  hit%   : "); uart_put_uint(pct);    uart_puts("%\r\n");
    uart_puts("=======================\r\n");

    return 0;
}
