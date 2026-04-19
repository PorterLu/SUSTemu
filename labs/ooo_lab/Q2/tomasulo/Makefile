REPO    := ../../../..
EMU     := $(REPO)/build/sustemu

CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
OBJDUMP = riscv64-unknown-elf-objdump
CFLAGS  = -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany -O2 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall -Wno-unused-variable -Wno-unused-function
CFLAGS_DBG = $(CFLAGS) -g

.PHONY: all run-inorder run-ooo run-inorder-debug run-ooo-debug disasm clean

all: tomasulo.bin

tomasulo.o: tomasulo.c csr_perf.h
	$(CC) $(CFLAGS) -c $< -o $@

start.o: start.S
	$(CC) -march=rv64im -mabi=lp64 -mno-relax -c $< -o $@

tomasulo.elf: start.o tomasulo.o
	$(LD) -T linker.ld start.o tomasulo.o -o $@

tomasulo.bin: tomasulo.elf
	$(OBJCOPY) -O binary $< $@

run-inorder: tomasulo.bin $(EMU)
	$(EMU) --inorder -b -l /dev/null -e tomasulo.elf tomasulo.bin

run-ooo: tomasulo.bin $(EMU)
	$(EMU) --ooo --bpred -b -l /dev/null -e tomasulo.elf tomasulo.bin

run-inorder-debug: tomasulo.bin $(EMU)
	$(EMU) --inorder --trace -b -l tomasulo_trace_inorder.log -e tomasulo.elf tomasulo.bin

run-ooo-debug: tomasulo.bin $(EMU)
	$(EMU) --ooo --bpred --trace -b -l tomasulo_trace_ooo.log -e tomasulo.elf tomasulo.bin

tomasulo.dbg.elf: start.o tomasulo.c csr_perf.h
	$(CC) $(CFLAGS_DBG) -c tomasulo.c -o tomasulo.dbg.o
	$(LD) -T linker.ld start.o tomasulo.dbg.o -o $@

disasm: tomasulo.dbg.elf
	$(OBJDUMP) -S $< > tomasulo.dis
	@echo "Disassembly written to tomasulo.dis"

clean:
	rm -f *.o *.elf *.bin *.log *.dis
