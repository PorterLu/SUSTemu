#include <vmem.h>
#include <pmem.h>
#include <cache.h>

word_t vaddr_ifetch(vaddr_t addr, int len)
{
	//return paddr_read(addr, len);
	return cache_read(L1I_cache, L2_cache, addr, len);
}


word_t vaddr_read(vaddr_t addr, int len)
{
	//return paddr_read(addr, len);
	return cache_read(L1D_cache, L2_cache, addr, len);
}

void vaddr_write(vaddr_t addr, int len, word_t data)
{
	//paddr_write(addr, len, data);
	cache_write(L1D_cache, L2_cache, addr, len, data);
}