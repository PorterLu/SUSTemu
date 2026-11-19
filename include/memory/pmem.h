#ifndef __PMEM_H__
#define __PMEM_H__

//header included
#include <stdint.h>
#include <common.h>

//macro
#define MBASE 0x80000000
#define MSIZE 0x10000000
#define SERIAL_BASE 0xa00003f8
#define RTC_BASE 0xa0000048

void host_write(void *addr, int len, word_t data);
word_t host_read(void *addr, int len);
uint8_t* guest_to_host(paddr_t paddr);
paddr_t host_to_guest(uint8_t *haddr);
word_t paddr_read(paddr_t addr, int len);
void paddr_write(paddr_t addr, int len, word_t data);

#endif
