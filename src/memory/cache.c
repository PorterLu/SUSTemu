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

/* --- 初始化函数 --- */
Cache* init_cache(int s, int E, char *name) {
    Cache *c = (Cache *)calloc(1, sizeof(Cache));
    c->s = s; c->E = E; c->b = 6; // 2^6 = 64
    c->name = name;
    int S = 1 << s;
    c->sets = (CacheSet *)malloc(S * sizeof(CacheSet));
    for (int i = 0; i < S; i++) {
        c->sets[i].lines = (CacheLine *)calloc(E, sizeof(CacheLine));
    }
    return c;
}

/* --- 核心查找与 LRU 逻辑 --- */

// 在单层 Cache 中寻找 Block，若未命中则返回牺牲行（Victim）
static CacheLine* find_line(Cache *c, paddr_t addr, int *is_hit) {
    c->timer++;
    uint64_t set_idx = (addr >> c->b) & ((1ULL << c->s) - 1);
    uint64_t tag = addr >> (c->s + c->b);
    CacheSet *set = &c->sets[set_idx];

    int lru_idx = 0;
    uint64_t min_time = UINT64_MAX;

    for (int i = 0; i < c->E; i++) {
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

// 核心功能访存：支持 L1 和 L2 的递归交互
static void fetch_block(Cache *l1, Cache *l2, paddr_t addr, CacheLine *l1_victim) {
    paddr_t block_paddr = addr & ~(paddr_t)(BLOCK_SIZE - 1);
    uint64_t tag_l1 = addr >> (l1->s + l1->b);

    // 1. 处理 L1 驱逐
    if (l1_victim->valid) {
        l1->evictions++;
        if (l1_victim->dirty) {
            // L1 脏块写回 L2
            l1->writebacks++;
            paddr_t v_addr = (l1_victim->tag << (l1->s + l1->b)) | 
                             (((addr >> l1->b) & ((1ULL << l1->s) - 1)) << l1->b);
            int hit_l2;
            CacheLine *l2_line = find_line(l2, v_addr, &hit_l2);
            // 将数据拷入 L2 对应行（无论 L2 之前是否命中，强制更新/分配）
            memcpy(l2_line->data, l1_victim->data, BLOCK_SIZE);
            l2_line->valid = 1;
            l2_line->dirty = 1; 
            l2_line->tag = v_addr >> (l2->s + l2->b);
            l2_line->last_access = l2->timer;
        }
    }

    // 2. 从 L2 或 内存 加载新块到 L1
    int hit_l2;
    CacheLine *l2_line = find_line(l2, addr, &hit_l2);
    if (!hit_l2) {
        // L2 Miss: 检查 L2 驱逐
        if (l2_line->valid) {
            l2->evictions++;
            if (l2_line->dirty) {
                l2->writebacks++;
                paddr_t v_l2_addr = (l2_line->tag << (l2->s + l2->b)) | 
                                    (((addr >> l2->b) & ((1ULL << l2->s) - 1)) << l2->b);
                store_block_to_mem(v_l2_addr, l2_line->data);
            }
        }
        // 从真实内存加载到 L2
        load_block_from_mem(block_paddr, l2_line->data);
        l2_line->valid = 1;
        l2_line->dirty = 0;
        l2_line->tag = addr >> (l2->s + l2->b);
    }

    // 3. 将数据从 L2 拷入 L1
    memcpy(l1_victim->data, l2_line->data, BLOCK_SIZE);
    l1_victim->valid = 1;
    l1_victim->dirty = 0;
    l1_victim->tag = tag_l1;
    l1_victim->last_access = l1->timer;
}

/* --- 对外暴露的读写接口 --- */
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
