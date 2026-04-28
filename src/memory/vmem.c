#include <vmem.h>
#include <pmem.h>
#include <cache.h>
#include <core.h>
#include <stdio.h>

word_t vaddr_ifetch(vaddr_t addr, int len)
{
	if (addr >= 0x80000000 && addr < 0x88000000)
		return cache_read(L1I_cache, L2_cache, addr, len);
	else
		return paddr_read(addr, len);
}

word_t vaddr_read(vaddr_t addr, int len)
{
	if (addr >= 0x80000000 && addr < 0x88000000) {
		/* Multi-core: flush any dirty copy in the other core's L1D */
		if (g_num_cores > 1) {
			int other = 1 - g_current_hartid;
			cache_snoop_flush_dirty(cores[other].l1d, L2_cache, addr);
		}
		return cache_read(L1D_cache, L2_cache, addr, len);
	} else {
		return paddr_read(addr, len);
	}
}

int vaddr_read_level(vaddr_t addr, int len, word_t *out_val)
{
	if (addr >= 0x80000000 && addr < 0x88000000) {
		if (g_num_cores > 1) {
			int other = 1 - g_current_hartid;
			cache_snoop_flush_dirty(cores[other].l1d, L2_cache, addr);
		}
		return cache_read_level(L1D_cache, L2_cache, addr, len, out_val);
	} else {
		*out_val = paddr_read(addr, len);
		return 0;   /* Direct DRAM mapped as L1-hit latency for non-cache range */
	}
}

int vaddr_probe_level(vaddr_t addr)
{
	if (addr >= 0x80000000 && addr < 0x88000000)
		return cache_probe_level(L1D_cache, L2_cache, addr);
	return 0;   /* Non-cache range: treat as L1 hit */
}

word_t vaddr_fill_and_read(vaddr_t addr, int len)
{
	if (addr >= 0x80000000 && addr < 0x88000000) {
		if (g_num_cores > 1) {
			int other = 1 - g_current_hartid;
			cache_snoop_flush_dirty(cores[other].l1d, L2_cache, addr);
		}
		return cache_fill_and_read(L1D_cache, L2_cache, addr, len);
	} else {
		return paddr_read(addr, len);
	}
}

int vaddr_probe_and_read_l1(vaddr_t addr, int len, word_t *out_val)
{
	if (addr >= 0x80000000 && addr < 0x88000000) {
		if (g_num_cores > 1) {
			int other = 1 - g_current_hartid;
			cache_snoop_flush_dirty(cores[other].l1d, L2_cache, addr);
		}
		return cache_probe_and_read_l1(L1D_cache, L2_cache, addr, len, out_val);
	} else {
		/* Non-cacheable range (MMIO or invalid speculative addr).
		 * Return L1-hit latency and let lsu_do_fill handle it
		 * (speculative loads to invalid addrs are faulted there). */
		*out_val = 0;
		return 0;
	}
}

void vaddr_write(vaddr_t addr, int len, word_t data)
{
	if (addr >= 0x80000000 && addr < 0x88000000) {
		/* Multi-core: invalidate the other core's L1D line */
		if (g_num_cores > 1) {
			int other = 1 - g_current_hartid;
			cache_snoop_invalidate(cores[other].l1d, addr);
		}
		cache_write(L1D_cache, L2_cache, addr, len, data);
	} else {
		paddr_write(addr, len, data);
	}
}
