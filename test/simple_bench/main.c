#include <stdint.h>
#include "csr_perf.h"

// 串口输出地址
#define UART_THR_ADDR 0xa00003f8
#define UART_THR      (*(volatile char *)UART_THR_ADDR)

// 测试参数
#define TEST_SIZE 32*1024
uint32_t data_buffer[TEST_SIZE];

void uart_putc(char c) {
    UART_THR = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

// 将数字转为十六进制字符串输出（用于显示结果）
void uart_put_hex(uint64_t val) {
    char hex_chars[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        uart_putc(hex_chars[(val >> (i * 4)) & 0xF]);
    }
}

void uart_put_dec(long val) {
    if (val < 0) { uart_putc('-'); val = -val; }
    if (val == 0) { uart_putc('0'); return; }
    char buf[20];
    int n = 0;
    while (val) { buf[n++] = '0' + (val % 10); val /= 10; }
    for (int i = n - 1; i >= 0; i--) uart_putc(buf[i]);
}

// --- 性能测试模块 ---

void test_performance() {
    volatile uint32_t temp = 0;

    // 1. 读写性能测试 (Memory Read/Write)
    uart_puts("\r\n--- Testing R/W Performance ---\r\n");

    long d_hits_start   = rdl1d_hits();
    long d_misses_start = rdl1d_misses();

    for (int i = 0; i < TEST_SIZE; i++) {
        data_buffer[i] = i; // 写
        temp = data_buffer[i]; // 读
    }

    long d_hits   = rdl1d_hits()   - d_hits_start;
    long d_misses = rdl1d_misses() - d_misses_start;

    uart_puts("L1D hits in R/W loop:   "); uart_put_dec(d_hits);   uart_puts("\r\n");
    uart_puts("L1D misses in R/W loop: "); uart_put_dec(d_misses); uart_puts("\r\n");
    uart_puts("L1D total accesses:     "); uart_put_dec(d_hits + d_misses); uart_puts("\r\n");

    // 2. 计算性能测试 (Computation - 简单的算术迭代)
    uart_puts("\r\n--- Testing Calc Performance ---\r\n");
    uint32_t a = 0x1234, b = 0x5678;
    for (int i = 0; i < 10000; i++) {
        a = (a * b) + i;
        a ^= (a >> 3);
    }

    // 打印结果以防止编译器过度优化
    uart_puts("\r\nFinal temp val: 0x");
    uart_put_hex(a);
    uart_puts("\r\nDone.\r\n");
}

int main() {
    uart_puts("RISC-V Bare-metal Performance Test Initializing...\r\n");

    test_performance();

    return 0;
}
