#ifndef __CACHE_H__
#define __CACHE_H__
#include <common.h>

#define BLOCK_SIZE 64      // 64字节块大小
#define WORD_SIZE  8       // 每次访存单位（word_t）
typedef struct {
    int valid;
    int dirty;
    uint64_t tag;
    uint64_t last_access;
    uint8_t data[BLOCK_SIZE]; 
} CacheLine;

typedef struct {
    CacheLine *lines;
} CacheSet;

typedef struct {
    int s;      // 组索引位 (2^s sets)
    int w;      // 关联度
    int off;    // 块偏移位 (2^b = BLOCK_SIZE, 这里应该是 6)
    CacheSet *sets;
    uint64_t timer;
    char *name;

    // 统计数据
    long hits, misses, evictions, writebacks;
} Cache;


extern Cache *L1I_cache;
extern Cache *L1D_cache;
extern Cache *L2_cache;

void cache_report(Cache *c);
word_t cache_read(Cache *l1, Cache *l2, paddr_t addr, int len);
void cache_write(Cache *l1, Cache *l2, paddr_t addr, int len, word_t data);
void init_cache_system();

#endif 