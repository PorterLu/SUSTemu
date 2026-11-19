#include <csr.h>
#include <debug.h>
#include <state.h>
#include <utils.h>
#include <string.h>
#include <memory/cache.h>

CSR csr;
uint64_t priv_level = 3;
bool virt_mode = false;
uint64_t g_sim_cycles  = 0;
uint64_t g_sim_instret = 0;
int      g_current_hartid = 0;
uint8_t pmpcfg[PMP_NUMBER_IN_SUSTEMU];
word_t pmpaddr[PMP_NUMBER_IN_SUSTEMU];
uint8_t pmp_on_count = 0;
bool csr_hide_custom_counters = false;

/* RV64IMAH with supervisor and user mode */
#define MISA_VALUE 0x8000000000141181ULL

/* writable bits of mstatus and the part of it that sstatus shows; vsstatus
 * has the bits of sstatus */
#define MSTATUS_WMASK (MSTATUS_SIE | MSTATUS_MIE | MSTATUS_SPIE | MSTATUS_MPIE | \
		       MSTATUS_SPP | MSTATUS_MPP | MSTATUS_MPRV | MSTATUS_SUM | \
		       MSTATUS_MXR | MSTATUS_TVM | MSTATUS_TW | MSTATUS_TSR | \
		       MSTATUS_GVA | MSTATUS_MPV)
#define SSTATUS_WMASK (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_SUM | \
		       MSTATUS_MXR)
#define HSTATUS_WMASK (HSTATUS_GVA | HSTATUS_SPV | HSTATUS_SPVP | HSTATUS_HU | \
		       HSTATUS_VTVM | HSTATUS_VTW | HSTATUS_VTSR)
/* UXL = SXL = 2: both lower modes are 64 bit, and so is VS mode (VSXL) */
#define MSTATUS_XL    0x0000000a00000000ULL
#define HSTATUS_VSXL  0x0000000200000000ULL

#define S_INTERRUPTS  (MIP_SSIP | MIP_STIP | MIP_SEIP)
#define M_INTERRUPTS  (MIP_MSIP | MIP_MTIP | MIP_MEIP)
/* The VS-level interrupts are always delegated to HS mode: these bits of
 * mideleg read as one. There are no guest external interrupts (GEILEN = 0). */
#define VS_INTERRUPTS (MIP_VSSIP | MIP_VSTIP | MIP_VSEIP)
/* exceptions that can be delegated: everything but an ecall from M-mode */
#define MEDELEG_WMASK 0xf0f7ffULL
/* HS mode keeps its own ecalls, those of VS mode, guest-page faults and
 * virtual-instruction exceptions */
#define HEDELEG_WMASK 0xb1ffULL
/* Sv39x4 or bare, no VMID bits; the root page table is 16 KiB */
#define HGATP_PPN     0x00000ffffffffffcULL
/* interrupt and exception code of a cause register */
#define CAUSE_WMASK   (CAUSE_INTERRUPT | 0x1f)

void csr_reset(void)
{
	memset(&csr, 0, sizeof(csr));
	memset(pmpcfg, 0, sizeof(pmpcfg));
	memset(pmpaddr, 0, sizeof(pmpaddr));
	pmp_on_count = 0;
	priv_level = MACHINE;
	virt_mode = false;
}

void mret_priv_transfer(){
	csr_mret();
}

void exception_priv_transfer(){
	csr.mstatus = (csr.mstatus & ~MSTATUS_MPP) | (priv_level << 11);
	priv_level = MACHINE;
	csr.mstatus = (csr.mstatus & ~MSTATUS_MPIE) |
		      ((csr.mstatus & MSTATUS_MIE) ? MSTATUS_MPIE : 0);
	csr.mstatus &= ~MSTATUS_MIE;
}

bool is_pmp_on(uint8_t pmp_id){
	// 修正：检查 A 字段是否不为 OFF (00)
	return (pmpcfg[pmp_id] & PMP_A) != PMP_A_OFF;
}

void set_pmpcfg(uint8_t id, uint8_t data){
	bool old_on = is_pmp_on(id);
	pmpcfg[id] = data;
	bool new_on = is_pmp_on(id);

	if (old_on && !new_on) {
		pmp_on_count -= 1;
	} else if (!old_on && new_on) {
		pmp_on_count += 1;
	}
}

/* A CSR number encodes the lowest privilege that may access it in bits 9:8
 * and is read-only when bits 11:10 are both set. */
