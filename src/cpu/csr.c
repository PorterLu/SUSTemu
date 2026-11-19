#include <csr.h>
#include <debug.h>
#include <state.h>
#include <utils.h>
#include <string.h>
#include <memory/cache.h>

CSR csr;
uint64_t priv_level = 3;
uint64_t g_sim_cycles  = 0;
uint64_t g_sim_instret = 0;
int      g_current_hartid = 0;
uint8_t pmpcfg[PMP_NUMBER_IN_SUSTEMU];
word_t pmpaddr[PMP_NUMBER_IN_SUSTEMU];
uint8_t pmp_on_count = 0;
bool csr_hide_custom_counters = false;

/* RV64IMA with supervisor and user mode */
#define MISA_VALUE 0x8000000000141101ULL

/* writable bits of mstatus and the part of it that sstatus shows */
#define MSTATUS_WMASK (MSTATUS_SIE | MSTATUS_MIE | MSTATUS_SPIE | MSTATUS_MPIE | \
		       MSTATUS_SPP | MSTATUS_MPP | MSTATUS_MPRV | MSTATUS_SUM | \
		       MSTATUS_MXR)
#define SSTATUS_WMASK (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_SUM | \
		       MSTATUS_MXR)
/* UXL = SXL = 2: both lower modes are 64 bit */
#define MSTATUS_XL    0x0000000a00000000ULL

#define S_INTERRUPTS  (MIP_SSIP | MIP_STIP | MIP_SEIP)
#define M_INTERRUPTS  (MIP_MSIP | MIP_MTIP | MIP_MEIP)
/* exceptions that can be delegated: everything but an ecall from M-mode */
#define MEDELEG_WMASK 0xf7ffULL

void csr_reset(void)
{
	memset(&csr, 0, sizeof(csr));
	memset(pmpcfg, 0, sizeof(pmpcfg));
	memset(pmpaddr, 0, sizeof(pmpaddr));
	pmp_on_count = 0;
	priv_level = MACHINE;
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
		return true;
	}
	return false;
}

static bool csr_read_only(uint32_t no)
{
	return ((no >> 10) & 3) == 3;
}

static bool csr_accessible(uint32_t no, bool is_write)
{
	if (!csr_implemented(no))
		return false;
	if (priv_level < ((no >> 8) & 3))
		return false;
	if (is_write && csr_read_only(no))
		return false;
	/* the unprivileged counters are gated by mcounteren and scounteren */
	if (no >= CYCLE && no <= 0xc1f) {
		word_t enable = ~0ULL;
		if (priv_level < MACHINE)
			enable &= csr.mcounteren;
		if (priv_level < SUPERVISOR)
			enable &= csr.scounteren;
		if (!((enable >> (no - CYCLE)) & 1))
			return false;
	}
	return true;
}

bool csr_read(uint32_t no, word_t *value)
{
	if (!csr_accessible(no, false))
		return false;

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
	case MIDELEG: *value = csr.mideleg; break;
	case MIE: *value = csr.mie; break;
	case SIE: *value = csr.mie & csr.mideleg; break;
	case MIP: *value = csr.mip; break;
	case SIP: *value = csr.mip & csr.mideleg; break;
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
	case MIE: csr.mie = data & (S_INTERRUPTS | M_INTERRUPTS); break;
	case SIE:
		csr.mie = (csr.mie & ~csr.mideleg) | (data & csr.mideleg);
		break;
	/* the machine-level pending bits are driven by the platform */
	case MIP:
		csr.mip = (csr.mip & ~S_INTERRUPTS) | (data & S_INTERRUPTS);
		break;
	case SIP:
		csr.mip = (csr.mip & ~(MIP_SSIP & csr.mideleg)) |
			  (data & MIP_SSIP & csr.mideleg);
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
	case MCAUSE: csr.mcause = data & (CAUSE_INTERRUPT | 0xf); break;
	case SCAUSE: csr.scause = data & (CAUSE_INTERRUPT | 0xf); break;
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

vaddr_t csr_trap(word_t cause, vaddr_t epc, word_t tval)
{
	const bool interrupt = (cause & CAUSE_INTERRUPT) != 0;
	const word_t code = cause & 0x3f;
	const word_t deleg = interrupt ? csr.mideleg : csr.medeleg;
	const word_t stored = (cause & CAUSE_INTERRUPT) | (code & 0xf);
	word_t tvec;

	if (priv_level <= SUPERVISOR && ((deleg >> code) & 1)) {
		csr.sepc = epc & ~3ULL;
		csr.scause = stored;
		csr.stval = tval;
		csr.mstatus = (csr.mstatus & ~(MSTATUS_SPIE | MSTATUS_SPP)) |
			      ((csr.mstatus & MSTATUS_SIE) ? MSTATUS_SPIE : 0) |
			      (priv_level == SUPERVISOR ? MSTATUS_SPP : 0);
		csr.mstatus &= ~MSTATUS_SIE;
		priv_level = SUPERVISOR;
		tvec = csr.stvec;
	} else {
		csr.mepc = epc & ~3ULL;
		csr.mcause = stored;
		csr.mtval = tval;
		csr.mstatus = (csr.mstatus & ~(MSTATUS_MPIE | MSTATUS_MPP)) |
			      ((csr.mstatus & MSTATUS_MIE) ? MSTATUS_MPIE : 0) |
			      (priv_level << 11);
		csr.mstatus &= ~MSTATUS_MIE;
		priv_level = MACHINE;
		tvec = csr.mtvec;
	}

	/* vectored mode only applies to interrupts */
	if (interrupt && (tvec & 1))
		return (tvec & ~3ULL) + 4 * code;
	return tvec & ~3ULL;
}

vaddr_t csr_mret(void)
{
	priv_level = (csr.mstatus & MSTATUS_MPP) >> 11;
	csr.mstatus = (csr.mstatus & ~MSTATUS_MIE) |
		      ((csr.mstatus & MSTATUS_MPIE) ? MSTATUS_MIE : 0);
	csr.mstatus |= MSTATUS_MPIE;
	csr.mstatus &= ~MSTATUS_MPP;
	if (priv_level != MACHINE)
		csr.mstatus &= ~MSTATUS_MPRV;
	return csr.mepc;
}

vaddr_t csr_sret(void)
{
	priv_level = (csr.mstatus & MSTATUS_SPP) ? SUPERVISOR : USER;
	csr.mstatus = (csr.mstatus & ~MSTATUS_SIE) |
		      ((csr.mstatus & MSTATUS_SPIE) ? MSTATUS_SIE : 0);
	csr.mstatus |= MSTATUS_SPIE;
	csr.mstatus &= ~(MSTATUS_SPP | MSTATUS_MPRV);
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
