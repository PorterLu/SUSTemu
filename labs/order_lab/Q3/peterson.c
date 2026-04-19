/*
 * order_lab/Q3/peterson/peterson.c — Peterson's Mutex
 *
 * Peterson's algorithm provides mutual exclusion for 2 cores using only
 * shared memory (no atomic instructions):
 *
 *   lock(i):
 *     want[i] = 1;          // "I want to enter"
 *     turn    = 1 - i;      // "You go first"
 *     while (want[1-i] && turn == 1-i) {}   // wait
 *
 *   unlock(i):
 *     want[i] = 0;
 *
 * Under Sequential Consistency (SC), this algorithm is provably correct:
 * at most one core can enter the critical section at a time.
 *
 * Under TSO, the algorithm BREAKS without fences:
 *   - want[i]=1 stays in the store buffer.
 *   - The other core reads want[i]=0 from cache (stale), sees no contention,
 *     and enters the critical section simultaneously.
 *
 * This program uses Peterson's lock to protect a shared counter increment.
 * Expected final value: 2 * N_ITER (no lost updates if lock is correct).
 *
 * Build variants:
 *   -DUSE_FENCE=0  No fences in lock → Peterson broken, lost updates
 *   -DUSE_FENCE=1  Fences after want[i]=1 and after turn=1-i → correct
 */

#define UART_BASE  0xa00003f8UL
#define MEM_BASE   0x80200000UL

/* Shared variables */
static volatile int *want  = (volatile int *)(MEM_BASE +  0); /* want[0], want[1] */
static volatile int *turn  = (volatile int *)(MEM_BASE +  8);
static volatile int *counter = (volatile int *)(MEM_BASE + 12);
static volatile int *seq0  = (volatile int *)(MEM_BASE + 16);
static volatile int *seq1  = (volatile int *)(MEM_BASE + 20);

#define N_ITER 2000

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

/*
 * Peterson's lock — acquire for core `i` (i = 0 or 1).
 *
 * TODO (Q4): Without fences, this lock is broken under TSO.
 *            Add fence rw,rw instructions at the right places so that
 *            want[i]=1 and turn=1-i are globally visible before the
 *            spin loop begins.
 */
static void lock(int i)
{
    int j = 1 - i;
    want[i] = 1;
#if USE_FENCE
    __asm__ volatile("fence rw, rw");   /* TODO: add this fence */
#endif
    *turn = j;
#if USE_FENCE
    __asm__ volatile("fence rw, rw");   /* TODO: add this fence */
#endif
    while (want[j] && *turn == j) {}
}

/*
 * Peterson's unlock — release for core `i`.
 */
static void unlock(int i)
{
    want[i] = 0;
#if USE_FENCE
    __asm__ volatile("fence rw, rw");
#endif
}

void lab_main(int hartid)
{
    barrier(hartid, 1);

    for (int i = 0; i < N_ITER; i++) {
        lock(hartid);

        /* Critical section: increment shared counter */
        int tmp = *counter;
        tmp = tmp + 1;
        *counter = tmp;

        unlock(hartid);
    }

    /* Wait for both cores to finish before reporting */
    barrier(hartid, 2);

    if (hartid == 0) {
        uart_puts("=== Q3: Peterson's Mutex ===\n");
#if USE_FENCE
        uart_puts("Mode      : WITH fence (correct TSO)\n");
#else
        uart_puts("Mode      : WITHOUT fence (broken under TSO)\n");
#endif
        uart_puts("Iterations per core : "); uart_putint(N_ITER); uart_putchar('\n');
        uart_puts("Expected final value: "); uart_putint(2 * N_ITER); uart_putchar('\n');
        uart_puts("Actual final value  : "); uart_putint(*counter); uart_putchar('\n');
        int lost = 2 * N_ITER - *counter;
        uart_puts("Lost updates        : "); uart_putint(lost); uart_putchar('\n');
        uart_puts(lost == 0
            ? "Result: CORRECT — Peterson's mutex works\n"
            : "Result: BROKEN — lost updates (TSO violation in lock)\n");
    }

    barrier(hartid, 3);
    __asm__ volatile("ebreak");
}