static bool csr_implemented(uint32_t no)
{
	if (no >= PMPCFG0 && no <= PMPCFG3) return true;
	if (no >= PMPADDR0 && no <= PMPADDR15) return true;
	if (no >= MHPMEVENT3 && no <= MHPMEVENT6) return true;
	if (no >= MHPMCOUNTER3 && no <= MHPMCOUNTER6) return true;
	if (no >= L1D_HITS && no <= L2_MISSES) return !csr_hide_custom_counters;
	switch (no) {
	case SSTATUS: case SIE: case STVEC: case SCOUNTEREN: case SENVCFG:
	case SSCRATCH: case SEPC: case SCAUSE: case STVAL: case SIP: case SATP:
	case MSTATUS: case MISA: case MEDELEG: case MIDELEG: case MIE:
	case MTVEC: case MCOUNTERN: case MENVCFG: case MCOUNTINHIBIT:
	case MSCRATCH: case MEPC: case MCAUSE: case MTVAL: case MIP:
	case MSECCFG: case TSELECT: case TDATA1: case TDATA2: case TDATA3:
	case MCONTEXT: case MCYCLE: case MINSTRET:
	case MVENDORID: case MARCHID: case MIMPID: case MHARTID: case MCONFIGPTR:
	case CYCLE: case INSTRET:
	case HSTATUS: case HEDELEG: case HIDELEG: case HIE: case HCOUNTEREN:
	case HGEIE: case HENVCFG: case HTVAL: case HIP: case HVIP: case HTINST:
	case HGATP: case HGEIP: case MTINST: case MTVAL2:
	case VSSTATUS: case VSIE: case VSTVEC: case VSSCRATCH: case VSEPC:
	case VSCAUSE: case VSTVAL: case VSIP: case VSATP:
		return true;
	}
	return false;
}

/* In VS and VU mode these supervisor CSRs stand for their VS copies, which
 * have the same number with bits 9:8 changed from 1 to 2. */
static uint32_t csr_target(uint32_t no)
{
	if (!virt_mode)
		return no;
	switch (no) {
	case SSTATUS: case SIE: case STVEC: case SSCRATCH: case SEPC:
	case SCAUSE: case STVAL: case SIP: case SATP:
		return no ^ 0x300;
	}
	return no;
}

static bool csr_read_only(uint32_t no)
{
	return ((no >> 10) & 3) == 3;
}

int csr_check(uint32_t no, bool is_write)
{
	/* bits 9:8 of the number: 0 user, 1 supervisor, 2 hypervisor and VS, 3
	 * machine. HS mode is the level that reaches the hypervisor CSRs. */
	const uint32_t level = (no >> 8) & 3;

	if (!csr_implemented(csr_target(no)))
		return CAUSE_ILLEGAL_INST;
	if (is_write && csr_read_only(no))
		return CAUSE_ILLEGAL_INST;

	if (!virt_mode) {
		const uint32_t have = priv_level == SUPERVISOR ? 2 : priv_level;
		if (level > have)
			return CAUSE_ILLEGAL_INST;
		if (priv_level == SUPERVISOR && (csr.mstatus & MSTATUS_TVM) &&
		    (no == SATP || no == HGATP))
			return CAUSE_ILLEGAL_INST;
	} else {
		/* what HS mode could do is a virtual-instruction exception here */
		if (level == 3)
			return CAUSE_ILLEGAL_INST;
		if (level == 2 || (level == 1 && priv_level == USER))
			return CAUSE_VIRTUAL_INST;
		if (no == SATP && (csr.hstatus & HSTATUS_VTVM))
			return CAUSE_VIRTUAL_INST;
	}

	/* the unprivileged counters are gated by mcounteren, by hcounteren for
	 * a guest and by scounteren for user mode */
	if (no >= CYCLE && no <= 0xc1f && priv_level < MACHINE) {
		const int bit = no - CYCLE;
		if (!((csr.mcounteren >> bit) & 1))
			return CAUSE_ILLEGAL_INST;
		if (virt_mode && !((csr.hcounteren >> bit) & 1))
			return CAUSE_VIRTUAL_INST;
		if (priv_level == USER && !((csr.scounteren >> bit) & 1))
			return CAUSE_ILLEGAL_INST;
	}
	return 0;
}

static bool csr_accessible(uint32_t no, bool is_write)
{
	return csr_check(no, is_write) == 0;
}

