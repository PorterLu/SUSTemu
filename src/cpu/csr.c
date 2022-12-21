#include <csr.h>
#include <debug.h>
#include <state.h>
#include <utils.h>

CSR csr;
uint64_t priv_level = 3;
uint8_t pmpcfg[PMP_NUMBER_IN_SUSTEMU];
word_t pmpaddr[PMP_NUMBER_IN_SUSTEMU];
uint8_t pmp_on_count = 0;

	/*id	,	level,	read,	write*/
uint32_t csr_table[][4] = {
	{MSTATUS, MACHINE,  true,	true},
	{MISA,	  MACHINE,	true,	false},
	{MEDELEG, MACHINE,	true,	true},
	{MIDELEG, MACHINE,	true,	true},
	{MIE,	  MACHINE,	true,	true},
	{MTVEC,	  MACHINE,	true,	true},
	{MCOUNTERN,	MACHINE, true,	false},
	{MSCRATCH,	MACHINE, true,	true},
	{MEPC,		MACHINE, true,	true},
	{MCAUSE,	MACHINE, true,	true},
	{MTVAL,		MACHINE, true,	true},
	{MIP,		MACHINE, true,	true},
	{MHARTID,	MACHINE, true,	false},
	{SSTATUS,	SUPERVISOR,	 true, 	true},
	{SIE,		SUPERVISOR,	 true,	true},
	{STVEC,		SUPERVISOR,	 true,	true},
	{SSCRATCH,	SUPERVISOR,	 true,	true},
	{SEPC,		SUPERVISOR,	 true,	true},
	{SCAUSE,	SUPERVISOR,	 true,	true},
	{STVAL,		SUPERVISOR,	 true,	true},
	{SIP,		SUPERVISOR,	 true,	true},
	{PMPCFG0,	MACHINE,	 true,	true},
	{PMPCFG2,	MACHINE,	 true,	true},
	{PMPCFG4,	MACHINE,	 true,	true},
	{PMPCFG6,	MACHINE,	 true,	true},
	{PMPCFG8,	MACHINE,	 true,	true},
	{PMPCFGA,	MACHINE,	 true,	true},
	{PMPCFGC,	MACHINE,	 true,	true},
	{PMPCFGE,	MACHINE,	 true,	true},
	{PMPADDR0,	MACHINE,	 true,	true},
	{PMPADDR2,	MACHINE,	 true,	true},
	{PMPADDR4,	MACHINE,	 true, 	true},
	{PMPADDR6,	MACHINE,	 true,	true},
	{PMPADDR8,	MACHINE,	 true,	true},
	{PMPADDRA,	MACHINE,	 true,	true},
	{PMPADDRC,	MACHINE,	 true,	true},
	{PMPADDRE,  MACHINE,	 true,	true},
};

void mret_priv_transfer(){
	//we do not consider supervisor mode now
	priv_level = (csr.mstatus & 0x18000) >> 11;
	csr.mstatus = csr.mstatus & (~(uint64_t)0x18000);
	csr.mstatus = ((csr.mstatus & 0x80) >> 4) | (csr.mstatus & (~(uint64_t)0x80));
	csr.mstatus = csr.mstatus | 0x80;
}

void exception_priv_transfer(){
	//we do not consider supervisor mode now
	csr.mstatus = (csr.mstatus & ~(uint64_t)0x18000) | (priv_level << 11);
	priv_level = MACHINE;
	csr.mstatus = ((csr.mstatus & 0x08) << 4) | (csr.mstatus & (~(uint64_t)0x80));
	csr.mstatus = csr.mstatus & ~(uint64_t)0x08;
	//printf("sbi call\n");
}

bool is_pmp_on(uint8_t pmp_id){
	return pmpcfg[pmp_id] & 0x18;
}

void set_pmpcfg(uint8_t id, uint8_t data){
	if(is_pmp_on(id)){
		if(!(data&0x1f)){
			pmp_on_count -= 1;
		}
	} else {
		if(data&0x1f){
			pmp_on_count += 1;
		}
	}
	pmpcfg[id] = data;
}

