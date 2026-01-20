#include "cache.h"
#include "pmem.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Cache *L1I_cache = NULL;
Cache *L1D_cache = NULL;
Cache *L2_cache = NULL;

// 从底层物理内存加载一整块 (64字节)
static void load_block_from_mem(paddr_t block_paddr, uint8_t *dest) {
    for (int i = 0; i < BLOCK_SIZE; i += WORD_SIZE) {
        word_t val = paddr_read(block_paddr + i, WORD_SIZE);
        memcpy(dest + i, &val, WORD_SIZE);
    }
}

// 将一整块 (64字节) 写回底层物理内存
static void store_block_to_mem(paddr_t block_paddr, uint8_t *src) {
    for (int i = 0; i < BLOCK_SIZE; i += WORD_SIZE) {
        word_t val;
        memcpy(&val, src + i, WORD_SIZE);
        paddr_write(block_paddr + i, WORD_SIZE, val);
    }
}

// 初始化 Cache 结构
Cache* init_cache(int s, int w, char *name) {
    Cache *c = (Cache *)calloc(1, sizeof(Cache));
    c->s = s; c->w = w; c->off = 6; // 2^6 = 64
    c->name = name;
    int S = 1 << s;
    c->sets = (CacheSet *)malloc(S * sizeof(CacheSet));
    for (int i = 0; i < S; i++) {
        c->sets[i].lines = (CacheLine *)calloc(w, sizeof(CacheLine));
    }
    return c;
}

// 在单层 Cache 中寻找 Block，若未命中则返回牺牲行
static CacheLine* find_line(Cache *c, paddr_t addr, int *is_hit) {
    c->timer++;
    uint64_t set_idx = (addr >> c->off) & ((1ULL << c->s) - 1);
    uint64_t tag = addr >> (c->s + c->off);
    CacheSet *set = &c->sets[set_idx];

    int lru_idx = 0;
    uint64_t min_time = UINT64_MAX;

    for (int i = 0; i < c->w; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag) {
            *is_hit = 1;
            c->hits++;
            set->lines[i].last_access = c->timer;
            return &set->lines[i];
        }
        if (set->lines[i].last_access < min_time) {
            min_time = set->lines[i].last_access;
            lru_idx = i;
        }
    }

    *is_hit = 0;
    c->misses++;
    return &set->lines[lru_idx];
}

// 统一的 L2 访存接口：is_write 为 true 表示 L1 写回脏块，false 表示 L1 读缺失
static void access_l2(Cache *l2, paddr_t addr, uint8_t *data, bool is_write) {
    int hit;
    CacheLine *l2_line = find_line(l2, addr, &hit);
    paddr_t block_paddr = addr & ~(paddr_t)(BLOCK_SIZE - 1);

    if (hit) {
        if (is_write) {
            memcpy(l2_line->data, data, BLOCK_SIZE);
            l2_line->dirty = 1;
        } else {
            memcpy(data, l2_line->data, BLOCK_SIZE);
        }
    } else {
        // L2 Miss: 处理替换逻辑
        if (l2_line->valid && l2_line->dirty) {
            l2->evictions++;
            l2->writebacks++;
            paddr_t p_l2_addr = (l2_line->tag << (l2->s + l2->off)) | 
                                (((addr >> l2->off) & ((1ULL << l2->s) - 1)) << l2->off);
            store_block_to_mem(p_l2_addr, l2_line->data);
        }

        if (is_write) {
            // L1 写回 L2：直接覆盖 L2 牺牲行
            memcpy(l2_line->data, data, BLOCK_SIZE);
            l2_line->dirty = 1;
        } else {
            // L1 缺失从 L2 读：先从内存加载到 L2，再交给 L1
            load_block_from_mem(block_paddr, l2_line->data);
            memcpy(data, l2_line->data, BLOCK_SIZE);
            l2_line->dirty = 0;
        }
        l2_line->valid = 1;
        l2_line->tag = addr >> (l2->s + l2->off);
    }
}

static void fetch_block(Cache *l1, Cache *l2, paddr_t addr, CacheLine *l1_victim) {
    // 1. 如果 L1 牺牲行是脏的，将其写回 L2
    if (l1_victim->valid) {
        l1->evictions++;
        if (l1_victim->dirty) {
            l1->writebacks++;
            paddr_t p_l1_addr = (l1_victim->tag << (l1->s + l1->off)) | 
                                (((addr >> l1->off) & ((1ULL << l1->s) - 1)) << l1->off);
            access_l2(l2, p_l1_addr, l1_victim->data, true);
        }
    }

    // 2. 从 L2 获取新块填充到 L1
    access_l2(l2, addr, l1_victim->data, false);

    // 3. 更新 L1 行状态
    l1_victim->valid = 1;
    l1_victim->dirty = 0;
    l1_victim->tag = addr >> (l1->s + l1->off);
    l1_victim->last_access = l1->timer;
}


word_t cache_read(Cache *l1, Cache *l2, paddr_t addr, int len) {
    int hit;
    CacheLine *line = find_line(l1, addr, &hit);
    if (!hit) {
        fetch_block(l1, l2, addr, line);
    }
    
    word_t res = 0;
    uint32_t offset = addr & (BLOCK_SIZE - 1);
    memcpy(&res, line->data + offset, len);
    return res;
}

void cache_write(Cache *l1, Cache *l2, paddr_t addr, int len, word_t data) {
    int hit;
    CacheLine *line = find_line(l1, addr, &hit);
    if (!hit) {
        fetch_block(l1, l2, addr, line);
    }

    uint32_t offset = addr & (BLOCK_SIZE - 1);
    memcpy(line->data + offset, &data, len);
    line->dirty = 1; // 写操作标记脏位
}

// 打印报告
void cache_report(Cache *c) {
    double rate = (c->hits + c->misses == 0) ? 0 : (double)c->misses / (c->hits + c->misses) * 100;
    printf("[%16s] Hits:%16ld Misses:%16ld Evict:%16ld WB:%ld MissRate:%16.2f%%\n",
           c->name, c->hits, c->misses, c->evictions, c->writebacks, rate);
}

void init_cache_system() {
    L1I_cache = init_cache(6, 8, "L1-Instruction"); // 32KB
    L1D_cache = init_cache(6, 8, "L1-Data");        // 32KB
    L2_cache  = init_cache(8, 16, "L2-Unified");    // 256KB
}