bool csr_read(uint32_t no, word_t *value)
{
	if (!csr_accessible(no, false))
		return false;
	no = csr_target(no);

	if (no >= PMPCFG0 && no <= PMPCFG3) {
		*value = csr.pmpcfg[no - PMPCFG0];
		return true;
	}
	if (no >= PMPADDR0 && no <= PMPADDR15) {
		*value = pmpaddr[no - PMPADDR0];
		return true;
	}
	if (no >= MHPMEVENT3 && no <= MHPMEVENT6) {
		*value = csr.mhpmevent[no - MHPMEVENT3];
		return true;
	}
	if (no >= MHPMCOUNTER3 && no <= MHPMCOUNTER6) {
		*value = csr.mhpmcounter[no - MHPMCOUNTER3];
		return true;
	}

	switch (no) {
	case MSTATUS: *value = (csr.mstatus & MSTATUS_WMASK) | MSTATUS_XL; break;
	case SSTATUS: *value = (csr.mstatus & SSTATUS_WMASK) |
			       (MSTATUS_XL & MSTATUS_UXL); break;
	case MISA: *value = MISA_VALUE; break;
	case MEDELEG: *value = csr.medeleg; break;
	case MIDELEG: *value = csr.mideleg | VS_INTERRUPTS; break;
	case MIE: *value = csr.mie; break;
	case SIE: *value = csr.mie & csr.mideleg; break;
	case HIE: *value = csr.mie & VS_INTERRUPTS; break;
	case VSIE: *value = (csr.mie & csr.hideleg) >> 1; break;
	case MIP: *value = csr.mip; break;
	case SIP: *value = csr.mip & csr.mideleg; break;
	case HIP: case HVIP: *value = csr.mip & VS_INTERRUPTS; break;
	case VSIP: *value = (csr.mip & csr.hideleg) >> 1; break;
	case HSTATUS: *value = (csr.hstatus & HSTATUS_WMASK) | HSTATUS_VSXL; break;
	case HEDELEG: *value = csr.hedeleg; break;
	case HIDELEG: *value = csr.hideleg; break;
	case HCOUNTEREN: *value = csr.hcounteren; break;
	case HENVCFG: *value = csr.henvcfg; break;
	case HTVAL: *value = csr.htval; break;
	case HTINST: *value = csr.htinst; break;
	case HGATP: *value = csr.hgatp; break;
	case HGEIE: case HGEIP: *value = 0; break;
	case MTVAL2: *value = csr.mtval2; break;
	case MTINST: *value = csr.mtinst; break;
	case VSSTATUS: *value = (csr.vsstatus & SSTATUS_WMASK) |
				(MSTATUS_XL & MSTATUS_UXL); break;
	case VSTVEC: *value = csr.vstvec; break;
	case VSSCRATCH: *value = csr.vsscratch; break;
	case VSEPC: *value = csr.vsepc; break;
	case VSCAUSE: *value = csr.vscause; break;
	case VSTVAL: *value = csr.vstval; break;
	case VSATP: *value = csr.vsatp; break;
	case MTVEC: *value = csr.mtvec; break;
	case STVEC: *value = csr.stvec; break;
	case MCOUNTERN: *value = csr.mcounteren; break;
	case SCOUNTEREN: *value = csr.scounteren; break;
	case MENVCFG: *value = csr.menvcfg; break;
	case SENVCFG: *value = csr.senvcfg; break;
	case MSECCFG: *value = csr.mseccfg; break;
	case MCOUNTINHIBIT: *value = csr.mcountinhibit; break;
	case MSCRATCH: *value = csr.mscratch; break;
	case SSCRATCH: *value = csr.sscratch; break;
	case MEPC: *value = csr.mepc; break;
	case SEPC: *value = csr.sepc; break;
	case MCAUSE: *value = csr.mcause; break;
	case SCAUSE: *value = csr.scause; break;
	case MTVAL: *value = csr.mtval; break;
	case STVAL: *value = csr.stval; break;
	case SATP: *value = csr.satp; break;
	case MHARTID: *value = (word_t)g_current_hartid; break;
	case MVENDORID: case MARCHID: case MIMPID: case MCONFIGPTR:
	case TSELECT: case TDATA1: case TDATA2: case TDATA3: case MCONTEXT:
		*value = 0; break;
	case MCYCLE: case CYCLE: *value = g_sim_cycles; break;
	case MINSTRET: case INSTRET: *value = g_sim_instret; break;
	case L1D_HITS:   *value = L1D_cache ? (word_t)L1D_cache->hits   : 0; break;
	case L1D_MISSES: *value = L1D_cache ? (word_t)L1D_cache->misses : 0; break;
	case L1I_HITS:   *value = L1I_cache ? (word_t)L1I_cache->hits   : 0; break;
	case L1I_MISSES: *value = L1I_cache ? (word_t)L1I_cache->misses : 0; break;
	case L2_HITS:    *value = L2_cache  ? (word_t)L2_cache->hits    : 0; break;
	case L2_MISSES:  *value = L2_cache  ? (word_t)L2_cache->misses  : 0; break;
	default: return false;
	}
	return true;
}

