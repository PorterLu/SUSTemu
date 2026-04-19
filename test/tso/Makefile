CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
SIM     = /home/porterlu/academic/sustemu/build/sustemu
FLAGS   = --dual --ooo --bpred -b

CFLAGS  = -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany -O1 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall

all: tso.bin tso_fence.bin

start.o: start.S
	$(CC) -march=rv64im_zicsr -mabi=lp64 -mno-relax -c $< -o $@

tso.o: tso.c
	$(CC) $(CFLAGS) -DUSE_FENCE=0 -c $< -o tso.o

tso_fence.o: tso.c
	$(CC) $(CFLAGS) -DUSE_FENCE=1 -c $< -o tso_fence.o

tso.elf: start.o tso.o
	$(LD) -T linker.ld start.o tso.o -o $@ --no-warn-rwx-segments

tso_fence.elf: start.o tso_fence.o
	$(LD) -T linker.ld start.o tso_fence.o -o $@ --no-warn-rwx-segments

tso.bin: tso.elf
	$(OBJCOPY) -O binary $< $@

tso_fence.bin: tso_fence.elf
	$(OBJCOPY) -O binary $< $@

run-tso: tso.bin tso.elf
	$(SIM) $(FLAGS) -e tso.elf tso.bin

run-tso-fence: tso_fence.bin tso_fence.elf
	$(SIM) $(FLAGS) -e tso_fence.elf tso_fence.bin

clean:
	rm -f *.o *.elf *.bin

