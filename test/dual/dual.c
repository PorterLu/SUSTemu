/*
 * test/dual/dual.c — Dual-core smoke test for Phase 5
 *
 * Both harts execute the same arithmetic workload independently.
 * Hart 0 announces its hartid at startup (before the workload) so
 * the output appears before either core reaches ebreak.
 * Each hart stores its result at RESULT_BASE + hartid * 8.
 */

#define UART_BASE    0xa00003f8UL
#define RESULT_BASE  0x80200000UL

static void uart_putchar(char c)
{
    volatile char *uart = (volatile char *)UART_BASE;
    *uart = c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putchar(*s++);
}

static void uart_puthex(unsigned long v)
{
    const char *hex = "0123456789ABCDEF";
    uart_putchar('0'); uart_putchar('x');
    for (int i = 60; i >= 0; i -= 4)
        uart_putchar(hex[(v >> i) & 0xf]);
}

static void uart_putint(int v)
{
    uart_putchar('0' + v);
}

static unsigned long run_workload(void)
{
    unsigned long result = 0;
    for (int i = 0; i < 10000; i++) {
        result += (unsigned long)i * i;
        result ^= result >> 17;
        result += 0xdeadbeefUL;
        result ^= result << 13;
    }
    return result;
}

void dual_main(int hartid)
{
    /* Announce hartid BEFORE the workload so both cores output while running */
    uart_puts("Hart ");
    uart_putint(hartid);
    uart_puts(": mhartid=");
    uart_putint(hartid);
    uart_putchar('\n');

    unsigned long result = run_workload();

    /* Store per-hart result to dedicated memory slot */
    volatile unsigned long *out =
        (volatile unsigned long *)(RESULT_BASE + hartid * 8);
    *out = result;

    if (hartid == 0) {
        uart_puts("Hart 0 result: ");
        uart_puthex(result);
        uart_putchar('\n');
        uart_puts("DUAL-CORE OK\n");
    }

    /* Both harts halt here */
    __asm__ volatile("ebreak");
}