bool csr_write(uint32_t no, word_t data)
{
	if (!csr_accessible(no, true))
		return false;
	no = csr_target(no);

	if (no >= PMPCFG0 && no <= PMPCFG3) {
		csr.pmpcfg[no - PMPCFG0] = data;
		/* RV64 keeps entries 0-7 in pmpcfg0 and 8-15 in pmpcfg2 */
		if (no == PMPCFG0 || no == PMPCFG2)
			for (int i = 0; i < 8; i++)
				set_pmpcfg((no - PMPCFG0) * 4 + i,
					   (data >> (i * 8)) & 0xff);
		return true;
	}
	if (no >= PMPADDR0 && no <= PMPADDR15) {
		pmpaddr[no - PMPADDR0] = data;
		return true;
	}
	if (no >= MHPMEVENT3 && no <= MHPMEVENT6) {
		csr.mhpmevent[no - MHPMEVENT3] = data;
		return true;
	}
	if (no >= MHPMCOUNTER3 && no <= MHPMCOUNTER6) {
		csr.mhpmcounter[no - MHPMCOUNTER3] = data;
		return true;
	}

	switch (no) {
	case MSTATUS:
		csr.mstatus = data & MSTATUS_WMASK;
		break;
	case SSTATUS:
		csr.mstatus = (csr.mstatus & ~SSTATUS_WMASK) |
			      (data & SSTATUS_WMASK);
		break;
	/* misa is fixed and there are no debug triggers: writes are ignored */
	case MISA: case TSELECT: case TDATA1: case TDATA2: case TDATA3:
	case MCONTEXT:
		break;
	case MEDELEG: csr.medeleg = data & MEDELEG_WMASK; break;
	case MIDELEG: csr.mideleg = data & S_INTERRUPTS; break;
	case MIE:
		csr.mie = data & (S_INTERRUPTS | M_INTERRUPTS | VS_INTERRUPTS);
		break;
	case SIE:
		csr.mie = (csr.mie & ~csr.mideleg) | (data & csr.mideleg);
		break;
	case HIE:
		csr.mie = (csr.mie & ~VS_INTERRUPTS) | (data & VS_INTERRUPTS);
		break;
	case VSIE:
		csr.mie = (csr.mie & ~csr.hideleg) | ((data << 1) & csr.hideleg);
		break;
	/* the machine-level pending bits are driven by the platform; of the
	 * VS-level bits only the software interrupt is writable here */
	case MIP:
		csr.mip = (csr.mip & ~(S_INTERRUPTS | MIP_VSSIP)) |
			  (data & (S_INTERRUPTS | MIP_VSSIP));
		break;
	case SIP:
		csr.mip = (csr.mip & ~(MIP_SSIP & csr.mideleg)) |
			  (data & MIP_SSIP & csr.mideleg);
		break;
	case HIP:
		csr.mip = (csr.mip & ~MIP_VSSIP) | (data & MIP_VSSIP);
		break;
	/* hvip is how a hypervisor raises the interrupts of its guest */
	case HVIP:
		csr.mip = (csr.mip & ~VS_INTERRUPTS) | (data & VS_INTERRUPTS);
		break;
	case VSIP:
		csr.mip = (csr.mip & ~(MIP_VSSIP & csr.hideleg)) |
			  ((data << 1) & MIP_VSSIP & csr.hideleg);
		break;
	case HSTATUS: csr.hstatus = data & HSTATUS_WMASK; break;
	case HEDELEG: csr.hedeleg = data & HEDELEG_WMASK; break;
	case HIDELEG: csr.hideleg = data & VS_INTERRUPTS; break;
	case HCOUNTEREN: csr.hcounteren = data & 0xffffffffULL; break;
	case HENVCFG: csr.henvcfg = data & 1; break;
	case HGEIE: break;
	case HTVAL: csr.htval = data; break;
	case HTINST: csr.htinst = data; break;
	case MTVAL2: csr.mtval2 = data; break;
	case MTINST: csr.mtinst = data; break;
	/* a write that selects an unsupported mode leaves the mode unchanged */
	case HGATP:
		if ((data >> 60) == 8 || (data >> 60) == 0)
			csr.hgatp = data & (0xf000000000000000ULL | HGATP_PPN);
		else
			csr.hgatp = (csr.hgatp & 0xf000000000000000ULL) | (data & HGATP_PPN);
		break;
	case VSSTATUS: csr.vsstatus = data & SSTATUS_WMASK; break;
	case VSTVEC: csr.vstvec = data & ~2ULL; break;
	case VSSCRATCH: csr.vsscratch = data; break;
	case VSEPC: csr.vsepc = data & ~3ULL; break;
	case VSCAUSE: csr.vscause = data & CAUSE_WMASK; break;
	case VSTVAL: csr.vstval = data; break;
	case VSATP:
		if ((data >> 60) == 8 || (data >> 60) == 0)
			csr.vsatp = data & 0xf0000fffffffffffULL;
		break;
	/* only direct (0) and vectored (1) trap vector modes */
	case MTVEC: csr.mtvec = data & ~2ULL; break;
	case STVEC: csr.stvec = data & ~2ULL; break;
	case MCOUNTERN: csr.mcounteren = data & 0xffffffffULL; break;
	case SCOUNTEREN: csr.scounteren = data & 0xffffffffULL; break;
	case MENVCFG: csr.menvcfg = data; break;
	case SENVCFG: csr.senvcfg = data; break;
	case MSECCFG: csr.mseccfg = data; break;
	case MCOUNTINHIBIT: csr.mcountinhibit = data; break;
	case MSCRATCH: csr.mscratch = data; break;
	case SSCRATCH: csr.sscratch = data; break;
	/* instructions are 4 bytes: the low two bits of an epc are zero */
	case MEPC: csr.mepc = data & ~3ULL; break;
	case SEPC: csr.sepc = data & ~3ULL; break;
	case MCAUSE: csr.mcause = data & CAUSE_WMASK; break;
	case SCAUSE: csr.scause = data & CAUSE_WMASK; break;
	case MTVAL: csr.mtval = data; break;
	case STVAL: csr.stval = data; break;
	/* Sv39 or bare; a write selecting any other mode is ignored. There
	 * are no ASID bits. */
	case SATP:
		if ((data >> 60) == 8 || (data >> 60) == 0)
			csr.satp = data & 0xf0000fffffffffffULL;
		break;
	case MCYCLE: g_sim_cycles = data; break;
	case MINSTRET: g_sim_instret = data; break;
	default: return false;
	}
	return true;
}

