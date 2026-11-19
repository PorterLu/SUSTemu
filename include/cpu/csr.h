#ifndef __CSR_H__
#define __CSR_H__
#include <common.h>

/* PMP 常量定义 */
#define PMP_NUMBER_IN_SUSTEMU 16
#define PMP_R     0x01
#define PMP_W     0x02
#define PMP_X     0x04
#define PMP_A     0x18
#define PMP_L     0x80
#define PMP_A_OFF   0x00
#define PMP_A_TOR   0x08
#define PMP_A_NA4   0x10
#define PMP_A_NAPOT 0x18


#define MSTATUS 0x300
#define MISA	0x301
#define MEDELEG 0x302
#define MIDELEG 0x303
#define MIE		0x304
#define MTVEC 	0x305
#define MCOUNTERN	0x306
#define MENVCFG	0x30a
#define MCOUNTINHIBIT	0x320
#define MHPMEVENT3	0x323
#define MHPMEVENT6	0x326
#define MSCRATCH	0x340
#define MEPC	0x341
#define MCAUSE	0x342
#define MTVAL	0x343
#define MIP		0x344
#define MSECCFG	0x747
#define TSELECT	0x7a0
#define TDATA1	0x7a1
#define TDATA2	0x7a2
#define TDATA3	0x7a3
#define MCONTEXT	0x7a8
#define MCYCLE	0xb00
#define MINSTRET	0xb02
#define MHPMCOUNTER3	0xb03
#define MHPMCOUNTER6	0xb06
#define MVENDORID 0xf11
#define MARCHID 0xf12
#define MIMPID	0xf13
#define MHARTID 0xf14
#define MCONFIGPTR 0xf15
#define SSTATUS 0x100
#define SIE 	0x104
#define STVEC	0x105
#define SCOUNTEREN	0x106
#define SENVCFG	0x10a
#define SSCRATCH	0x140
#define SEPC	0x141
#define SCAUSE	0x142
#define STVAL	0x143
#define SIP		0x144
#define SATP	0x180
#define PMPCFG0 0x3a0
#define PMPCFG1 0x3a1
#define PMPCFG2 0x3a2
#define PMPCFG3 0x3a3
#define PMPCFG4	0x3a4
#define PMPCFG6	0x3a6
#define PMPCFG8 0x3a8
#define PMPCFGA 0x3aa
#define PMPCFGC	0x3ac
#define PMPCFGE	0x3ae
#define PMPADDR0 0x3b0
#define PMPADDR1 0x3b1
#define PMPADDR2 0x3b2
#define PMPADDR3 0x3b3
#define PMPADDR4 0x3b4
#define PMPADDR5 0x3b5
#define PMPADDR6 0x3b6
#define PMPADDR7 0x3b7
#define PMPADDR8 0x3b8
#define PMPADDR9 0x3b9
#define PMPADDR10 0x3ba
#define PMPADDR11 0x3bb
#define PMPADDR12 0x3bc
#define PMPADDR13 0x3bd
#define PMPADDR14 0x3be
#define PMPADDR15 0x3bf

/* Performance counter CSRs (read-only from all privilege levels) */
#define CYCLE    0xc00   /* rdcycle — cycles executed by this hart */
#define TIME     0xc01   /* rdtime — not implemented, traps */
#define INSTRET  0xc02   /* rdinstret — instructions retired */

/* Custom HPM counters: cache hit/miss stats (hpmcounter3..8, 0xC03-0xC08) */
#define L1D_HITS    0xc03
#define L1D_MISSES  0xc04
#define L1I_HITS    0xc05
#define L1I_MISSES  0xc06
#define L2_HITS     0xc07
#define L2_MISSES   0xc08

