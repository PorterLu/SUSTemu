/*
 * Reference model for differential testing of an RV64IMAH hart with machine,
 * supervisor and user mode, the hypervisor extension and Sv39 paging (Sv39x4
 * for the guest-physical stage).
 *
 * The device under test (DUT) reports every instruction it retires. For each
 * of them difftest_step() executes instructions here until one retires as
 * well, taking on its own every exception that lies in between. The caller
 * then compares the retired pc and the registers.
 *
 * The reference only owns the RAM. It cannot know a value that depends on a
 * device or on the timing of the DUT (a load from a device, a counter, the
 * pending bits wired to devices), so the caller passes the value the DUT wrote
 * to the destination register and the reference adopts it in exactly those
 * cases. Interrupts are asynchronous: the caller reports the one the DUT took
 * with difftest_raise_intr() and the reference checks that it was allowed.
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
	word_t tval2;	/* mtval2 or htval: a guest physical address >> 2 */
	word_t tinst;	/* mtinst or htinst */
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
/* Kinds of memory access by the hypervisor load and store instructions: as
 * the guest in hstatus.SPVP, and for HLVX with execute in place of read
 * permission. */
enum { ACCESS_GUEST = 1, ACCESS_EXECUTE = 2 };
/* mtinst/htinst when a guest-page fault hits the read of a VS-stage page
 * table entry: a 64-bit load */
#define TINST_PTE_READ 0x3000

/* Why an address translation failed */
struct Fault {
	bool guest;	/* a guest-page fault of the guest-physical stage */
	bool gva;	/* the address translated was a guest virtual one */
	word_t tval2;
	word_t tinst;
};

static bool in_ram(paddr_t addr, size_t size)
{
	return addr >= MBASE && size <= MSIZE && addr - MBASE <= MSIZE - size;
}

static void take_trap(word_t cause, word_t tval, word_t tval2, word_t tinst, bool gva)
{
	if (trap_count < MAX_TRAPS) {
		traps[trap_count].cause = cause;
		traps[trap_count].epc = cpu.pc;
		traps[trap_count].tval = tval;
		traps[trap_count].tval2 = tval2;
		traps[trap_count].tinst = tinst;
	}
	trap_count++;
	cpu.pc = csr_trap_guest(cause, cpu.pc, tval, tval2, tinst, gva);
}

static void take_exception(word_t cause, word_t tval)
{
	take_trap(cause, tval, 0, 0, false);
}

/* An exception of a memory access at a virtual address: page faults carry
 * what the translation reported, the others only the kind of the address. */
static void take_fault(word_t cause, word_t va, const struct Fault *fault)
{
	if (fault->guest)
		cause += CAUSE_FETCH_GUEST_PAGE_FAULT - CAUSE_FETCH_PAGE_FAULT;
	take_trap(cause, va, fault->tval2, fault->tinst, fault->gva);
}

static uint64_t read_pte(paddr_t addr)
{
	return in_ram(addr, 8) ? host_read(guest_to_host(addr), 8) : 0;
}

/* Guest-physical stage: Sv39x4, a guest physical address of 41 bits behind a
 * root table of 2048 entries. Every access counts as a user access. The reads
 * of the VS-stage page tables (`implicit`) need read permission. */
static bool translate_guest(paddr_t gpa, int access, int kind, bool implicit, paddr_t *pa)
{
	*pa = gpa;
	if ((csr.hgatp >> 60) != 8)
		return true;
	if (gpa >> 41)
		return false;

	paddr_t table = (csr.hgatp & 0xfffffffffffULL) << 12;
	for (int level = 2; level >= 0; level--) {
		const uint64_t index = (gpa >> (12 + 9 * level)) & (level == 2 ? 0x7ff : 0x1ff);
		const uint64_t pte = read_pte(table + index * 8);
		const bool r = pte & 2, w = pte & 4, x = pte & 8, u = pte & 16;
		const uint64_t ppn = (pte >> 10) & 0xfffffffffffULL;

		if (!(pte & 1) || (!r && w))
			return false;
		if (!r && !x) {
			table = ppn << 12;
			continue;
		}

		if (level && (ppn & ((1ULL << (9 * level)) - 1)))
			return false;
		if (!u)
			return false;
		if (implicit) {
			if (!r)
				return false;
		} else if (access == ACCESS_FETCH || (kind & ACCESS_EXECUTE)) {
			if (!x)
				return false;
		} else if (access == ACCESS_LOAD) {
			if (!(r || (x && (csr.mstatus & MSTATUS_MXR))))
				return false;
		} else if (!w) {
			return false;
		}
		if (!(pte & 0x40) || (access == ACCESS_STORE && !implicit && !(pte & 0x80)))
			return false;

		const uint64_t offset_mask = (1ULL << (12 + 9 * level)) - 1;
		*pa = ((ppn << 12) & ~offset_mask) | (gpa & offset_mask);
		return true;
	}
	return false;
}

