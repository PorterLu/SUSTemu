# SUSTemu
## Introduction
  SUSTemu is a RV64 system emulator, which supports IM extension. It draws lessons from Nemu(NJU emulator).

## Device  
  * flash
  * serial
  * rtc
  * keyborad
  * vga

## CSR
  8 PMPs are provided for security. S mode is under task.
  
## Interrupt and exeception
  Interrupt is under plan.

## Problem log
1. Enclave should save context after sbi_call, as application can't save context in enclave
2. pc should be set to 0xffffffff8000000
3. The address under 0x80000000 should be accessed by user level apps.
 
