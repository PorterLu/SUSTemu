#include <csr.h>
#include <debug.h>
#include <state.h>
#include <utils.h>

CSR csr;
uint64_t priv_level = 3;
uint8_t pmpcfg[PMP_NUMBER_IN_SUSTEMU];
word_t pmpaddr[PMP_NUMBER_IN_SUSTEMU];
uint8_t pmp_on_count = 0;

/* id, level, read, write */
uint32_t csr_table[][4] = {
	{MSTATUS, MACHINE, true, true},
	{MISA, MACHINE, true, false},
	{MEDELEG, MACHINE, true, true},
	{MIDELEG, MACHINE, true, true},
	{MIE, MACHINE, true, true},
	{MTVEC, MACHINE, true, true},
	{MCOUNTERN, MACHINE, true, false},
	{MSCRATCH, MACHINE, true, true},
	{MEPC, MACHINE, true, true},
	{MCAUSE, MACHINE, true, true},
	{MTVAL, MACHINE, true, true},
	{MIP, MACHINE, true, true},
	{MHARTID, MACHINE, true, false},
	{SSTATUS, SUPERVISOR, true, true},
	{SIE, SUPERVISOR, true, true},
	{STVEC, SUPERVISOR, true, true},
	{SSCRATCH, SUPERVISOR, true, true},
	{SEPC, SUPERVISOR, true, true},
	{SCAUSE, SUPERVISOR, true, true},
	{STVAL, SUPERVISOR, true, true},
	{SIP, SUPERVISOR, true, true},
	{PMPCFG0, MACHINE, true, true},
	{PMPCFG2, MACHINE, true, true},
	{PMPCFG4, MACHINE, true, true},
	{PMPCFG6, MACHINE, true, true},
	{PMPCFG8, MACHINE, true, true},
	{PMPCFGA, MACHINE, true, true},
	{PMPCFGC, MACHINE, true, true},
	{PMPCFGE, MACHINE, true, true},
	{PMPADDR0, MACHINE, true, true},
	{PMPADDR2, MACHINE, true, true},
	{PMPADDR4, MACHINE, true, true},
	{PMPADDR6, MACHINE, true, true},
	{PMPADDR8, MACHINE, true, true},
};

void mret_priv_transfer(){
	priv_level = (csr.mstatus & 0x18000) >> 11;
	csr.mstatus = csr.mstatus & (~(uint64_t)0x18000);
	csr.mstatus = ((csr.mstatus & 0x80) >> 4) | (csr.mstatus & (~(uint64_t)0x80));
	csr.mstatus = csr.mstatus | 0x80;
}

void exception_priv_transfer(){
	csr.mstatus = (csr.mstatus & ~(uint64_t)0x18000) | (priv_level << 11);
	priv_level = MACHINE;
	csr.mstatus = ((csr.mstatus & 0x08) << 4) | (csr.mstatus & (~(uint64_t)0x80));
	csr.mstatus = csr.mstatus & ~(uint64_t)0x08;
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

void set_csr(uint64_t no, uint64_t data){
	if(!priv_check(no, true))
		return;

	switch(no){
		case MEPC: csr.mepc = data; return;
		case MCAUSE: csr.mcause = data; return;
		case MTVEC: csr.mtvec = data; return;
		case MHARTID: return;
		case MTVAL: csr.mtval = data; return;
		case MIP: csr.mip = data; return; // 注意：通常应为 mie，此处保留你的变量名
		case MIE: csr.mie = data; return;
		case MIDELEG: csr.mideleg = data; return;
		case MEDELEG: csr.medeleg = data; return;
		case MSCRATCH: csr.mscratch = data; return;
		case SSTATUS: csr.sstatus = data; return;
		case SIE: csr.sie = data; return;
		case STVEC: csr.stvec = data; return;
		case SSCRATCH: csr.sscratch = data; return;
		case STVAL: csr.stval = data; return;
		case SIP: csr.sip = data; return;

		/* 修正：RV64 下 PMPCFG 寄存器是 64 位的，包含 8 个 PMP 配置 */
		case PMPCFG0:
			for(int i=0; i<8; i++) set_pmpcfg(i, (data >> (i*8)) & 0xFF);
			return;
		case PMPCFG2:
			for(int i=0; i<8; i++) set_pmpcfg(i+8, (data >> (i*8)) & 0xFF);
			return;

		/* 修正：PMPADDR 索引 */
		case PMPADDR0: pmpaddr[0] = data; return;
		case PMPADDR2: pmpaddr[1] = data; return;
		case PMPADDR4: pmpaddr[2] = data; return;
		case PMPADDR6: pmpaddr[3] = data; return;
		case PMPADDR8: pmpaddr[4] = data; return;
	}
}

word_t read_csr(uint64_t no){
	if(!priv_check(no, false))
		return 0xbadbeef;

	switch(no){
		case MEPC: return csr.mepc;
		case MCAUSE: return csr.mcause;
		case MTVEC: return csr.mtvec;
		case MSTATUS: return csr.mstatus;
		case MHARTID: return 0;
		case MTVAL: return csr.mtval;
		case MIP: return csr.mip;
		case MIE: return csr.mie;
		case MIDELEG: return csr.mideleg;
		case MEDELEG: return csr.medeleg;
		case MISA: return 0x8000000000001100ULL;
		case MSCRATCH: return csr.mscratch;
		case SSTATUS: return csr.sstatus;
		case SIE: return csr.sie;
		case STVEC: return csr.stvec;
		case SSCRATCH: return csr.sscratch;
		case SEPC: return csr.sepc;
		case SCAUSE: return csr.scause;
		case STVAL: return csr.stval;
		case SIP: return csr.sip;

		/* 修正：PMPCFG 读取逻辑 */
		case PMPCFG0: {
			uint64_t val = 0;
			for(int i=0; i<8; i++) val |= ((uint64_t)pmpcfg[i] << (i*8));
			return val;
		}
		case PMPADDR0: return pmpaddr[0];
		case PMPADDR2: return pmpaddr[1];
	}
	Assert(0, "unknown no:%lx\n", no);
}

word_t raise_intr(word_t NO, vaddr_t epc, uint32_t args){
	csr.mepc = epc;
	csr.mcause = NO;
	return csr.mtvec;
}

bool priv_check(uint64_t no, bool is_write){
	int num = sizeof(csr_table) / sizeof(csr_table[0]);
	for(int i = 0; i < num; i++){
		if(no == csr_table[i][0]){
			if(priv_level < csr_table[i][1] || (!is_write && !csr_table[i][2]) || (is_write && !csr_table[i][3])){
				set_bad();
				state = NEMU_ABORT;
				printf(ANSI_FMT("Privilege Check fail\n", ANSI_FG_RED));
				return false;
			}
			return true;
		}
	}
	return false;
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
