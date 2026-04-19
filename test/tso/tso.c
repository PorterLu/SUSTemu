/*
 * test/tso/tso.c — TSO store-load litmus test
 *
 * Classic Dekker-variant:
 *   Initially x = y = 0
 *   Core 0: x=1; [fence?]; r1=y
 *   Core 1: y=1; [fence?]; r2=x
 *
 * Under SC:   r1=0 && r2=0 is IMPOSSIBLE.
 * Under TSO:  r1=0 && r2=0 is POSSIBLE (store buffer delays visibility).
 *
 * Build: -DUSE_FENCE=0 → no fence in test sequence, expect violations
 *        -DUSE_FENCE=1 → fence between store and load, no violations
 *
 * The inter-trial barrier always uses fence so that the barrier mechanism
 * itself is not affected by the store buffer.
 */

#define UART_BASE   0xa00003f8UL
#define MEM_BASE    0x80200000UL

/* Shared memory layout (non-overlapping 4-byte slots) */
static volatile int *x     = (volatile int *)(MEM_BASE +  0);
static volatile int *y     = (volatile int *)(MEM_BASE +  4);
static volatile int *r1v   = (volatile int *)(MEM_BASE +  8);
static volatile int *r2v   = (volatile int *)(MEM_BASE + 12);
/* Barrier: each core owns its own monotonically-increasing sequence number.
 * A core waits until the other's seq has reached its own seq. */
static volatile int *seq0  = (volatile int *)(MEM_BASE + 16);
static volatile int *seq1  = (volatile int *)(MEM_BASE + 20);

#define N_TRIALS 100

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
 * barrier(hartid, n): rendezvous at phase n.
 * Each core writes its own seq = n, fences so the write is globally visible,
 * then spins until the other core's seq >= n.
 * Using >= (not ==) avoids the missed-value race: if the other core has
 * already advanced past n, the condition is still satisfied.
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

void tso_main(int hartid)
{
    int violations = 0;

    /* seq0, seq1, x, y start at 0 from DRAM zero-initialisation.
     * No explicit init needed: avoiding the init race where Core 0
     * would overwrite Core 1's already-written seq1. */

    /* Phase 1: both cores ready */
    barrier(hartid, 1);

    for (int t = 0; t < N_TRIALS; t++) {
        /* Phase 3t+2: reset shared variables (core 0 does the writes) */
        if (hartid == 0) { *x = 0; *y = 0; }
        barrier(hartid, 3 * t + 2);

        /* ── Test sequence ───────────────────────────────────────────── */
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

        /* Phase 3t+3: test done, results written */
        barrier(hartid, 3 * t + 3);

        /* Phase 3t+4: core 0 checks the result */
        if (hartid == 0 && *r1v == 0 && *r2v == 0)
            violations++;

        barrier(hartid, 3 * t + 4);
    }

    /* Report (core 0 only, after all trials) */
    if (hartid == 0) {
        uart_puts("=== TSO Litmus Test ===\n");
#if USE_FENCE
        uart_puts("Mode      : WITH fence rw,rw\n");
#else
        uart_puts("Mode      : WITHOUT fence\n");
#endif
        uart_puts("Trials    : "); uart_putint(N_TRIALS); uart_putchar('\n');
        uart_puts("Violations: "); uart_putint(violations); uart_putchar('\n');
        uart_puts(violations > 0
            ? "Result: TSO VIOLATION (r1=0 && r2=0 observed)\n"
            : "Result: No violation observed\n");
    }

    /* Final barrier: Core 1 waits for Core 0 to finish printing before
     * either core calls ebreak.  Without this, Core 1's ebreak sets
     * state=NEMU_END (in EX stage) before Core 0 has flushed its UART
     * stores, terminating the simulation prematurely. */
    barrier(hartid, 3 * N_TRIALS + 5);

    __asm__ volatile("ebreak");
}
