#include <common.h>
#include <string.h>
#include <stdio.h>
#include <csr.h>
#include <reg.h>

const char *regs[] = {
	"$0", "ra", "sp", "gp", "tp", "t0", "t1", "t2", 
	"s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
	"a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
	"s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

#define NR_REG ((sizeof(regs))/(sizeof(regs[0])))

CPU_state cpu;

void dump_gpr(){
	int i;
	for(i=0; i<32; i++)
	{
		printf("%3s = 0x%-16lx   ",regs[i], cpu.gpr[i]);
		if(i%8 == 7)
			printf("\n");
	}
}

void reg_display(){
	printf("Now pc is %lx\n", cpu.pc);
	dump_gpr();

	printf("mepc: %lx\n", csr.mepc);
	printf("mcause: %lx\n", csr.mcause);
	printf("mtvec: %lx\n", csr.mtvec);
	printf("mstatus: %lx\n", csr.mstatus);
}

void init_regs() {
	memset(&cpu, 0, sizeof(CPU_state));
	cpu.pc = 0x80000000;
}

word_t reg_str2val(const char *s, bool *success){
	int i;
	for(i = 0; i < NR_REG; i++){
		if(!strcmp(s, regs[i])){
			*success = true;
			return cpu.gpr[i];
		}
	}

	if(!strcmp(s, "pc"))
	{
		*success = true;
		return cpu.pc;
	}

	*success = false;
	return 0;
}
