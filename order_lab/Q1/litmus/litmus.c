/*
 * order_lab/Q1/litmus/litmus.c — Store-Load Reordering Litmus Test
 *
 * Classic Dekker litmus test (TSO violation demo):
 *
 *   Initially: x = 0, y = 0
 *   Core 0:  x = 1; [fence?]; r1 = y
 *   Core 1:  y = 1; [fence?]; r2 = x
 *
 * Under Sequential Consistency (SC):
 *   At least one of the two stores must be globally visible before
 *   the other core's load. Therefore r1=0 && r2=0 is IMPOSSIBLE.
 *
 * Under Total Store Order (TSO):
 *   Both stores can sit in each core's store buffer and be invisible
 *   to the other core when the loads execute. So r1=0 && r2=0 IS
 *   POSSIBLE — a TSO violation.
 *
 * Build variants:
 *   -DUSE_FENCE=0  No fence between store and load → expect violations
 *   -DUSE_FENCE=1  fence rw,rw between store and load → no violations
 */

#define UART_BASE  0xa00003f8UL
#define MEM_BASE   0x80200000UL

static volatile int *x    = (volatile int *)(MEM_BASE +  0);
static volatile int *y    = (volatile int *)(MEM_BASE +  4);
static volatile int *r1v  = (volatile int *)(MEM_BASE +  8);
static volatile int *r2v  = (volatile int *)(MEM_BASE + 12);
static volatile int *seq0 = (volatile int *)(MEM_BASE + 16);
static volatile int *seq1 = (volatile int *)(MEM_BASE + 20);

#define N_TRIALS 200

static void uart_putchar(char c)
{
    volatile char *uart = (volatile char *)UART_BASE;
    *uart = c;
}
static void uart_puts(const char *s) { while (*s) uart_putchar(*s++); }
static void uart_putint(int v)
{
    if (v < 0) { uart_putchar('-'); v = -v; }
    if (v >= 10) uart_putint(v / 10);
    uart_putchar('0' + v % 10);
}

/*
 * barrier(hartid, n): rendezvous barrier using monotonic sequence numbers.
 * Each core writes its own seq = n and fences, then waits for the other's
 * seq >= n. Using >= avoids the missed-value race when a core advances fast.
 */
static void barrier(int hartid, int n)
{
    if (hartid == 0) {
        *seq0 = n;
        __asm__ volatile("fence rw, rw");
        while (*seq1 < n) {}
    } else {
        *seq1 = n;
        __asm__ volatile("fence rw, rw");
        while (*seq0 < n) {}
    }
}

void lab_main(int hartid)
{
    int violations = 0;

    barrier(hartid, 1);

    for (int t = 0; t < N_TRIALS; t++) {
        /* Core 0 resets shared variables each trial */
        if (hartid == 0) { *x = 0; *y = 0; }
        barrier(hartid, 3 * t + 2);

        /* ── Litmus test sequence ──────────────────────────────────── */
        if (hartid == 0) {
            *x = 1;
#if USE_FENCE
            __asm__ volatile("fence rw, rw");
#endif
            *r1v = *y;
        } else {
            *y = 1;
#if USE_FENCE
            __asm__ volatile("fence rw, rw");
#endif
            *r2v = *x;
        }
        /* ─────────────────────────────────────────────────────────── */

        barrier(hartid, 3 * t + 3);

        if (hartid == 0 && *r1v == 0 && *r2v == 0)
            violations++;

        barrier(hartid, 3 * t + 4);
    }

    if (hartid == 0) {
        uart_puts("=== Q1: Store-Load Reordering Litmus Test ===\n");
#if USE_FENCE
        uart_puts("Mode      : WITH fence rw,rw\n");
#else
        uart_puts("Mode      : WITHOUT fence\n");
#endif
        uart_puts("Trials    : "); uart_putint(N_TRIALS); uart_putchar('\n');
        uart_puts("Violations: "); uart_putint(violations); uart_putchar('\n');
        uart_puts("Rate      : ");
        uart_putint(violations * 100 / N_TRIALS); uart_puts("%\n");
        uart_puts(violations > 0
            ? "Result: TSO VIOLATION observed (r1=0 && r2=0)\n"
            : "Result: No violation (SC behavior)\n");
    }

    barrier(hartid, 3 * N_TRIALS + 5);
    __asm__ volatile("ebreak");
}
