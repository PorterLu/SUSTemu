#ifndef __VMEM_H__
#define __VMEM_H__

#include <stdint.h>
#include <pmem.h>
#include <common.h>

typedef uint64_t vaddr_t; 

word_t vaddr_ifetch(vaddr_t addr, int len);
word_t vaddr_read(vaddr_t addr, int len);
void vaddr_write(vaddr_t addr, int len, word_t data);

/* vaddr_read_level — like vaddr_read() but also returns cache hit level.
 * Returns: 0=L1 hit, 1=L2 hit, 2=DRAM.  Value written to *out_val. */
int vaddr_read_level(vaddr_t addr, int len, word_t *out_val);

/* vaddr_probe_level — probe cache level WITHOUT filling or reading data.
 * Returns: 0=L1 hit, 1=L2 hit, 2=DRAM.  No side effects on cache state. */
int vaddr_probe_level(vaddr_t addr);

/* vaddr_fill_and_read — fill cache line from L2/DRAM into L1, return data.
 * Called when the OOO MEM-stage countdown expires (deferred cache fill). */
word_t vaddr_fill_and_read(vaddr_t addr, int len);

/* vaddr_probe_and_read_l1 — single-lookup probe + optional read for L1 hit.
 * Uses cache_probe_and_read_l1; on L1 hit reads data in the same lookup.
 * On miss returns the cache level without side effects on L1.
 * Returns: 0 = L1 hit (data in *out_val), 1 = L2 hit, 2 = DRAM. */
int vaddr_probe_and_read_l1(vaddr_t addr, int len, word_t *out_val);


#endif