REPO    := ../..
EMU     := $(REPO)/build/sustemu

CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
CFLAGS  = -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany -O2 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall -Wno-unused-variable -Wno-unused-function

.PHONY: all build run clean

all: seq_scan.bin

seq_scan.o: seq_scan.c csr_perf.h
	$(CC) $(CFLAGS) -c $< -o $@

start.o: start.S
	$(CC) -march=rv64im -mabi=lp64 -mno-relax -c $< -o $@

seq_scan.elf: start.o seq_scan.o
	$(LD) -T linker.ld start.o seq_scan.o -o $@ --no-warn-rwx-segments

seq_scan.bin: seq_scan.elf
	$(OBJCOPY) -O binary $< $@

# Rebuild the simulator after editing src/memory/prefetch.c
build:
	$(MAKE) -C $(REPO)

# Single run
run: seq_scan.bin $(EMU)
	$(EMU) -b -l /dev/null -e seq_scan.elf seq_scan.bin

clean:
	rm -f *.o *.elf *.bin