static void csr_abort(const char *what, uint64_t no)
{
	set_bad();
	state = NEMU_ABORT;
	printf(ANSI_FMT("%s: priv=%lu csr=%lx\n", ANSI_FG_RED), what,
	       (unsigned long)priv_level, (unsigned long)no);
}

/* Interrupts are taken by the most privileged mode first: M, then HS, then
 * VS. Within one mode the order is external, software, timer. */
int csr_pending_interrupt(word_t pending)
{
	static const int order[] = {11, 3, 7, 9, 1, 5, 10, 2, 6};
	const word_t mideleg = csr.mideleg | VS_INTERRUPTS;
	const word_t active = pending & csr.mie;
	const word_t level[3] = {
		active & ~mideleg,
		active & mideleg & ~csr.hideleg,
		active & mideleg & csr.hideleg,
	};
	/* a mode takes its interrupts when it runs with them enabled, and
	 * always while a less privileged mode runs */
	const bool enabled[3] = {
		priv_level < MACHINE || (csr.mstatus & MSTATUS_MIE),
		priv_level < MACHINE &&
			(virt_mode || priv_level == USER || (csr.mstatus & MSTATUS_SIE)),
		virt_mode && (priv_level == USER || (csr.vsstatus & MSTATUS_SIE)),
	};

	for (int i = 0; i < 3; i++) {
		if (!enabled[i])
			continue;
		for (unsigned k = 0; k < sizeof(order) / sizeof(order[0]); k++)
			if ((level[i] >> order[k]) & 1)
				return order[k];
	}
	return -1;
}

