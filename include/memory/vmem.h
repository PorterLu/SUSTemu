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


#endif