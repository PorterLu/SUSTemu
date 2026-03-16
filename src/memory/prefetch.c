#include "prefetch.h"
#include "cache.h"   // BLOCK_SIZE

paddr_t prefetch_hint(paddr_t addr)
{
    /* todo: 顺序预取 — 返回 miss 块之后紧接的那个块的起始地址
     * 返回 0 表示不预取（当前占位符保持不变时系统行为与无预取相同）*/
    return 0;
}