/* Where a trap is handled: 3 machine, 2 HS, 1 VS mode. */
static int trap_target(bool interrupt, word_t code)
{
	const word_t deleg = interrupt ? (csr.mideleg | VS_INTERRUPTS) : csr.medeleg;
	const word_t hdeleg = interrupt ? csr.hideleg : csr.hedeleg;

	if (priv_level == MACHINE || !((deleg >> code) & 1))
		return 3;
	if (virt_mode && ((hdeleg >> code) & 1))
		return 1;
	return 2;
}

int csr_interrupt_code(int number)
{
	return trap_target(true, number) == 1 ? number - 1 : number;
}

bool priv_check(uint64_t no, bool is_write){
	if (csr_accessible(no, is_write))
		return true;
	csr_abort("Illegal CSR access", no);
	return false;
}

void set_csr(uint64_t no, uint64_t data){
	if (!csr_write(no, data))
		csr_abort("Illegal CSR write", no);
}

word_t read_csr(uint64_t no){
	word_t value = 0xbadbeef;
	if (!csr_read(no, &value))
		csr_abort("Illegal CSR read", no);
	return value;
}

word_t raise_intr(word_t NO, vaddr_t epc, uint32_t args){
	csr.mepc = epc;
	csr.mcause = NO;
	return csr.mtvec;
}

/* The interrupt-enable stack of an S-level status register on a trap */
static word_t status_trap(word_t status)
{
	status = (status & ~(MSTATUS_SPIE | MSTATUS_SPP)) |
		 ((status & MSTATUS_SIE) ? MSTATUS_SPIE : 0) |
		 (priv_level == SUPERVISOR ? MSTATUS_SPP : 0);
	return status & ~MSTATUS_SIE;
}

vaddr_t csr_trap_guest(word_t cause, vaddr_t epc, word_t tval, word_t tval2,
		       word_t tinst, bool gva)
{
	const bool interrupt = (cause & CAUSE_INTERRUPT) != 0;
	word_t code = cause & 0x3f;
	const int target = trap_target(interrupt, code);
	word_t tvec;

	if (target == 1) {
		/* the guest sees its own interrupt numbers */
		if (interrupt)
			code--;
		csr.vsepc = epc & ~3ULL;
		csr.vscause = (cause & CAUSE_INTERRUPT) | code;
		csr.vstval = tval;
		csr.vsstatus = status_trap(csr.vsstatus);
		priv_level = SUPERVISOR;
		tvec = csr.vstvec;
	} else if (target == 2) {
		csr.sepc = epc & ~3ULL;
		csr.scause = (cause & CAUSE_INTERRUPT) | code;
		csr.stval = tval;
		csr.htval = tval2;
		csr.htinst = tinst;
		csr.mstatus = status_trap(csr.mstatus);
		/* SPVP follows SPP, but only for a trap that leaves a guest */
		csr.hstatus &= ~(HSTATUS_SPV | HSTATUS_GVA);
		if (gva)
			csr.hstatus |= HSTATUS_GVA;
		if (virt_mode) {
			csr.hstatus |= HSTATUS_SPV;
			csr.hstatus = (csr.hstatus & ~HSTATUS_SPVP) |
				      (priv_level == SUPERVISOR ? HSTATUS_SPVP : 0);
		}
		virt_mode = false;
		priv_level = SUPERVISOR;
		tvec = csr.stvec;
	} else {
		csr.mepc = epc & ~3ULL;
		csr.mcause = (cause & CAUSE_INTERRUPT) | code;
		csr.mtval = tval;
		csr.mtval2 = tval2;
		csr.mtinst = tinst;
		csr.mstatus = (csr.mstatus & ~(MSTATUS_MPIE | MSTATUS_MPP |
					       MSTATUS_MPV | MSTATUS_GVA)) |
			      ((csr.mstatus & MSTATUS_MIE) ? MSTATUS_MPIE : 0) |
			      (priv_level << 11) |
			      (virt_mode ? MSTATUS_MPV : 0) |
			      (gva ? MSTATUS_GVA : 0);
		csr.mstatus &= ~MSTATUS_MIE;
		virt_mode = false;
		priv_level = MACHINE;
		tvec = csr.mtvec;
	}

	/* vectored mode only applies to interrupts */
	if (interrupt && (tvec & 1))
		return (tvec & ~3ULL) + 4 * code;
	return tvec & ~3ULL;
}

