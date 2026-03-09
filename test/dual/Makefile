CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy

CFLAGS  = -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany -O2 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall

all: dual.bin

start.o: start.S
	$(CC) -march=rv64im_zicsr -mabi=lp64 -mno-relax -c $< -o $@

dual.o: dual.c
	$(CC) $(CFLAGS) -c $< -o $@

dual.elf: start.o dual.o
	$(LD) -T linker.ld start.o dual.o -o $@ --no-warn-rwx-segments

dual.bin: dual.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin
