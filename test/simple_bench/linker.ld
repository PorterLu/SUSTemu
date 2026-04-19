OUTPUT_ARCH( "riscv" )
ENTRY( _start )

SECTIONS
{
  . = 0x80000000;

  /* 代码段 */
  .text : {
    *(.text.init) /* 确保启动汇编排在最前面 */
    *(.text .text.*)
  }

  /* 只读数据段 */
  .rodata : {
    *(.rodata .rodata.*)
  }

  /* 已初始化数据段 */
  .data : {
    *(.data .data.*)
  }

  /* 未初始化数据段 */
  .bss : {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  }

  /* 定义堆栈顶端 (向下增长) */
  . = ALIGN(16);
  . += 0x1000; /* 4KB 栈空间 */
  _stack_top = .;
}
