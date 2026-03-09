CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy

CFLAGS  = -march=rv64im -mabi=lp64 -mcmodel=medany -O2 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall -Wno-unused-variable -Wno-unused-function

all: dhrystone.bin

dhrystone.o: dhrystone.c
	$(CC) $(CFLAGS) -c $< -o $@

start.o: start.S
	$(CC) -march=rv64im -mabi=lp64 -mno-relax -c $< -o $@

dhrystone.elf: start.o dhrystone.o
	$(LD) -T linker.ld start.o dhrystone.o -o $@ --no-warn-rwx-segments

dhrystone.bin: dhrystone.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin
