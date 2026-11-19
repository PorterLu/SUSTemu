/*
 * Reference model for differential testing of an RV64IMA hart with machine,
 * supervisor and user mode and Sv39 paging.
 *
 * The device under test (DUT) reports every instruction it retires. For each
 * of them difftest_step() executes instructions here until one retires as
 * well, taking on its own every exception that lies in between. The caller
 * then compares the retired pc and the registers.
 *
 * The reference only owns the RAM. It cannot know a value that depends on a
 * device or on the timing of the DUT (a load from a device, a counter, the
 * pending interrupt bits), so the caller passes the value the DUT wrote to the
 * destination register and the reference adopts it in exactly those cases.
 * Interrupts are asynchronous: the caller reports the one the DUT took with
 * difftest_raise_intr() and the reference checks that it was allowed.
 */
#include <common.h>
#include <cpu/ir.h>
#include <csr.h>
#include <pmem.h>
#include <reg.h>
#include <state.h>
#include <string.h>

struct DiffRegisters {
	word_t gpr[32];
	word_t pc;
};

struct DiffTrap {
	word_t cause;
	word_t epc;
	word_t tval;
};

/* What the last retired instruction was and what it stored. */
struct DiffStep {
	uint64_t inst;
	uint64_t store_width;	/* bytes, 0 when it did not store */
	uint64_t store_addr;	/* physical */
	uint64_t store_data;
};

#define MAX_TRAPS 8
static struct DiffTrap traps[MAX_TRAPS];
static int trap_count;
static struct DiffStep last_step;
/* LR sets the reservation, any store clears it; the address is not compared */
static bool reservation;

enum { ACCESS_FETCH, ACCESS_LOAD, ACCESS_STORE };

static bool in_ram(paddr_t addr, size_t size)
{
	return addr >= MBASE && size <= MSIZE && addr - MBASE <= MSIZE - size;
}

static void take_exception(word_t cause, word_t tval)
{
	if (trap_count < MAX_TRAPS) {
		traps[trap_count].cause = cause;
		traps[trap_count].epc = cpu.pc;
		traps[trap_count].tval = tval;
	}
	trap_count++;
	cpu.pc = csr_trap(cause, cpu.pc, tval);
}

/* Sv39 address translation. The accessed and dirty bits are never updated: a
 * leaf with A clear, or with D clear on a store, faults. */
static bool translate(vaddr_t va, int access, paddr_t *pa)
{
	uint64_t priv = priv_level;
	if (access != ACCESS_FETCH && (csr.mstatus & MSTATUS_MPRV))
		priv = (csr.mstatus & MSTATUS_MPP) >> 11;

	*pa = va;
	if (priv == MACHINE || (csr.satp >> 60) != 8)
		return true;

	/* bits 63:39 must all equal bit 38 */
	if ((vaddr_t)(((int64_t)(va << 25)) >> 25) != va)
		return false;

	paddr_t table = (csr.satp & 0xfffffffffffULL) << 12;
	for (int level = 2; level >= 0; level--) {
		const paddr_t pte_addr = table + ((va >> (12 + 9 * level)) & 0x1ff) * 8;
		const uint64_t pte = in_ram(pte_addr, 8)
			? host_read(guest_to_host(pte_addr), 8) : 0;
		const bool r = pte & 2, w = pte & 4, x = pte & 8, u = pte & 16;
		const uint64_t ppn = (pte >> 10) & 0xfffffffffffULL;

		if (!(pte & 1) || (!r && w))
			return false;
		if (!r && !x) {
			table = ppn << 12;
			continue;
		}

		/* a superpage must be aligned */
		if (level && (ppn & ((1ULL << (9 * level)) - 1)))
			return false;
		if (priv == USER && !u)
			return false;
		/* supervisor reaches user pages only for data and with SUM */
		if (priv == SUPERVISOR && u &&
		    (access == ACCESS_FETCH || !(csr.mstatus & MSTATUS_SUM)))
			return false;
		if (access == ACCESS_FETCH && !x)
			return false;
		if (access == ACCESS_LOAD &&
		    !(r || (x && (csr.mstatus & MSTATUS_MXR))))
			return false;
		if (access == ACCESS_STORE && !w)
			return false;
		if (!(pte & 0x40) || (access == ACCESS_STORE && !(pte & 0x80)))
			return false;

		const uint64_t offset_mask = (1ULL << (12 + 9 * level)) - 1;
		*pa = ((ppn << 12) & ~offset_mask) | (va & offset_mask);
		return true;
	}
	return false;
}

