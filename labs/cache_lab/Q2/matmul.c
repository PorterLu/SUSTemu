#include "csr_perf.h"

/* ── Minimal runtime ─────────────────────────────────────────────────────── */

typedef unsigned long  uint64_t;
typedef unsigned int   uint32_t;

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

/* ── Matrix parameters ───────────────────────────────────────────────────── */

#define N 128 /* 128×128 int = 64 KB per matrix; B alone > 32 KB L1D → cache thrash */

static int A[N][N], B[N][N], C[N][N];

/* ── Helper: sequential init ─────────────────────────────────────────────── */
static void init(void)
{
    int i, j;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            A[i][j] = i + j;
            B[i][j] = i - j;
        }
}

/* ── ijk: naive order ────────────────────────────────────────────────────── */
/* B accessed as B[k][j]: k strides rows, j advances within a row.
 * But the outermost loop is i, middle is j, innermost is k — so for fixed
 * (i,j) we walk down column j of B: B[0][j], B[1][j], ..., B[N-1][j].
 * Column stride = N*sizeof(int) = 256 B = 4 cache lines → thrashes cache. */

__attribute__((noinline))
static void matmul_ijk(void)
{
    int i, j, k;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++)
            for (k = 0; k < N; k++)
                C[i][j] += A[i][k] * B[k][j];
}

__attribute__((noinline))
static void matmul_reorder(void)
{
        //todo
}

/* ── report helper ───────────────────────────────────────────────────────── */

static void report(const char *label, uint64_t h0, uint64_t m0,
                                      uint64_t lh0, uint64_t lm0)
{
    uint64_t hits   = rdl1d_hits()   - h0;
    uint64_t misses = rdl1d_misses() - m0;
    uint64_t total  = hits + misses;
    uint64_t pct    = total ? hits * 100 / total : 0;

    uint64_t l2hits   = rdl2_hits()   - lh0;
    uint64_t l2misses = rdl2_misses() - lm0;
    uint64_t l2total  = l2hits + l2misses;
    uint64_t l2pct    = l2total ? l2hits * 100 / l2total : 0;

    uart_puts(label);
    uart_puts(": L1D hits="); uart_put_uint(hits);
    uart_puts("  misses=");   uart_put_uint(misses);
    uart_puts("  hit%=");     uart_put_uint(pct);     uart_puts("%");
    uart_puts("  |  L2 hits="); uart_put_uint(l2hits);
    uart_puts("  misses=");     uart_put_uint(l2misses);
    uart_puts("  hit%=");       uart_put_uint(l2pct);   uart_puts("%\r\n");
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    int i, j;
    uint64_t h0, m0;

    uart_puts("=== Matrix Multiply Cache Demo (N=128) ===\r\n");

    init();

    uint64_t lh0, lm0;

    /* ijk */
    h0 = rdl1d_hits(); m0 = rdl1d_misses();
    lh0 = rdl2_hits(); lm0 = rdl2_misses();
    matmul_ijk();
    report("ijk", h0, m0, lh0, lm0);

    /* clear C */
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++)
            C[i][j] = 0;

    /* ikj */
    h0 = rdl1d_hits(); m0 = rdl1d_misses();
    lh0 = rdl2_hits(); lm0 = rdl2_misses();
    matmul_reorder();
    report("reorder", h0, m0, lh0, lm0);

    uart_puts("=========================================\r\n");
    return 0;
}
