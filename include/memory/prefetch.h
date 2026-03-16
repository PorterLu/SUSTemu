#ifndef __PREFETCH_H__
#define __PREFETCH_H__
#include <common.h>

/* 学生实现此函数（src/memory/prefetch.c）
 * addr: 刚发生 L1D miss 的物理地址
 * 返回: 需要预取的地址；返回 0 = 跳过（接口安全空操作） */
paddr_t prefetch_hint(paddr_t addr);

#endif
