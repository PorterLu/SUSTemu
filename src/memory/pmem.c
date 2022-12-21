#include <pmem.h>
#include <mmio.h>
#include <stdio.h>
#include <reg.h>
#include <state.h>
#include <mtrace.h>
#include <debug.h>
#include <csr.h>

uint8_t pmem[MSIZE];

word_t host_read(void *addr, int len) {
  switch (len) {
    case 1: return *(uint8_t  *)addr;
    case 2: return *(uint16_t *)addr;
    case 4: return *(uint32_t *)addr;
    case 8: return *(uint64_t *)addr;
  }

  return 0;
}

void host_write(void *addr, int len, word_t data) {
  switch (len) {
    case 1: *(uint8_t  *)addr = data; return;
    case 2: *(uint16_t *)addr = data; return;
    case 4: *(uint32_t *)addr = data; return;
	case 8: *(uint64_t *)addr = data; return;
  }
}

word_t paddr_read(paddr_t addr, int len)
{
	pmp_check(addr, false, false);
	addr = addr & 0xffffffff;
	if(addr >= 0x80000000 && addr <= 0x88000000)
		return host_read(guest_to_host(addr), len);
	else
		return mmio_read(addr, len);
	
	panic("should not reach here, read addr %lx\n", addr);
}

void paddr_write(paddr_t addr, int len, word_t data)
{
	pmp_check(addr, false, true);
	addr = addr & 0xffffffff;
	if(addr >= 0x80000000 && addr <= 0x88000000){
		host_write(guest_to_host(addr), len, data);
		return;
	}
	else
		mmio_write(addr, len, data);
}

uint8_t* guest_to_host(paddr_t paddr) { return pmem +  paddr - MBASE;}
paddr_t host_to_guest(uint8_t *haddr) { return haddr - pmem + MBASE;}