/* Address translation of an access in the current mode. Sv39 by satp, or for
 * a guest Sv39 by vsatp followed by the guest-physical stage, which also
 * translates the address of every VS-stage page table entry. The accessed and
 * dirty bits are never updated: a leaf with A clear, or with D clear on a
 * store, faults. */
static bool translate(vaddr_t va, int access, int kind, paddr_t *pa, struct Fault *fault)
{
	uint64_t priv = priv_level;
	bool virt = virt_mode;

	if (kind & ACCESS_GUEST) {
		priv = (csr.hstatus & HSTATUS_SPVP) ? SUPERVISOR : USER;
		virt = true;
	} else if (access != ACCESS_FETCH && (csr.mstatus & MSTATUS_MPRV)) {
		priv = (csr.mstatus & MSTATUS_MPP) >> 11;
		virt = priv != MACHINE && (csr.mstatus & MSTATUS_MPV);
	}

	const word_t atp = virt ? csr.vsatp : csr.satp;
	const word_t status = virt ? csr.vsstatus : csr.mstatus;
	const bool sum = status & MSTATUS_SUM;
	/* sstatus.MXR of the hypervisor also applies to the pages of a guest */
	const bool mxr = (status | csr.mstatus) & MSTATUS_MXR;

	fault->guest = false;
	fault->gva = virt;
	fault->tval2 = 0;
	fault->tinst = 0;
	*pa = va;
	if (priv == MACHINE)
		return true;

	paddr_t gpa = va;
	if ((atp >> 60) == 8) {
		bool found = false;
		paddr_t table = (atp & 0xfffffffffffULL) << 12;

		/* bits 63:39 must all equal bit 38 */
		if ((vaddr_t)(((int64_t)(va << 25)) >> 25) != va)
			return false;

		for (int level = 2; level >= 0 && !found; level--) {
			const paddr_t pte_gpa = table + ((va >> (12 + 9 * level)) & 0x1ff) * 8;
			paddr_t pte_addr = pte_gpa;

			if (virt && !translate_guest(pte_gpa, ACCESS_LOAD, 0, true, &pte_addr)) {
				fault->guest = true;
				fault->tval2 = pte_gpa >> 2;
				fault->tinst = TINST_PTE_READ;
				return false;
			}

			const uint64_t pte = read_pte(pte_addr);
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
			if (priv == SUPERVISOR && u && (access == ACCESS_FETCH || !sum))
				return false;
			if (access == ACCESS_FETCH || (kind & ACCESS_EXECUTE)) {
				if (!x)
					return false;
			} else if (access == ACCESS_LOAD) {
				if (!(r || (x && mxr)))
					return false;
			} else if (!w) {
				return false;
			}
			if (!(pte & 0x40) || (access == ACCESS_STORE && !(pte & 0x80)))
				return false;

			const uint64_t offset_mask = (1ULL << (12 + 9 * level)) - 1;
			gpa = ((ppn << 12) & ~offset_mask) | (va & offset_mask);
			found = true;
		}
		if (!found)
			return false;
	}

	*pa = gpa;
	if (virt && !translate_guest(gpa, access, kind, false, pa)) {
		fault->guest = true;
		fault->tval2 = gpa >> 2;
		return false;
	}
	return true;
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

/* Load `width` bytes at va into rd. A load from a device returns the value
 * of the DUT. */
static bool load(vaddr_t va, int width, bool is_signed, int kind, int rd, word_t dut_value)
{
	struct Fault fault;
	paddr_t pa;

	if (va & (width - 1)) {
		translate(va, ACCESS_LOAD, kind, &pa, &fault);
		take_trap(CAUSE_MISALIGNED_LOAD, va, 0, 0, fault.gva);
		return false;
	}
	if (!translate(va, ACCESS_LOAD, kind, &pa, &fault)) {
		take_fault(CAUSE_LOAD_PAGE_FAULT, va, &fault);
		return false;
	}

	word_t value = dut_value;
	if (in_ram(pa, width)) {
		value = host_read(guest_to_host(pa), width);
		if (is_signed)
			value = sign_extend(value, width);
	}
	set_gpr(rd, value);
	return true;
}

static bool store_at(vaddr_t va, int width, int kind, word_t value)
{
	struct Fault fault;
	paddr_t pa;

	if (va & (width - 1)) {
		translate(va, ACCESS_STORE, kind, &pa, &fault);
		take_trap(CAUSE_MISALIGNED_STORE, va, 0, 0, fault.gva);
		return false;
	}
	if (!translate(va, ACCESS_STORE, kind, &pa, &fault)) {
		take_fault(CAUSE_STORE_PAGE_FAULT, va, &fault);
		return false;
	}
	store(pa, width, value);
	return true;
}

static bool exec_load(uint32_t raw, word_t dut_value)
{
	static const int widths[8] = {1, 2, 4, 8, 1, 2, 4, 0};
	const int funct3 = (raw >> 12) & 7;
	const vaddr_t va = cpu.gpr[(raw >> 15) & 0x1f] + (word_t)((int32_t)raw >> 20);

	if (!widths[funct3]) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	return load(va, widths[funct3], funct3 < 4, 0, (raw >> 7) & 0x1f, dut_value);
}

static bool exec_store(uint32_t raw)
{
	const int funct3 = (raw >> 12) & 7;
	const word_t offset = (word_t)(((int32_t)raw >> 25) << 5) | ((raw >> 7) & 0x1f);
	const vaddr_t va = cpu.gpr[(raw >> 15) & 0x1f] + offset;

	if (funct3 > 3) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	return store_at(va, 1 << funct3, 0, cpu.gpr[(raw >> 20) & 0x1f]);
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
	struct Fault fault;
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
	/* SC and the AMOs are checked as stores even when they do not write */
	const int access = is_lr ? ACCESS_LOAD : ACCESS_STORE;
	if (va & (width - 1)) {
		translate(va, access, 0, &pa, &fault);
		take_trap(is_lr ? CAUSE_MISALIGNED_LOAD : CAUSE_MISALIGNED_STORE, va, 0, 0,
			  fault.gva);
		return false;
	}
	if (!translate(va, access, 0, &pa, &fault)) {
		take_fault(is_lr ? CAUSE_LOAD_PAGE_FAULT : CAUSE_STORE_PAGE_FAULT, va, &fault);
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

/* The bits of a CSR that depend on the timing of the DUT or on its interrupt
 * lines: a read returns them as the DUT returned them. */
static word_t csr_dut_bits(uint32_t no)
{
	switch (no) {
	case MIP:
		return MIP_MSIP | MIP_MTIP | MIP_MEIP;
	case MCYCLE: case MINSTRET: case CYCLE: case INSTRET:
		return ~0ULL;
	}
	return 0;
}

/* The privileged instructions without operands. Returns 0 when the current
 * mode may execute one, or the cause of the exception it raises instead: a
 * guest gets a virtual-instruction exception where HS mode would succeed. */
static int system_check(uint32_t raw)
{
	const bool user = priv_level == USER, hs = priv_level == SUPERVISOR && !virt_mode;
	const bool tw = csr.mstatus & MSTATUS_TW;

	if (raw == 0x30200073)				/* MRET */
		return priv_level == MACHINE ? 0 : CAUSE_ILLEGAL_INST;
	if (raw == 0x10200073) {			/* SRET */
		if (virt_mode)
			return user || (csr.hstatus & HSTATUS_VTSR) ? CAUSE_VIRTUAL_INST : 0;
		return user || (hs && (csr.mstatus & MSTATUS_TSR)) ? CAUSE_ILLEGAL_INST : 0;
	}
	if (raw == 0x10500073) {			/* WFI */
		if (priv_level == MACHINE)
			return 0;
		if (tw)
			return CAUSE_ILLEGAL_INST;
		if (virt_mode)
			return user || (csr.hstatus & HSTATUS_VTW) ? CAUSE_VIRTUAL_INST : 0;
		return user ? CAUSE_ILLEGAL_INST : 0;
	}
	if ((raw & 0xfe007fff) == 0x12000073) {		/* SFENCE.VMA */
		if (virt_mode)
			return user || (csr.hstatus & HSTATUS_VTVM) ? CAUSE_VIRTUAL_INST : 0;
		return user || (hs && (csr.mstatus & MSTATUS_TVM)) ? CAUSE_ILLEGAL_INST : 0;
	}
	if ((raw & 0xfe007fff) == 0x22000073) {		/* HFENCE.VVMA */
		if (virt_mode)
			return CAUSE_VIRTUAL_INST;
		return user ? CAUSE_ILLEGAL_INST : 0;
	}
	if ((raw & 0xfe007fff) == 0x62000073) {		/* HFENCE.GVMA */
		if (virt_mode)
			return CAUSE_VIRTUAL_INST;
		return user || (hs && (csr.mstatus & MSTATUS_TVM)) ? CAUSE_ILLEGAL_INST : 0;
	}
	return CAUSE_ILLEGAL_INST;
}

/* HLV, HLVX and HSV: a load or store at rs1 as the guest in hstatus.SPVP */
static bool exec_hypervisor_access(uint32_t raw, word_t dut_value)
{
	const int funct7 = raw >> 25;
	const int rs2 = (raw >> 20) & 0x1f;
	const int rd = (raw >> 7) & 0x1f;
	const vaddr_t va = cpu.gpr[(raw >> 15) & 0x1f];
	const int width = 1 << ((funct7 >> 1) & 3);
	const bool is_store = funct7 & 1;
	bool valid;

	if ((funct7 & 0x78) != 0x30)
		valid = false;
	else if (is_store)
		valid = rd == 0;
	else if (rs2 == 0)
		valid = true;				/* HLV.B/H/W/D */
	else if (rs2 == 1)
		valid = width < 8;			/* HLV.BU/HU/WU */
	else
		valid = rs2 == 3 && (width == 2 || width == 4);	/* HLVX.HU/WU */

	if (!valid) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	if (virt_mode) {
		take_exception(CAUSE_VIRTUAL_INST, raw);
		return false;
	}
	if (priv_level == USER && !(csr.hstatus & HSTATUS_HU)) {
		take_exception(CAUSE_ILLEGAL_INST, raw);
		return false;
	}

	if (is_store)
		return store_at(va, width, ACCESS_GUEST, cpu.gpr[rs2]);
	return load(va, width, rs2 == 0, ACCESS_GUEST | (rs2 == 3 ? ACCESS_EXECUTE : 0),
		    rd, dut_value);
}

static bool exec_system(uint32_t raw, word_t dut_value)
{
	const int funct3 = (raw >> 12) & 7;
	const int rd = (raw >> 7) & 0x1f;
	const int rs1 = (raw >> 15) & 0x1f;
	const uint32_t no = raw >> 20;

	if (funct3 == 0) {
		if (raw == 0x00000073) {		/* ECALL */
			take_exception(priv_level == MACHINE ? CAUSE_MACHINE_ECALL
				       : priv_level == USER ? CAUSE_USER_ECALL
				       : virt_mode ? CAUSE_VIRTUAL_SUPERVISOR_ECALL
				       : CAUSE_SUPERVISOR_ECALL, 0);
			return false;
		}
		if (raw == 0x00100073) {		/* EBREAK */
			take_trap(CAUSE_BREAKPOINT, cpu.pc, 0, 0, virt_mode);
			return false;
		}

		const int cause = system_check(raw);
		if (cause) {
			take_exception(cause, raw);
			return false;
		}
		if (raw == 0x30200073) {
			cpu.pc = csr_mret();
			return true;
		}
		if (raw == 0x10200073) {
			cpu.pc = csr_sret();
			return true;
		}
		/* WFI and the fences of the translation caches: nothing to do */
		cpu.pc += 4;
		return true;
	}
	if (funct3 == 4) {
		if (!exec_hypervisor_access(raw, dut_value))
			return false;
		cpu.pc += 4;
		return true;
	}

	const int op = funct3 & 3;			/* 1 write, 2 set, 3 clear */
	const word_t operand = (funct3 & 4) ? (word_t)rs1 : cpu.gpr[rs1];
	const bool writes = op == 1 || rs1 != 0;
	const int cause = csr_check(no, writes);
	word_t old;

	if (cause || !csr_read(no, &old)) {
		take_exception(cause ? cause : CAUSE_ILLEGAL_INST, raw);
		return false;
	}
	if (writes) {
		const word_t value = op == 1 ? operand
				   : op == 2 ? (old | operand) : (old & ~operand);
		csr_write(no, value);
	}
	const word_t dut_bits = csr_dut_bits(no);
	set_gpr(rd, (old & ~dut_bits) | (dut_value & dut_bits));
	cpu.pc += 4;
	return true;
}

/* Execute the instruction at cpu.pc. Returns false when it trapped instead of
 * retiring; cpu.pc then points at the trap handler. */
static bool execute_one(word_t dut_value)
{
	const vaddr_t pc = cpu.pc;
	struct Fault fault;
	paddr_t physical_pc;

	if (!translate(pc, ACCESS_FETCH, 0, &physical_pc, &fault)) {
		take_fault(CAUSE_FETCH_PAGE_FAULT, pc, &fault);
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
		take_trap(CAUSE_MISALIGNED_FETCH, ir.dnpc, 0, 0, virt_mode);
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

static word_t pending_bits(uint64_t hw_pending)
{
	const word_t machine_bits = MIP_MSIP | MIP_MTIP | MIP_MEIP;
	return (csr.mip & ~machine_bits) | (hw_pending & machine_bits);
}

/* The cause code of the interrupt the reference would take now, or -1.
 * hw_pending holds the machine-level pending bits (MSIP, MTIP, MEIP), which
 * are wired to devices and unknown here. */
int difftest_pending_intr(uint64_t hw_pending)
{
	const int number = csr_pending_interrupt(pending_bits(hw_pending));
	return number < 0 ? -1 : csr_interrupt_code(number);
}

/* The DUT took an interrupt and stored the cause code `code`. Returns non-zero
 * when the reference state does not allow that: 1 no interrupt is pending and
 * enabled, 2 another one has priority. */
int difftest_raise_intr(uint64_t code, uint64_t hw_pending)
{
	const int number = csr_pending_interrupt(pending_bits(hw_pending));

	if (number < 0) {
		cpu.pc = csr_trap(CAUSE_INTERRUPT | code, cpu.pc, 0);
		return 1;
	}
	const int result = (uint64_t)csr_interrupt_code(number) == code ? 0 : 2;
	cpu.pc = csr_trap(CAUSE_INTERRUPT | number, cpu.pc, 0);
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

/* Value of a CSR regardless of the current mode, for mismatch reports */
uint64_t difftest_get_csr(uint32_t no)
{
	const uint64_t saved = priv_level;
	const bool saved_virt = virt_mode;
	word_t value = 0;

	priv_level = MACHINE;
	virt_mode = false;
	if (no == MCYCLE || no == MINSTRET || !csr_read(no, &value))
		value = 0;
	priv_level = saved;
	virt_mode = saved_virt;
	return value;
}

/* The privilege level, with bit 2 set in VS and VU mode */
uint64_t difftest_get_priv(void)
{
	return priv_level | (virt_mode ? 4 : 0);
}
