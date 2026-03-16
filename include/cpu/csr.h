#ifndef __CSR_H__
#define __CSR_H__
#include <common.h>

/* PMP 常量定义 */
#define PMP_NUMBER_IN_SUSTEMU 8
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
#define MSCRATCH	0x340
#define MEPC	0x341
#define MCAUSE	0x342
#define MTVAL	0x343
#define MIP		0x344
#define MHARTID 0xf14
#define SSTATUS 0x100
#define SIE 	0x104
#define STVEC	0x105
#define SSCRATCH	0x140
#define SEPC	0x141
#define SCAUSE	0x142
#define STVAL	0x143
#define SIP		0x144
#define SATP	0x180
#define PMPCFG0 0x3a0
#define PMPCFG2 0x3a2
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

/* Performance counter CSRs (read-only from all privilege levels) */
#define CYCLE    0xc00   /* rdcycle — cycles executed by this hart */
#define INSTRET  0xc02   /* rdinstret — instructions retired */

/* Custom HPM counters: cache hit/miss stats (hpmcounter3..8, 0xC03-0xC08) */
#define L1D_HITS    0xc03
#define L1D_MISSES  0xc04
#define L1I_HITS    0xc05
#define L1I_MISSES  0xc06
#define L2_HITS     0xc07
#define L2_MISSES   0xc08

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
	word_t misa;
	word_t mscratch;
	word_t sstatus;
	word_t sepc;
	word_t scause;
	word_t sie;
	word_t sip;
	word_t stval;
	word_t stvec;
	word_t satp;
	word_t sscratch;
} CSR;

extern CSR csr;
extern uint64_t priv_level;
extern uint8_t pmpcfg[PMP_NUMBER_IN_SUSTEMU];
extern word_t pmpaddr[PMP_NUMBER_IN_SUSTEMU];
void set_csr(uint64_t no, uint64_t data);
word_t read_csr(uint64_t no);
word_t raise_intr(word_t NO, vaddr_t epc, uint32_t args);
void INV(vaddr_t addr);
bool priv_check(uint64_t no, bool is_write);
bool pmp_check(paddr_t addr, bool is_execute, bool is_write);
void mret_priv_transfer();
void exception_priv_transfer();

#endif
