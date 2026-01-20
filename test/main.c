#include <stdint.h>

// 串口输出地址
#define UART_THR_ADDR 0xa00003f8
#define UART_THR      (*(volatile char *)UART_THR_ADDR)

// 测试参数
#define TEST_SIZE 1024
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

// --- 性能测试模块 ---

void test_performance() {
    volatile uint32_t temp = 0;

    // 1. 读写性能测试 (Memory Read/Write)
    uart_puts("\r\n--- Testing R/W Performance ---\r\n");
    for (int i = 0; i < TEST_SIZE; i++) {
        data_buffer[i] = i; // 写
        temp = data_buffer[i]; // 读
    }

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

    while (1) {
        // 挂起或循环
    }
    return 0;
}
