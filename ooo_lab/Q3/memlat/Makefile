REPO    := ../../..
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

all: memlat.bin

memlat.o: memlat.c csr_perf.h
	$(CC) $(CFLAGS) -c $< -o $@

start.o: start.S
	$(CC) -march=rv64im -mabi=lp64 -mno-relax -c $< -o $@

memlat.elf: start.o memlat.o
	$(LD) -T linker.ld start.o memlat.o -o $@

memlat.bin: memlat.elf
	$(OBJCOPY) -O binary $< $@

run-inorder: memlat.bin $(EMU)
	$(EMU) --inorder -b -l /dev/null -e memlat.elf memlat.bin

run-ooo: memlat.bin $(EMU)
	$(EMU) --ooo --bpred -b -l /dev/null -e memlat.elf memlat.bin

run-inorder-debug: memlat.bin $(EMU)
	$(EMU) --inorder --trace -b -l memlat_trace_inorder.log -e memlat.elf memlat.bin

run-ooo-debug: memlat.bin $(EMU)
	$(EMU) --ooo --bpred --trace -b -l memlat_trace_ooo.log -e memlat.elf memlat.bin

memlat.dbg.elf: start.o memlat.c csr_perf.h
	$(CC) $(CFLAGS_DBG) -c memlat.c -o memlat.dbg.o
	$(LD) -T linker.ld start.o memlat.dbg.o -o $@

disasm: memlat.dbg.elf
	$(OBJDUMP) -S $< > memlat.dis
	@echo "Disassembly written to memlat.dis"

clean:
	rm -f *.o *.elf *.bin *.log *.dis
