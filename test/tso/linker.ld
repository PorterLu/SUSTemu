OUTPUT_ARCH( "riscv" )
ENTRY( _start )

SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text.init)
    *(.text .text.*)
  }

  .rodata : {
    *(.rodata .rodata.*)
  }

  .data : {
    *(.data .data.*)
  }

  .bss : {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  }

  /* Hart 0 stack: 4 KB */
  . = ALIGN(16);
  . += 0x1000;
  _stack_top = .;

  /* Hart 1 stack: another 4 KB immediately above */
  . += 0x1000;
  _stack1_top = .;
}
