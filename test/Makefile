all:
	riscv64-unknown-elf-gcc -march=rv64im -mabi=lp64 -mcmodel=medany -O2 -ffreestanding -mno-relax -c main.c -o main.o
	riscv64-unknown-elf-gcc -march=rv64im -mabi=lp64 -mno-relax -c start.S -o start.o
	riscv64-unknown-elf-ld -T linker.ld start.o main.o -o kernel.elf --no-warn-rwx-segments
	riscv64-unknown-elf-objcopy -O binary kernel.elf kernel.bin
clean:
	rm -rf ./*.o kernel.elf kernel.bin
