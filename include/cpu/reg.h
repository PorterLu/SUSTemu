#ifndef __REG_H__
#define __REG_H__
#include <stdint.h>
#include <common.h>

#define Reg(i) reg##i

typedef struct {
	word_t gpr[32];
	vaddr_t pc;
} CPU_state;

extern CPU_state cpu;
extern const char *regs[];

//void update_register();
void dump_gpr();
void reg_display();
word_t reg_str2val(const char *s, bool *success);
void init_regs();
#define gpr(idx) (cpu.gpr[idx])

#endif
