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

.PHONY: all run-inorder run-inorder-debug run-ooo run-ooo-debug disasm clean

all: ilp.bin

ilp.o: ilp.c csr_perf.h
	$(CC) $(CFLAGS) -c $< -o $@

start.o: start.S
	$(CC) -march=rv64im -mabi=lp64 -mno-relax -c $< -o $@

ilp.elf: start.o ilp.o
	$(LD) -T linker.ld start.o ilp.o -o $@

ilp.bin: ilp.elf
	$(OBJCOPY) -O binary $< $@

run-inorder: ilp.bin $(EMU)
	$(EMU) --inorder --bpred -b -l /dev/null -e ilp.elf ilp.bin

run-inorder-debug: ilp.bin $(EMU)
	$(EMU) --inorder --bpred --trace -b -l ilp_trace_inorder.log -e ilp.elf ilp.bin

run-ooo: ilp.bin $(EMU)
	$(EMU) --ooo --bpred -b -l /dev/null -e ilp.elf ilp.bin

run-ooo-debug: ilp.bin $(EMU)
	$(EMU) --ooo --bpred --trace -b -l ilp_trace_ooo.log -e ilp.elf ilp.bin

# Recompile with -g then disassemble: C source lines interleaved with PC/assembly
ilp.dbg.elf: start.o ilp.c csr_perf.h
	$(CC) $(CFLAGS_DBG) -c ilp.c -o ilp.dbg.o
	$(LD) -T linker.ld start.o ilp.dbg.o -o $@

disasm: ilp.dbg.elf
	$(OBJDUMP) -S $< > ilp.dis
	@echo "Disassembly written to ilp.dis"

clean:
	rm -f *.o *.elf *.bin *.log *.dis
