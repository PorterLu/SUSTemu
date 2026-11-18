#include <common.h>
#include <cpu/ir.h>
#include <csr.h>
#include <pmem.h>
#include <reg.h>
#include <state.h>
#include <utils.h>
#include <string.h>

struct DiffRegisters {
	word_t gpr[32];
	word_t pc;
};

extern uint8_t pmp_on_count;

static bool difftest_memory_contains(paddr_t addr, size_t size)
{
	return addr >= MBASE && size <= MSIZE && addr <= MBASE + MSIZE - size;
}

void difftest_init(int hartid)
{
	memset(&cpu, 0, sizeof(cpu));
	memset(&csr, 0, sizeof(csr));
	memset(pmpcfg, 0, sizeof(pmpcfg));
	memset(pmpaddr, 0, sizeof(pmpaddr));

	cpu.pc = MBASE;
	priv_level = MACHINE;
	g_current_hartid = hartid;
	g_sim_cycles = 0;
	g_sim_instret = 0;
	pmp_on_count = 0;
	state = NEMU_RUNNING;
}

void difftest_exec(uint64_t n)
{
	while (n--) {
		IR_Inst ir;
		word_t raw = host_read(guest_to_host(cpu.pc), 4);

		ir_decode((uint32_t)raw, cpu.pc, cpu.pc + 4, &ir);
		ir_execute(&ir, &cpu);

		if (ir.type == ITYPE_LOAD) {
			word_t value = host_read(guest_to_host(ir.mem_addr),
						 ir.mem_width);
			if (ir.mem_sign) {
				switch (ir.mem_width) {
				case 1: value = SEXT(value, 8); break;
				case 2: value = SEXT(value, 16); break;
				case 4: value = SEXT(value, 32); break;
				}
			}
			ir.result = value;
		} else if (ir.type == ITYPE_STORE) {
			host_write(guest_to_host(ir.mem_addr), ir.mem_width,
				   ir.src2_val);
		}

		ir_writeback(&ir, &cpu);
		cpu.pc = ir.dnpc;
		g_sim_cycles++;
		g_sim_instret++;
	}
}

void difftest_regcpy(void *regs, int to_reference)
{
	struct DiffRegisters *r = regs;
	if (to_reference) {
		memcpy(cpu.gpr, r->gpr, sizeof(cpu.gpr));
		cpu.pc = r->pc;
	} else {
		memcpy(r->gpr, cpu.gpr, sizeof(cpu.gpr));
		r->pc = cpu.pc;
	}
}

void difftest_memcpy(paddr_t addr, void *data, size_t size, int to_reference)
{
	if (!difftest_memory_contains(addr, size))
		return;

	if (to_reference)
		memcpy(guest_to_host(addr), data, size);
	else
		memcpy(data, guest_to_host(addr), size);
}

void difftest_raise_intr(uint64_t interrupt)
{
	if (interrupt < 64)
		csr.mip |= 1ULL << interrupt;
}