void set_csr(uint64_t no, uint64_t data){
	if(!priv_check(no, true))
		return;

	switch(no){
		case MEPC:
			csr.mepc = data;
			return;
		case MCAUSE:
			csr.mcause = data;
			return;
		case MTVEC:
			csr.mtvec = data;
			return;
		case MHARTID:
			return;
		case MTVAL:
			csr.mtval = data;
			return;
		case MIP:
			csr.mie = data;
			return;
		case MIDELEG:
			csr.mideleg = data;
			return;
		case MEDELEG:
			csr.medeleg = data;
			return;
		case MISA:
			return;
		case MSCRATCH:
			csr.mscratch = data;
			return;
		case SSTATUS:
			csr.sstatus = data;
			return;
		case SIE:
			csr.sie = data;
			return;
		case STVEC:
			csr.stvec = data;
			return;
		case SSCRATCH:
			csr.sscratch = data;
			return;
		case STVAL:
			csr.stval = data;
			return;
		case SIP:
			csr.sip = data;
			return;
		case PMPCFG0:
			set_pmpcfg(0, data);
			return;
		case PMPCFG2:
			set_pmpcfg(2/2, data);
			return;
		case PMPCFG4:
			set_pmpcfg(4/2, data);
			return;
		case PMPCFG6:
			set_pmpcfg(6/2, data);
			return;
		case PMPCFG8:
			set_pmpcfg(8/2, data);
			return;
		case PMPCFGA:
			set_pmpcfg(0xa/2, data);
			return;
		case PMPCFGC:
			set_pmpcfg(0xc/2, data);
			return;
		case PMPCFGE:
			set_pmpcfg(0xe/2, data);
			return;
		case PMPADDR0:
			pmpaddr[0] = data;
			return;
		case PMPADDR2:
			pmpaddr[2/2] = data;
			return;
		case PMPADDR4:
			pmpaddr[4/2] = data;
			return;
		case PMPADDR6:
			pmpaddr[6/2] = data;
			return;
		case PMPADDR8:
			pmpaddr[8/2] = data;
			return;
		case PMPADDRA:
			pmpaddr[0xa/2] = data;
			return;
		case PMPADDRC:
			pmpaddr[0xc/2] = data;
			return;
		case PMPADDRE:
			pmpaddr[0xe/2] = data;
			return;
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
		case MISA: return 0x8000000000001100;
		case MSCRATCH: return csr.mscratch;
		case SSTATUS: return csr.sstatus;
		case SIE: return csr.sie;
		case STVEC: return csr.stvec;
		case SSCRATCH: return csr.sscratch;
		case SEPC: return csr.sepc;
		case SCAUSE: return csr.scause;
		case STVAL: return csr.stval;
		case SIP: return csr.sip;
		case PMPCFG0: return pmpcfg[0];
		case PMPCFG2: return pmpcfg[2/2];
		case PMPCFG4: return pmpcfg[4/2];
		case PMPCFG6: return pmpcfg[6/2];
		case PMPCFG8: return pmpcfg[8/2];
		case PMPCFGA: return pmpcfg[0xa/2];
		case PMPCFGC: return pmpcfg[0xc/2];
		case PMPCFGE: return pmpcfg[0xe/2];
		case PMPADDR0: return pmpcfg[0];
		case PMPADDR2: return pmpcfg[2/2];
		case PMPADDR4: return pmpcfg[4/2];
		case PMPADDR6: return pmpcfg[6/2];
		case PMPADDR8: return pmpcfg[8/2];
		case PMPADDRA: return pmpcfg[0xa/2];
		case PMPADDRC: return pmpcfg[0xc/2];
		case PMPADDRE: return pmpcfg[0xe/2];
	}

	//return csr.mscratch;
	Assert(0, "unknown no:%lx\n", no);
}

word_t raise_intr(word_t NO, vaddr_t epc, uint32_t args){
	csr.mepc = epc;
	csr.mcause = NO;
	return csr.mtvec;
}

bool priv_check(uint64_t no, bool is_write){
	int i;
	bool pass = false;

	for(i = 0; i < sizeof(csr_table); i++){
		if(no == csr_table[i][0]){
			if(priv_level < csr_table[i][1] || (!is_write && !csr_table[i][2]) || (is_write && !csr_table[i][3])){
				set_bad();
				state = NEMU_ABORT;
				printf(ANSI_FMT("Privilege Check fail\n", ANSI_FG_RED));
				return false;
			}
			pass = true;
			break;
		}
	}
	
	return pass;
}

bool pmp_check(paddr_t addr, bool is_execute, bool is_write){
	if(!pmp_on_count || (priv_level == MACHINE))
		return true;
	
	int i;
	//we only support any address pmp
	//printf("mode: %ld\n", priv_level);
	//printf("addr:%lx\n", addr);
	for(i = 0; i < PMP_NUMBER_IN_SUSTEMU; i++){
		//printf("pmpaddr%d: %lx\n", i, pmpaddr[i]);
		if(is_pmp_on(0) && (i == 0) && addr< pmpaddr[0]){
			if((!(pmpcfg[0]&1) && !is_write) || (!(pmpcfg[0]&2) && is_write) || (!(pmpcfg[0]&4) && is_execute)){
				set_bad();
				state = NEMU_ABORT;
				printf(ANSI_FMT("PMP Check fail\n", ANSI_FG_RED));
				return false;
			}
			return true;
		} else if(is_pmp_on(i) && addr >= pmpaddr[i-1] && addr <= pmpaddr[i]){
			if((!(pmpcfg[i]&1) && !is_write) || (!(pmpcfg[i]&2) && is_write) || (!(pmpcfg[i]&4) && is_execute)){
				set_bad();
				state = NEMU_ABORT;
				printf(ANSI_FMT("PMP Check fail\n", ANSI_FG_RED));
				return false;
			}	
			return true;
		}
	}

	if(priv_level == 3)
		return true;
	else {
		if(pmp_on_count){
			set_bad();
			state = NEMU_ABORT;
			printf(ANSI_FMT("PMP Check fail\n", ANSI_FG_RED));
			return false;
		}else{
			return true;
		}
	}
}