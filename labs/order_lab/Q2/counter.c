/*
 * order_lab/Q2/counter/counter.c — Non-Atomic Shared Counter
 *
 * Both cores increment a shared counter N times each.
 * Expected final value: 2 * N_ITER
 *
 * The increment operation is:
 *   tmp = counter;   (LOAD)
 *   tmp = tmp + 1;
 *   counter = tmp;   (STORE)
 *
 * This is NOT atomic. Under TSO with a store buffer:
 *   - Core 0's store (counter=k+1) sits in its store buffer.
 *   - Core 1's load reads the stale value k from cache.
 *   - Both cores end up writing k+1, losing one update.
 *
 * Without any synchronization, the final counter is roughly N_ITER
 * instead of 2*N_ITER — approximately half the updates are lost.
 */

#define UART_BASE  0xa00003f8UL
#define MEM_BASE   0x80200000UL

static volatile int *counter = (volatile int *)(MEM_BASE + 0);
static volatile int *done0   = (volatile int *)(MEM_BASE + 4);
static volatile int *done1   = (volatile int *)(MEM_BASE + 8);
static volatile int *seq0    = (volatile int *)(MEM_BASE + 12);
static volatile int *seq1    = (volatile int *)(MEM_BASE + 16);

#define N_ITER 1000

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

void lab_main(int hartid)
{
    /* Wait for both cores to be ready */
    barrier(hartid, 1);

    /* Both cores increment the shared counter N_ITER times */
    for (int i = 0; i < N_ITER; i++) {
        int tmp = *counter;   /* LOAD  — may read stale value from cache */
        tmp = tmp + 1;
        *counter = tmp;       /* STORE — goes into store buffer, not cache */
    }

    /* Signal this core is done */
    if (hartid == 0) *done0 = 1;
    else             *done1 = 1;
    __asm__ volatile("fence rw, rw");

    /* Core 0 waits for Core 1, then reports */
    if (hartid == 0) {
        while (*done1 == 0) {}
        __asm__ volatile("fence rw, rw");

        uart_puts("=== Q2: Non-Atomic Shared Counter ===\n");
        uart_puts("Iterations per core : "); uart_putint(N_ITER); uart_putchar('\n');
        uart_puts("Expected final value: "); uart_putint(2 * N_ITER); uart_putchar('\n');
        uart_puts("Actual final value  : "); uart_putint(*counter); uart_putchar('\n');
        int lost = 2 * N_ITER - *counter;
        uart_puts("Lost updates        : "); uart_putint(lost); uart_putchar('\n');
        uart_puts(lost > 0
            ? "Result: LOST UPDATES observed (non-atomic increment)\n"
            : "Result: No lost updates\n");
    }

    barrier(hartid, 2);
    __asm__ volatile("ebreak");
}
