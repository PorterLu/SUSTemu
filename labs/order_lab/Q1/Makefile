CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
REPO    := ../../..
SIM     = $(REPO)/build/sustemu
FLAGS   = --dual --ooo --bpred -b

CFLAGS  = -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany -O1 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall

all: litmus.bin litmus_fence.bin

start.o: start.S
	$(CC) -march=rv64im_zicsr -mabi=lp64 -mno-relax -c $< -o $@

litmus.o: litmus.c
	$(CC) $(CFLAGS) -DUSE_FENCE=0 -c $< -o litmus.o

litmus_fence.o: litmus.c
	$(CC) $(CFLAGS) -DUSE_FENCE=1 -c $< -o litmus_fence.o

litmus.elf: start.o litmus.o
	$(LD) -T linker.ld start.o litmus.o -o $@ --no-warn-rwx-segments

litmus_fence.elf: start.o litmus_fence.o
	$(LD) -T linker.ld start.o litmus_fence.o -o $@ --no-warn-rwx-segments

litmus.bin: litmus.elf
	$(OBJCOPY) -O binary $< $@

litmus_fence.bin: litmus_fence.elf
	$(OBJCOPY) -O binary $< $@

run: litmus.bin litmus.elf
	$(SIM) $(FLAGS) -e litmus.elf litmus.bin

run-fence: litmus_fence.bin litmus_fence.elf
	$(SIM) $(FLAGS) -e litmus_fence.elf litmus_fence.bin

clean:
	rm -f *.o *.elf *.bin
