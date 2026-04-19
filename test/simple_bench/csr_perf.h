#ifndef __CSR_PERF_H__
#define __CSR_PERF_H__

/* Convenience macros for reading custom cache-stats HPM counters.
 * Uses inline assembly so they work in bare-metal RISC-V C code. */

#define rdl1d_hits()   ({ long _r; asm volatile("csrr %0, 0xc03" : "=r"(_r)); _r; })
#define rdl1d_misses() ({ long _r; asm volatile("csrr %0, 0xc04" : "=r"(_r)); _r; })
#define rdl1i_hits()   ({ long _r; asm volatile("csrr %0, 0xc05" : "=r"(_r)); _r; })
#define rdl1i_misses() ({ long _r; asm volatile("csrr %0, 0xc06" : "=r"(_r)); _r; })
#define rdl2_hits()    ({ long _r; asm volatile("csrr %0, 0xc07" : "=r"(_r)); _r; })
#define rdl2_misses()  ({ long _r; asm volatile("csrr %0, 0xc08" : "=r"(_r)); _r; })

#endif /* __CSR_PERF_H__ */
