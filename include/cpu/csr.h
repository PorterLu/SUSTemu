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

/* hypervisor extension: the hypervisor's own CSRs, the two it adds to
 * machine level and the VS copies of the supervisor CSRs */
#define HSTATUS	0x600
#define HEDELEG	0x602
#define HIDELEG	0x603
#define HIE		0x604
#define HCOUNTEREN	0x606
#define HGEIE	0x607
#define HENVCFG	0x60a
#define HTVAL	0x643
#define HIP		0x644
#define HVIP	0x645
#define HTINST	0x64a
#define HGATP	0x680
#define HGEIP	0xe12
#define MTINST	0x34a
#define MTVAL2	0x34b
#define VSSTATUS	0x200
#define VSIE	0x204
#define VSTVEC	0x205
#define VSSCRATCH	0x240
#define VSEPC	0x241
#define VSCAUSE	0x242
#define VSTVAL	0x243
#define VSIP	0x244
#define VSATP	0x280
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
#define MSTATUS_TVM   (1ULL << 20)
#define MSTATUS_TW    (1ULL << 21)
#define MSTATUS_TSR   (1ULL << 22)
#define MSTATUS_UXL   (3ULL << 32)
#define MSTATUS_SXL   (3ULL << 34)
#define MSTATUS_GVA   (1ULL << 38)
#define MSTATUS_MPV   (1ULL << 39)

/* hstatus fields */
#define HSTATUS_GVA   (1ULL << 6)
#define HSTATUS_SPV   (1ULL << 7)
#define HSTATUS_SPVP  (1ULL << 8)
#define HSTATUS_HU    (1ULL << 9)
#define HSTATUS_VTVM  (1ULL << 20)
#define HSTATUS_VTW   (1ULL << 21)
#define HSTATUS_VTSR  (1ULL << 22)

/* interrupt bits of mip/mie */
#define MIP_SSIP (1ULL << 1)
#define MIP_MSIP (1ULL << 3)
#define MIP_STIP (1ULL << 5)
#define MIP_MTIP (1ULL << 7)
#define MIP_SEIP (1ULL << 9)
#define MIP_MEIP (1ULL << 11)
#define MIP_VSSIP (1ULL << 2)
#define MIP_VSTIP (1ULL << 6)
#define MIP_VSEIP (1ULL << 10)

/* exception causes */
#define CAUSE_MISALIGNED_FETCH  0
#define CAUSE_ILLEGAL_INST      2
#define CAUSE_BREAKPOINT        3
#define CAUSE_MISALIGNED_LOAD   4
#define CAUSE_MISALIGNED_STORE  6
#define CAUSE_USER_ECALL        8
#define CAUSE_SUPERVISOR_ECALL  9
#define CAUSE_VIRTUAL_SUPERVISOR_ECALL 10
#define CAUSE_MACHINE_ECALL     11
#define CAUSE_FETCH_PAGE_FAULT  12
#define CAUSE_LOAD_PAGE_FAULT   13
#define CAUSE_STORE_PAGE_FAULT  15
#define CAUSE_FETCH_GUEST_PAGE_FAULT 20
#define CAUSE_LOAD_GUEST_PAGE_FAULT  21
#define CAUSE_VIRTUAL_INST      22
#define CAUSE_STORE_GUEST_PAGE_FAULT 23
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
	/* hypervisor extension. hip, hvip and hie are views of mip and mie,
	 * vsip and vsie views of those through hideleg. */
	word_t hstatus;
	word_t hedeleg;
	word_t hideleg;
	word_t hcounteren;
	word_t henvcfg;
	word_t htval;
	word_t htinst;
	word_t hgatp;
	word_t mtval2;
	word_t mtinst;
	word_t vsstatus;
	word_t vstvec;
	word_t vsscratch;
	word_t vsepc;
	word_t vscause;
	word_t vstval;
	word_t vsatp;
} CSR;

extern CSR csr;
extern uint64_t priv_level;
/* virtualization mode V: set in VS and VU mode */
extern bool virt_mode;
extern uint8_t pmpcfg[PMP_NUMBER_IN_SUSTEMU];
extern word_t pmpaddr[PMP_NUMBER_IN_SUSTEMU];
/* The sustemu-only cache statistics counters (0xc03..0xc08) do not exist on a
 * device under test; a difftest reference hides them. */
extern bool csr_hide_custom_counters;

void csr_reset(void);
/* Whether the current mode may access a CSR: 0, or the cause of the exception
 * the access raises (illegal instruction or virtual instruction). */
int csr_check(uint32_t no, bool is_write);
/* Access a CSR with the privilege and read-only checks of the architecture.
 * A false return means the access raises an exception (see csr_check). In VS
 * mode the supervisor CSRs stand for their VS copies. */
bool csr_read(uint32_t no, word_t *value);
bool csr_write(uint32_t no, word_t value);
/* Take a trap (cause bit 63 set for an interrupt) and return the handler pc.
 * The cause of an interrupt is its bit number in mip. */
vaddr_t csr_trap(word_t cause, vaddr_t epc, word_t tval);
/* The same with the values for mtval2/htval and mtinst/htinst, and whether
 * tval holds a guest virtual address. */
vaddr_t csr_trap_guest(word_t cause, vaddr_t epc, word_t tval, word_t tval2,
		       word_t tinst, bool gva);
vaddr_t csr_mret(void);
vaddr_t csr_sret(void);
/* The interrupt the hart takes now, as its bit number in mip, or -1. `pending`
 * is mip with the bits that are wired to devices. */
int csr_pending_interrupt(word_t pending);
/* The cause code a trap for that interrupt stores: a VS-level interrupt
 * handled in VS mode appears as the supervisor interrupt of the guest. */
int csr_interrupt_code(int number);

void set_csr(uint64_t no, uint64_t data);
word_t read_csr(uint64_t no);
word_t raise_intr(word_t NO, vaddr_t epc, uint32_t args);
void INV(vaddr_t addr);
bool priv_check(uint64_t no, bool is_write);
bool pmp_check(paddr_t addr, bool is_execute, bool is_write);
void mret_priv_transfer();
void exception_priv_transfer();

#endif
