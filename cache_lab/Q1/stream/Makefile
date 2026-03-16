REPO    := ../../..
EMU     := $(REPO)/build/sustemu

CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
CFLAGS  = -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany -O2 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall -Wno-unused-variable -Wno-unused-function

.PHONY: all build run sweep clean

all: stream.bin

stream.o: stream.c csr_perf.h
	$(CC) $(CFLAGS) -c $< -o $@

start.o: start.S
	$(CC) -march=rv64im -mabi=lp64 -mno-relax -c $< -o $@

stream.elf: start.o stream.o
	$(LD) -T linker.ld start.o stream.o -o $@ --no-warn-rwx-segments

stream.bin: stream.elf
	$(OBJCOPY) -O binary $< $@

# Rebuild the simulator after editing include/memory/cache.h
build:
	$(MAKE) -C $(REPO)

# Single run — reads L1D_S from whatever is compiled into the binary
run: stream.bin $(EMU)
	$(EMU) -b -l /dev/null -e stream.elf stream.bin

# Sweep: patch L1D_S, rebuild simulator + test, run, collect hit%
sweep: $(EMU)
	@bash run_sweep.sh

clean:
	rm -f *.o *.elf *.bin
