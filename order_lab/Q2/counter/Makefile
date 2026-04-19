CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
SIM     = /home/porterlu/academic/sustemu/build/sustemu
FLAGS   = --dual --ooo --bpred -b

CFLAGS  = -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany -O1 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall

all: counter.bin

start.o: start.S
	$(CC) -march=rv64im_zicsr -mabi=lp64 -mno-relax -c $< -o $@

counter.o: counter.c
	$(CC) $(CFLAGS) -c $< -o counter.o

counter.elf: start.o counter.o
	$(LD) -T linker.ld start.o counter.o -o $@ --no-warn-rwx-segments

counter.bin: counter.elf
	$(OBJCOPY) -O binary $< $@

run: counter.bin counter.elf
	$(SIM) $(FLAGS) -e counter.elf counter.bin

clean:
	rm -f *.o *.elf *.bin
