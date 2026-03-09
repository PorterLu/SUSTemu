/* test/dual/start.S — dual-core entry point
 *
 * Both harts boot here.  Each reads mhartid to select its own stack
 * and then calls dual_main(hartid).
 *
 * Stack layout (4 KB per hart, growing downward):
 *   Hart 0 stack top : _stack_top      (end of .bss + 0x1000)
 *   Hart 1 stack top : _stack_top + 0x1000
 */

.section .text.init
.global _start

_start:
    /* Read hart ID */
    csrr  a0, mhartid          /* a0 = hartid (0 or 1) */

    /* BSS clear — only hart 0 does it to avoid races */
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
    /* Set per-hart stack:
     *   Hart 0 → _stack_top
     *   Hart 1 → _stack_top + 0x1000   */
    la    sp, _stack_top
    slli  t0, a0, 12            /* hartid * 4096 */
    add   sp, sp, t0

    /* Call dual_main(hartid) */
    call  dual_main

    /* Should not reach here, but halt anyway */
    ebreak
