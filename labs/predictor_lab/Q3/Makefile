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

.PHONY: all run-ooo disasm clean

all: bench.bin

bench.o: bench.c csr_perf.h
	$(CC) $(CFLAGS) -c $< -o $@

start.o: start.S
	$(CC) -march=rv64im -mabi=lp64 -mno-relax -c $< -o $@

bench.elf: start.o bench.o
	$(LD) -T linker.ld start.o bench.o -o $@

bench.bin: bench.elf
	$(OBJCOPY) -O binary $< $@

# Q3a: in-order mode (mispred penalty = 1 cycle)
run-bpred: bench.bin $(EMU)
	$(EMU) --inorder --bpred -b -l /dev/null -e bench.elf bench.bin

# Q3a: OOO mode (mispred penalty = ROB flush ~16 cycles, cold/warm gap is larger)
run-ooo: bench.bin $(EMU)
	$(EMU) --ooo --bpred -b -l /dev/null -e bench.elf bench.bin

bench.dbg.elf: start.o bench.c csr_perf.h
	$(CC) $(CFLAGS_DBG) -c bench.c -o bench.dbg.o
	$(LD) -T linker.ld start.o bench.dbg.o -o $@

disasm: bench.dbg.elf
	$(OBJDUMP) -S $< > bench.dis
	@echo "Disassembly written to bench.dis"

clean:
	rm -f *.o *.elf *.bin *.log *.dis
