CC      = riscv64-unknown-elf-gcc
LD      = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
REPO    := ../../..
SIM     = $(REPO)/build/sustemu
FLAGS   = --dual --ooo --bpred -b

CFLAGS  = -march=rv64im_zicsr -mabi=lp64 -mcmodel=medany -O1 \
          -ffreestanding -fno-builtin -mno-relax \
          -Wall

all: peterson.bin peterson_fence.bin

start.o: start.S
	$(CC) -march=rv64im_zicsr -mabi=lp64 -mno-relax -c $< -o $@

peterson.o: peterson.c
	$(CC) $(CFLAGS) -DUSE_FENCE=0 -c $< -o peterson.o

peterson_fence.o: peterson.c
	$(CC) $(CFLAGS) -DUSE_FENCE=1 -c $< -o peterson_fence.o

peterson.elf: start.o peterson.o
	$(LD) -T linker.ld start.o peterson.o -o $@ --no-warn-rwx-segments

peterson_fence.elf: start.o peterson_fence.o
	$(LD) -T linker.ld start.o peterson_fence.o -o $@ --no-warn-rwx-segments

peterson.bin: peterson.elf
	$(OBJCOPY) -O binary $< $@

peterson_fence.bin: peterson_fence.elf
	$(OBJCOPY) -O binary $< $@

run: peterson.bin peterson.elf
	$(SIM) $(FLAGS) -e peterson.elf peterson.bin

run-fence: peterson_fence.bin peterson_fence.elf
	$(SIM) $(FLAGS) -e peterson_fence.elf peterson_fence.bin

clean:
	rm -f *.o *.elf *.bin