static word_t sign_extend(word_t value, int width)
{
	switch (width) {
	case 1: return (word_t)(int64_t)(int8_t)value;
	case 2: return (word_t)(int64_t)(int16_t)value;
	case 4: return (word_t)(int64_t)(int32_t)value;
	}
	return value;
}

static void set_gpr(int rd, word_t value)
{
	if (rd)
		cpu.gpr[rd] = value;
}

/* A store to the RAM is carried out; one to a device is only recorded. */
static void store(paddr_t pa, int width, word_t value)
{
	if (in_ram(pa, width))
		host_write(guest_to_host(pa), width, value);
	last_step.store_width = width;
	last_step.store_addr = pa;
	last_step.store_data = width == 8 ? value : value & ((1ULL << (8 * width)) - 1);
	reservation = false;
}

static bool exec_load(uint32_t raw, word_t dut_value)
{
	static const int widths[8] = {1, 2, 4, 8, 1, 2, 4, 0};
	const int funct3 = (raw >> 12) & 7;
	const int width = widths[funct3];
	const vaddr_t va = cpu.gpr[(raw >> 15) & 0x1f] + (word_t)((int32_t)raw >> 20);
	paddr_t pa;

	if (!width) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	if (va & (width - 1)) {
		take_exception(CAUSE_MISALIGNED_LOAD, va);
		return false;
	}
	if (!translate(va, ACCESS_LOAD, &pa)) {
		take_exception(CAUSE_LOAD_PAGE_FAULT, va);
		return false;
	}

	word_t value = dut_value;	/* a device read: only the DUT knows */
	if (in_ram(pa, width)) {
		value = host_read(guest_to_host(pa), width);
		if (funct3 < 4)
			value = sign_extend(value, width);
	}
	set_gpr((raw >> 7) & 0x1f, value);
	return true;
}

static bool exec_store(uint32_t raw)
{
	const int funct3 = (raw >> 12) & 7;
	const int width = 1 << funct3;
	const word_t offset = (word_t)(((int32_t)raw >> 25) << 5) | ((raw >> 7) & 0x1f);
	const vaddr_t va = cpu.gpr[(raw >> 15) & 0x1f] + offset;
	paddr_t pa;

	if (funct3 > 3) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	if (va & (width - 1)) {
		take_exception(CAUSE_MISALIGNED_STORE, va);
		return false;
	}
	if (!translate(va, ACCESS_STORE, &pa)) {
		take_exception(CAUSE_STORE_PAGE_FAULT, va);
		return false;
	}

	store(pa, width, cpu.gpr[(raw >> 20) & 0x1f]);
	return true;
}

