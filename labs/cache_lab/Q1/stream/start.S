.section .text.init
.global _start

_start:
    # 2. 设置堆栈指针
    la sp, _stack_top

    # 3. 清零 BSS 段 (确保全局变量初值为 0)
    la t0, __bss_start
    la t1, __bss_end
bss_clear:
    beq t0, t1, bss_done
    sb zero, 0(t0)
    addi t0, t0, 1
    j bss_clear

bss_done:
    # 4. 跳转到 C 语言入口
    call main
    ebreak