/* mstatus fields */
#define MSTATUS_SIE   (1ULL << 1)
#define MSTATUS_MIE   (1ULL << 3)
#define MSTATUS_SPIE  (1ULL << 5)
#define MSTATUS_MPIE  (1ULL << 7)
#define MSTATUS_SPP   (1ULL << 8)
#define MSTATUS_MPP   (3ULL << 11)
#define MSTATUS_MPRV  (1ULL << 17)
#define MSTATUS_SUM   (1ULL << 18)
#define MSTATUS_MXR   (1ULL << 19)
#define MSTATUS_UXL   (3ULL << 32)
#define MSTATUS_SXL   (3ULL << 34)

/* interrupt bits of mip/mie */
#define MIP_SSIP (1ULL << 1)
#define MIP_MSIP (1ULL << 3)
#define MIP_STIP (1ULL << 5)
#define MIP_MTIP (1ULL << 7)
#define MIP_SEIP (1ULL << 9)
#define MIP_MEIP (1ULL << 11)

/* exception causes */
#define CAUSE_MISALIGNED_FETCH  0
#define CAUSE_ILLEGAL_INST      2
#define CAUSE_BREAKPOINT        3
#define CAUSE_MISALIGNED_LOAD   4
#define CAUSE_MISALIGNED_STORE  6
#define CAUSE_USER_ECALL        8
#define CAUSE_FETCH_PAGE_FAULT  12
#define CAUSE_LOAD_PAGE_FAULT   13
#define CAUSE_STORE_PAGE_FAULT  15
#define CAUSE_INTERRUPT         (1ULL << 63)

/* Global simulation cycle counter (bumped each pipeline/OOO cycle, or each
 * instruction in functional mode).  Exposed via CYCLE CSR. */
extern uint64_t g_sim_cycles;
extern uint64_t g_sim_instret;

/* Current hart index — updated by core_cycle() before dispatching.
 * read_csr(MHARTID) returns this value. */
extern int g_current_hartid;

enum {
	USER,
	SUPERVISOR,
	PADDING,
	MACHINE
};

/* sstatus, sie and sip are views of mstatus, mie and mip and have no storage
 * of their own. */
typedef struct {
	word_t mstatus;
	word_t mepc;
	word_t mcause;
	word_t mtvec;
	word_t mideleg;
	word_t medeleg;
	word_t mie;
	word_t mip;
	word_t mtval;
	word_t mscratch;
	word_t mcounteren;
	word_t scounteren;
	word_t menvcfg;
	word_t senvcfg;
	word_t mseccfg;
	word_t mcountinhibit;
	word_t mhpmevent[4];
	word_t mhpmcounter[4];
	word_t pmpcfg[4];
	word_t sepc;
	word_t scause;
	word_t stval;
	word_t stvec;
	word_t satp;
	word_t sscratch;
} CSR;

extern CSR csr;
extern uint64_t priv_level;
extern uint8_t pmpcfg[PMP_NUMBER_IN_SUSTEMU];
extern word_t pmpaddr[PMP_NUMBER_IN_SUSTEMU];
/* The sustemu-only cache statistics counters (0xc03..0xc08) do not exist on a
 * device under test; a difftest reference hides them. */
extern bool csr_hide_custom_counters;

void csr_reset(void);
/* Access a CSR with the privilege and read-only checks of the architecture.
 * A false return means the access raises an illegal-instruction exception. */
bool csr_read(uint32_t no, word_t *value);
bool csr_write(uint32_t no, word_t value);
/* Take a trap (cause bit 63 set for an interrupt) and return the handler pc. */
vaddr_t csr_trap(word_t cause, vaddr_t epc, word_t tval);
vaddr_t csr_mret(void);
vaddr_t csr_sret(void);

void set_csr(uint64_t no, uint64_t data);
word_t read_csr(uint64_t no);
word_t raise_intr(word_t NO, vaddr_t epc, uint32_t args);
void INV(vaddr_t addr);
bool priv_check(uint64_t no, bool is_write);
bool pmp_check(paddr_t addr, bool is_execute, bool is_write);
void mret_priv_transfer();
void exception_priv_transfer();

#endif