vaddr_t csr_trap(word_t cause, vaddr_t epc, word_t tval)
{
	return csr_trap_guest(cause, epc, tval, 0, 0, false);
}

vaddr_t csr_mret(void)
{
	priv_level = (csr.mstatus & MSTATUS_MPP) >> 11;
	virt_mode = priv_level != MACHINE && (csr.mstatus & MSTATUS_MPV);
	csr.mstatus = (csr.mstatus & ~MSTATUS_MIE) |
		      ((csr.mstatus & MSTATUS_MPIE) ? MSTATUS_MIE : 0);
	csr.mstatus |= MSTATUS_MPIE;
	csr.mstatus &= ~(MSTATUS_MPP | MSTATUS_MPV);
	if (priv_level != MACHINE)
		csr.mstatus &= ~MSTATUS_MPRV;
	return csr.mepc;
}

/* In VS mode SRET works on vsstatus and stays in the guest; elsewhere it
 * returns to the mode in sstatus.SPP and hstatus.SPV. */
vaddr_t csr_sret(void)
{
	if (virt_mode) {
		priv_level = (csr.vsstatus & MSTATUS_SPP) ? SUPERVISOR : USER;
		csr.vsstatus = (csr.vsstatus & ~MSTATUS_SIE) |
			       ((csr.vsstatus & MSTATUS_SPIE) ? MSTATUS_SIE : 0);
		csr.vsstatus |= MSTATUS_SPIE;
		csr.vsstatus &= ~MSTATUS_SPP;
		return csr.vsepc;
	}
	priv_level = (csr.mstatus & MSTATUS_SPP) ? SUPERVISOR : USER;
	virt_mode = (csr.hstatus & HSTATUS_SPV) != 0;
	csr.mstatus = (csr.mstatus & ~MSTATUS_SIE) |
		      ((csr.mstatus & MSTATUS_SPIE) ? MSTATUS_SIE : 0);
	csr.mstatus |= MSTATUS_SPIE;
	csr.mstatus &= ~(MSTATUS_SPP | MSTATUS_MPRV);
	csr.hstatus &= ~HSTATUS_SPV;
	return csr.sepc;
}

bool pmp_check(paddr_t addr, bool is_execute, bool is_write){
	/* 修正 1：如果没有任何 PMP 开启，且是 Machine 模式，默认允许 */
	if(!pmp_on_count && (priv_level == MACHINE))
		return true;

	/* 修正 2：第一匹配原则 (First-match) */
	for(int i = 0; i < PMP_NUMBER_IN_SUSTEMU; i++){
		uint8_t cfg = pmpcfg[i];
		uint8_t a_field = cfg & PMP_A;
		if(a_field == PMP_A_OFF) continue;

		uint64_t start = 0, end = 0;

		/* 修正 3：地址计算。pmpaddr 存储的是地址的 [55:2] 位，需左移 2 位 */
		if(a_field == PMP_A_TOR){
			start = (i == 0) ? 0 : (pmpaddr[i-1] << 2);
			end = pmpaddr[i] << 2;
		} else if(a_field == PMP_A_NA4){
			start = pmpaddr[i] << 2;
			end = start + 4;
		} else if(a_field == PMP_A_NAPOT){
			uint64_t mask = (pmpaddr[i] << 2) | 0x3;
			uint64_t size = 1;
			while (mask & 1) { mask >>= 1; size <<= 1; }
			start = (pmpaddr[i] << 2) & ~(size - 1);
			end = start + size;
		}

		/* 判定是否落在范围内 */
		if(addr >= start && addr < end){
			/* 修正 4：M 模式下，只有设置了锁位 (L bit) 才受权限检查约束 */
			if(priv_level == MACHINE && !(cfg & PMP_L))
				return true;

			bool r = cfg & PMP_R;
			bool w = cfg & PMP_W;
			bool x = cfg & PMP_X;

			if((is_execute && !x) || (is_write && !w) || (!is_execute && !is_write && !r)){
				goto pmp_fault;
			}
			return true; /* 匹配并权限检查通过 */
		}
	}

	/* 修正 5：默认判定。
	   如果没有匹配任何条目，M 模式允许，S/U 模式拒绝。 */
	if(priv_level == MACHINE) return true;

pmp_fault:
	set_bad();
	state = NEMU_ABORT;
	printf(ANSI_FMT("PMP Check fail at %lx\n", ANSI_FG_RED), addr);
	return false;
}
