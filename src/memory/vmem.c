#include <vmem.h>
#include <pmem.h>
#include <cache.h>
#include <stdio.h>
word_t vaddr_ifetch(vaddr_t addr, int len)
{
	//return paddr_read(addr, len);
	if (addr >= 0x80000000 && addr < 0x88000000)
		return cache_read(L1I_cache, L2_cache, addr, len);
	else 
		return paddr_read(addr, len);
}


word_t vaddr_read(vaddr_t addr, int len)
{
	//return paddr_read(addr, len);
	if (addr >= 0x80000000 && addr < 0x88000000)
		return cache_read(L1D_cache, L2_cache, addr, len);
	else 
		return paddr_read(addr, len);
}

void vaddr_write(vaddr_t addr, int len, word_t data)
{
	//paddr_write(addr, len, data);
	if (addr >= 0x80000000 && addr < 0x88000000)
		cache_write(L1D_cache, L2_cache, addr, len, data);
	else 
		paddr_write(addr, len, data);
}