#ifndef __UTILS_H__
#define __UTILS_h__

#define ANSI_FG_RED 	"\33[1;31m"
#define ANSI_FG_GREEN 	"\33[1;32m"
#define ANSI_FG_BLUE	"\33[1;34m"
#define ANSI_NONE		"\33[0m"

#define ANSI_FMT(str, fmt) fmt str ANSI_NONE
#define RESET_VECTOR 0x80000000
#define ARRLEN(arr) sizeof(arr)/sizeof(arr[0])
#define MAP(c, f) c(f)
#define concat(x, y) x ## y
#define BITMASK(bits) ((1ull << (bits)) - 1)
#define BITS(x, hi, lo) (((x) >> (lo)) & BITMASK((hi) - (lo) + 1)) 							// similar to x[hi:lo] in verilog
#define SEXT(x, len) ({ struct { int64_t n : len; } __x = { .n = x }; (uint64_t)__x.n; })
#define UEXT(x, len) ({ struct { uint64_t n : len;} __x = { .n = x }; (uint64_t)__x.n; })
#define STRLEN(CONST_STR) (sizeof(CONST_STR) - 1)

#endif
