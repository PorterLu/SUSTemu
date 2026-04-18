#ifndef MEM_H
#define MEM_H

#include "common.h"
#include "mmc.h"

// Single byte
byte memory_readb(word address);
void memory_writeb(word address, byte data);

// Two bytes (word), LSB first
word memory_readw(word address);
void memory_writew(word address, word data);

/* Exposed for fast-path inlining */
extern byte memory[];   /* NES ROM/SRAM (mmc.c) */
extern byte CPU_RAM[];  /* NES CPU RAM  (cpu.c)  */

/* Fast-path memory read: avoids function-call dispatch for the two most
 * common cases (ROM fetch ~60%, RAM ~30%).  PPU/PSG still use slow path. */
static inline byte memory_readb_fast(word address) {
  if (__builtin_expect(address >= 0x8000, 1)) return memory[address];
  if (__builtin_expect(address < 0x2000,  0)) return CPU_RAM[address & 0x07FF];
  return memory_readb(address);
}

#endif
