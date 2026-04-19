REPO    := ../../..
EMU     := $(REPO)/build/sustemu

CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
CFLAGS  = -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany -O2 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall -Wno-unused-variable -Wno-unused-function

.PHONY: all build run clean

all: matmul.bin

matmul.o: matmul.c csr_perf.h
	$(CC) $(CFLAGS) -c $< -o $@

start.o: start.S
	$(CC) -march=rv64im -mabi=lp64 -mno-relax -c $< -o $@

matmul.elf: start.o matmul.o
	$(LD) -T linker.ld start.o matmul.o -o $@ --no-warn-rwx-segments

matmul.bin: matmul.elf
	$(OBJCOPY) -O binary $< $@

# Rebuild the simulator after editing include/memory/cache.h
build:
	$(MAKE) -C $(REPO)

# Single run
run: matmul.bin $(EMU)
	$(EMU) -b -l /dev/null -e matmul.elf matmul.bin

clean:
	rm -f *.o *.elf *.bin