static bool exec_atomic(uint32_t raw, word_t dut_value)
{
	const int funct3 = (raw >> 12) & 7;
	const int funct5 = raw >> 27;
	const int width = funct3 == 2 ? 4 : 8;
	const int rd = (raw >> 7) & 0x1f;
	const vaddr_t va = cpu.gpr[(raw >> 15) & 0x1f];
	const word_t src = cpu.gpr[(raw >> 20) & 0x1f];
	const bool is_lr = funct5 == 0x02;
	paddr_t pa;

	switch (funct5) {
	case 0x00: case 0x01: case 0x03: case 0x04: case 0x08: case 0x0c:
	case 0x10: case 0x14: case 0x18: case 0x1c:
		break;
	case 0x02:
		if (((raw >> 20) & 0x1f) == 0)
			break;
		/* fall through: LR with a non-zero rs2 field is not an instruction */
	default:
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	if (funct3 != 2 && funct3 != 3) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	if (va & (width - 1)) {
		take_exception(is_lr ? CAUSE_MISALIGNED_LOAD : CAUSE_MISALIGNED_STORE, va);
		return false;
	}
	/* SC and the AMOs are checked as stores even when they do not write */
	if (!translate(va, is_lr ? ACCESS_LOAD : ACCESS_STORE, &pa)) {
		take_exception(is_lr ? CAUSE_LOAD_PAGE_FAULT : CAUSE_STORE_PAGE_FAULT, va);
		return false;
	}

	const bool ram = in_ram(pa, width);
	if (funct5 == 0x03) {				/* SC */
		const bool success = reservation;
		if (success)
			store(pa, width, src);
		set_gpr(rd, success ? 0 : 1);
		reservation = false;
		return true;
	}

	word_t old = dut_value;
	if (ram)
		old = sign_extend(host_read(guest_to_host(pa), width), width);
	if (is_lr) {
		reservation = true;
		set_gpr(rd, old);
		return true;
	}

	word_t result;
	if (width == 4) {
		const int32_t a = (int32_t)old, b = (int32_t)src;
		switch (funct5) {
		case 0x10: result = (word_t)(a < b ? a : b); break;
		case 0x14: result = (word_t)(a > b ? a : b); break;
		case 0x18: result = (uint32_t)a < (uint32_t)b ? (uint32_t)a : (uint32_t)b; break;
		case 0x1c: result = (uint32_t)a > (uint32_t)b ? (uint32_t)a : (uint32_t)b; break;
		default: result = 0; break;
		}
	} else {
		const int64_t a = (int64_t)old, b = (int64_t)src;
		switch (funct5) {
		case 0x10: result = (word_t)(a < b ? a : b); break;
		case 0x14: result = (word_t)(a > b ? a : b); break;
		case 0x18: result = old < src ? old : src; break;
		case 0x1c: result = old > src ? old : src; break;
		default: result = 0; break;
		}
	}
	switch (funct5) {
	case 0x00: result = old + src; break;
	case 0x01: result = src; break;
	case 0x04: result = old ^ src; break;
	case 0x08: result = old | src; break;
	case 0x0c: result = old & src; break;
	}

	store(pa, width, result);
	set_gpr(rd, old);
	return true;
}

/* CSRs whose value depends on the timing of the DUT or on its interrupt
 * lines: a read returns what the DUT returned. */
static bool csr_follows_dut(uint32_t no)
{
	return no == MIP || no == SIP || no == MCYCLE || no == MINSTRET ||
	       no == CYCLE || no == INSTRET;
}

static bool exec_system(uint32_t raw, word_t dut_value)
{
	const int funct3 = (raw >> 12) & 7;
	const int rd = (raw >> 7) & 0x1f;
	const int rs1 = (raw >> 15) & 0x1f;
	const uint32_t no = raw >> 20;

	if (funct3 == 0) {
		if (raw == 0x00000073) {		/* ECALL */
			take_exception(CAUSE_USER_ECALL + priv_level, 0);
			return false;
		}
		if (raw == 0x00100073) {		/* EBREAK */
			take_exception(CAUSE_BREAKPOINT, cpu.pc);
			return false;
		}
		if (raw == 0x30200073 && priv_level == MACHINE) {
			cpu.pc = csr_mret();
			return true;
		}
		if (raw == 0x10200073 && priv_level >= SUPERVISOR) {
			cpu.pc = csr_sret();
			return true;
		}
		/* WFI, and SFENCE.VMA outside user mode: nothing to do here */
		if (raw == 0x10500073 ||
		    ((raw & 0xfe007fff) == 0x12000073 && priv_level >= SUPERVISOR)) {
			cpu.pc += 4;
			return true;
		}
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	if (funct3 == 4) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}

	const int op = funct3 & 3;			/* 1 write, 2 set, 3 clear */
	const word_t operand = (funct3 & 4) ? (word_t)rs1 : cpu.gpr[rs1];
	const bool writes = op == 1 || rs1 != 0;
	word_t old;

	if (!csr_read(no, &old)) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	if (writes) {
		const word_t value = op == 1 ? operand
				   : op == 2 ? (old | operand) : (old & ~operand);
		if (!csr_write(no, value)) {
			take_exception(CAUSE_ILLEGAL_INST, raw);
			return false;
		}
	}
	set_gpr(rd, csr_follows_dut(no) ? dut_value : old);
	cpu.pc += 4;
	return true;
}

/* Execute the instruction at cpu.pc. Returns false when it trapped instead of
 * retiring; cpu.pc then points at the trap handler. */
static bool execute_one(word_t dut_value)
{
	const vaddr_t pc = cpu.pc;
	paddr_t physical_pc;

	if (!translate(pc, ACCESS_FETCH, &physical_pc)) {
		take_exception(CAUSE_FETCH_PAGE_FAULT, pc);
		return false;
	}
	/* a fetch outside the RAM reads zero, which is not an instruction */
	const uint32_t raw = in_ram(physical_pc, 4)
		? (uint32_t)host_read(guest_to_host(physical_pc), 4) : 0;
	last_step.inst = raw;
	last_step.store_width = 0;
	/* only 32-bit encodings exist: no compressed instructions */
	if ((raw & 3) != 3) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}

	switch (raw & 0x7f) {
	case 0x03:
		if (!exec_load(raw, dut_value))
			return false;
		cpu.pc = pc + 4;
		return true;
	case 0x23:
		if (!exec_store(raw))
			return false;
		cpu.pc = pc + 4;
		return true;
	case 0x2f:
		if (!exec_atomic(raw, dut_value))
			return false;
		cpu.pc = pc + 4;
		return true;
	case 0x73:
		return exec_system(raw, dut_value);
	case 0x0f:				/* FENCE, FENCE.I: no caches here */
		if (((raw >> 12) & 7) > 1) {
			take_exception(CAUSE_ILLEGAL_INST, raw);
			return false;
		}
		cpu.pc = pc + 4;
		return true;
	}

	IR_Inst ir;
	ir_decode(raw, pc, pc + 4, &ir);
	if (ir.name[0] == '?') {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	ir_execute(&ir, &cpu);
	/* a taken jump or branch to a misaligned target traps on the jump */
	if (ir.dnpc & 3) {
		take_exception(CAUSE_MISALIGNED_FETCH, ir.dnpc);
		return false;
	}
	ir_writeback(&ir, &cpu);
	cpu.pc = ir.dnpc;
	return true;
}

void difftest_init(int hartid)
{
	memset(&cpu, 0, sizeof(cpu));
	csr_reset();
	csr_hide_custom_counters = true;
	cpu.pc = MBASE;
	g_current_hartid = hartid;
	g_sim_cycles = 0;
	g_sim_instret = 0;
	reservation = false;
	trap_count = 0;
	state = NEMU_RUNNING;
}

/* Execute until one instruction retires and store its pc. Returns the number
 * of exceptions taken on the way (read them with difftest_get_trap), or -1
 * when the reference only keeps trapping. */
int difftest_step(uint64_t dut_value, uint64_t *retired_pc)
{
	trap_count = 0;
	while (trap_count <= MAX_TRAPS) {
		const vaddr_t pc = cpu.pc;
		if (execute_one(dut_value)) {
			cpu.gpr[0] = 0;
			*retired_pc = pc;
			g_sim_cycles++;
			g_sim_instret++;
			return trap_count;
		}
	}
	return -1;
}

void difftest_get_step(void *step)
{
	memcpy(step, &last_step, sizeof(last_step));
}

int difftest_get_trap(int index, void *trap)
{
	if (index < 0 || index >= trap_count || index >= MAX_TRAPS)
		return -1;
	memcpy(trap, &traps[index], sizeof(struct DiffTrap));
	return 0;
}

/* The DUT took interrupt `code`. hw_pending holds the machine-level pending
 * bits (MSIP, MTIP, MEIP), which are wired to devices and unknown here.
 * Returns non-zero when the reference state does not allow that interrupt:
 * 1 not pending and enabled, 2 a higher priority interrupt should be taken. */
int difftest_raise_intr(uint64_t code, uint64_t hw_pending)
{
	const word_t machine_bits = MIP_MSIP | MIP_MTIP | MIP_MEIP;
	const word_t pending = ((csr.mip & ~machine_bits) |
				(hw_pending & machine_bits)) & csr.mie;
	const bool m_enabled = priv_level < MACHINE || (csr.mstatus & MSTATUS_MIE);
	const bool s_enabled = priv_level < SUPERVISOR ||
			       (priv_level == SUPERVISOR && (csr.mstatus & MSTATUS_SIE));
	const word_t allowed = (m_enabled ? pending & ~csr.mideleg : 0) |
			       (s_enabled ? pending & csr.mideleg : 0);
	static const int priority[] = {11, 3, 7, 9, 1, 5};
	int result = 0;

	if (code > 63 || !((allowed >> code) & 1)) {
		result = 1;
	} else {
		for (unsigned i = 0; i < sizeof(priority) / sizeof(priority[0]); i++) {
			if ((allowed >> priority[i]) & 1) {
				if ((uint64_t)priority[i] != code)
					result = 2;
				break;
			}
		}
	}

	cpu.pc = csr_trap(CAUSE_INTERRUPT | code, cpu.pc, 0);
	return result;
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
	if (!in_ram(addr, size))
		return;

	if (to_reference)
		memcpy(guest_to_host(addr), data, size);
	else
		memcpy(data, guest_to_host(addr), size);
}

/* Value of a CSR regardless of the current privilege, for mismatch reports */
uint64_t difftest_get_csr(uint32_t no)
{
	const uint64_t saved = priv_level;
	word_t value = 0;

	priv_level = MACHINE;
	if (no == MCYCLE || no == MINSTRET || !csr_read(no, &value))
		value = 0;
	priv_level = saved;
	return value;
}

uint64_t difftest_get_priv(void)
{
	return priv_level;
}
