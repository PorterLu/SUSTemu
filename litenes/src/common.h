#ifndef COMMON_H
#define COMMON_H

#include <am.h>
#include <klib.h>
#include <klib-macros.h>

typedef uint8_t byte;
typedef uint16_t word;
typedef uint32_t dword;

/* Inline bit operations — expanded directly in callers, no function-call
 * overhead on the simulated RISC-V CPU.  Also retained as out-of-line
 * functions in common.c for compatibility with function-pointer users. */
static inline bool common_bit_set(long long value, byte position) {
    return value & (1L << position);
}

#define _COMMON_BITOPS(SUFFIX, TYPE) \
    static inline void common_set_bit##SUFFIX(TYPE *v, byte p)    { *v |= (TYPE)(1L << p); } \
    static inline void common_unset_bit##SUFFIX(TYPE *v, byte p)  { *v &= (TYPE)(~(1L << p)); } \
    static inline void common_toggle_bit##SUFFIX(TYPE *v, byte p) { *v ^= (TYPE)(1L << p); } \
    static inline void common_modify_bit##SUFFIX(TYPE *v, byte p, bool set) { \
        set ? common_set_bit##SUFFIX(v, p) : common_unset_bit##SUFFIX(v, p); \
    }

_COMMON_BITOPS(b, byte)
_COMMON_BITOPS(w, word)
_COMMON_BITOPS(d, dword)

#undef _COMMON_BITOPS

#endif
