/* order_lab/Q3/peterson/start.S — dual-core entry */

.section .text.init
.global _start

_start:
    csrr  a0, mhartid

    bnez  a0, skip_bss

    la    t0, __bss_start
    la    t1, __bss_end
bss_clear:
    beq   t0, t1, bss_done
    sb    zero, 0(t0)
    addi  t0, t0, 1
    j     bss_clear
bss_done:

skip_bss:
    la    sp, _stack_top
    slli  t0, a0, 12
    add   sp, sp, t0

    call  lab_main

    ebreak
