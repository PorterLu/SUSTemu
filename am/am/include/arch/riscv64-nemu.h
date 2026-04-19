#ifndef ARCH_H__
#define ARCH_H__

/*
struct Context {
  // TODO: fix the order of these members to match trap.S
  uintptr_t mepc, mcause, gpr[32], mstatus;
  void *pdir;
};*/
/*
#define REGS(f) \
f(0)  f( 1) f(2) f( 3) f( 4) f( 5) f( 6) f( 7) f( 8) f( 9) \
f(10) f(11) f(12) f(13) f(14) f(15) f(16) f(17) f(18) f(19) \
f(20) f(21) f(22) f(23) f(24) f(25) f(26) f(27) f(28) f(29) \
f(30) f(31)*/

//#define GPR(i) GPR ## i,

struct Context{
	uintptr_t  gpr[32], mcause, mstatus,  mepc;
	void *pdir;
};


#define GPR1 gpr[17] // a7
#define GPR2 gpr[10]
#define GPR3 gpr[11]
#define GPR4 gpr[12]
#define GPRx gpr[10]

#endif
