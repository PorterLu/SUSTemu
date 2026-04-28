#include "cache.h"
#include "pmem.h"
#include "prefetch.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Cache *L1I_cache = NULL;
Cache *L1D_cache = NULL;
Cache *L2_cache = NULL;

// 从底层物理内存加载一整块 (64字节)
static void load_block_from_mem(paddr_t block_paddr, uint8_t *dest) {
    /* Fast path: address is in normal physical memory — single 64-byte memcpy
     * avoids 8× (paddr_read + pmp_check + range-check + memcpy) overhead. */
    paddr_t pa = block_paddr & 0xffffffff;
    if (__builtin_expect(pa >= MBASE && pa + BLOCK_SIZE <= MBASE + MSIZE, 1)) {
        memcpy(dest, guest_to_host(pa), BLOCK_SIZE);
        return;
    }
    /* Fallback for MMIO or out-of-range addresses */
    for (int i = 0; i < BLOCK_SIZE; i += WORD_SIZE) {
        word_t val = paddr_read(block_paddr + i, WORD_SIZE);
        memcpy(dest + i, &val, WORD_SIZE);
    }
}

// 将一整块 (64字节) 写回底层物理内存
static void store_block_to_mem(paddr_t block_paddr, uint8_t *src) {
    paddr_t pa = block_paddr & 0xffffffff;
    if (__builtin_expect(pa >= MBASE && pa + BLOCK_SIZE <= MBASE + MSIZE, 1)) {
        memcpy(guest_to_host(pa), src, BLOCK_SIZE);
        return;
    }
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
        c->sets[i].mru_way = -1;
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

    /* Fast path: check MRU way first */
    int mru = set->mru_way;
    if (mru >= 0 && set->lines[mru].valid && set->lines[mru].tag == tag) {
        *is_hit = 1;
        c->hits++;
        set->lines[mru].last_access = c->timer;
        return &set->lines[mru];
    }

    for (int i = 0; i < c->w; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag) {
            *is_hit = 1;
            c->hits++;
            set->lines[i].last_access = c->timer;
            set->mru_way = i;
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
        // 不命中 L2，需要处理 L2 Victim Line
        if (l2_line->valid && l2_line->dirty) {
            l2->evictions++;
            l2->writebacks++;
            paddr_t p_l2_addr = (l2_line->tag << (l2->s + l2->off)) | 
                                (((addr >> l2->off) & ((1ULL << l2->s) - 1)) << l2->off);
            store_block_to_mem(p_l2_addr, l2_line->data);
        }

        if (is_write) {
            // L1 写回 L2：直接覆盖 L2 Victim Line
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
        l2_line->last_access = l2->timer;
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

// 静默查找：不更新 hits/misses/timer，仅用于预取决策
static CacheLine* find_line_quiet(Cache *c, paddr_t addr, int *is_hit) {
    uint64_t set_idx = (addr >> c->off) & ((1ULL << c->s) - 1);
    uint64_t tag = addr >> (c->s + c->off);
    CacheSet *set = &c->sets[set_idx];
    int lru_idx = 0;
    uint64_t min_time = UINT64_MAX;

    /* Fast path: check MRU way first */
    int mru = set->mru_way;
    if (mru >= 0 && set->lines[mru].valid && set->lines[mru].tag == tag) {
        *is_hit = 1;
        return &set->lines[mru];
    }

    for (int i = 0; i < c->w; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag) {
            *is_hit = 1;
            set->mru_way = i;
            return &set->lines[i];
        }
        if (set->lines[i].last_access < min_time) {
            min_time = set->lines[i].last_access;
            lru_idx = i;
        }
    }
    *is_hit = 0;
    return &set->lines[lru_idx];
}

void cache_prefetch(Cache *l1, Cache *l2, paddr_t miss_addr) {
    paddr_t cur = miss_addr;
    for (int i = 0; i < PREFETCH_DEGREE; i++) {
        paddr_t hint = prefetch_hint(cur);
        if (hint == 0) break;
        int already;
        CacheLine *v = find_line_quiet(l1, hint, &already);
        if (!already)
            fetch_block(l1, l2, hint, v);
        cur = hint;
    }
}


word_t cache_read(Cache *l1, Cache *l2, paddr_t addr, int len) {
    int hit;
    CacheLine *line = find_line(l1, addr, &hit);
    if (!hit) {
        fetch_block(l1, l2, addr, line);
        cache_prefetch(l1, l2, addr);
    }

    word_t res = 0;
    uint32_t offset = addr & (BLOCK_SIZE - 1);
    memcpy(&res, line->data + offset, len);
    return res;
}

/* cache_read_level — like cache_read() but returns the cache level that
 * serviced the request: 0=L1 hit, 1=L2 hit, 2=DRAM (both L1 and L2 missed).
 * The loaded value is written to *out_val.
 * Used by the OOO pipeline MEM stage to determine stall duration. */
int cache_read_level(Cache *l1, Cache *l2, paddr_t addr, int len, word_t *out_val) {
    int l1_hit;
    CacheLine *line = find_line(l1, addr, &l1_hit);
    int level;
    if (!l1_hit) {
        /* Probe L2 without modifying statistics */
        int l2_hit;
        find_line_quiet(l2, addr, &l2_hit);
        level = l2_hit ? 1 : 2;
        fetch_block(l1, l2, addr, line);
        cache_prefetch(l1, l2, addr);
    } else {
        level = 0;
    }
    word_t res = 0;
    uint32_t offset = addr & (BLOCK_SIZE - 1);
    memcpy(&res, line->data + offset, len);
    *out_val = res;
    return level;
}

/* cache_probe_level — probe the cache hierarchy to determine which level
 * would service addr, WITHOUT filling any cache line.
 * Returns: 0=L1 hit, 1=L2 hit, 2=DRAM.
 * Used by the OOO MEM stage on first access to decide stall duration. */
int cache_probe_level(Cache *l1, Cache *l2, paddr_t addr)
{
    int l1_hit;
    find_line_quiet(l1, addr, &l1_hit);
    if (l1_hit) return 0;
    int l2_hit;
    find_line_quiet(l2, addr, &l2_hit);
    return l2_hit ? 1 : 2;
}

/* cache_fill_and_read — fill the cache line (fetch from L2/DRAM into L1)
 * and return the data at addr.  Called when the MEM-stage countdown reaches
 * zero, modelling that the block arrives in L1 only after the full latency. */
word_t cache_fill_and_read(Cache *l1, Cache *l2, paddr_t addr, int len)
{
    int hit;
    CacheLine *line = find_line(l1, addr, &hit);
    if (!hit) {
        fetch_block(l1, l2, addr, line);
        cache_prefetch(l1, l2, addr);
    }
    word_t res = 0;
    uint32_t offset = addr & (BLOCK_SIZE - 1);
    memcpy(&res, line->data + offset, len);
    return res;
}

int cache_probe_and_read_l1(Cache *l1, Cache *l2, paddr_t addr, int len, word_t *out_val)
{
    int l1_hit;
    CacheLine *line = find_line_quiet(l1, addr, &l1_hit);
    if (l1_hit) {
        /* Single lookup for L1 hit: apply stats that find_line would do.
         * find_line_quiet already set mru_way; we fill in timer/hits/last_access. */
        l1->timer++;
        l1->hits++;
        line->last_access = l1->timer;

        uint32_t offset = addr & (BLOCK_SIZE - 1);
        memcpy(out_val, line->data + offset, len);
        return 0;
    }
    /* L1 miss: probe L2 quietly, no L1 stats touched (fill happens later) */
    int l2_hit;
    find_line_quiet(l2, addr, &l2_hit);
    return l2_hit ? 1 : 2;
}

void cache_write(Cache *l1, Cache *l2, paddr_t addr, int len, word_t data) {
    int hit;
    CacheLine *line = find_line(l1, addr, &hit);
    if (!hit) {
        fetch_block(l1, l2, addr, line);
        cache_prefetch(l1, l2, addr);
    }

    uint32_t offset = addr & (BLOCK_SIZE - 1);
    memcpy(line->data + offset, &data, len);
    line->dirty = 1; // 写操作标记脏位
}

// 打印报告
void cache_report(Cache *c) {
    long total = c->hits + c->misses;
    double hit_rate  = (total == 0) ? 0.0 : (double)c->hits  / total * 100.0;
    double miss_rate = (total == 0) ? 0.0 : (double)c->misses / total * 100.0;
    printf("  [%-16s] accesses: %8ld  hits: %8ld (%6.2f%%)  misses: %8ld (%6.2f%%)  evict: %6ld  wb: %6ld\n",
           c->name, total, c->hits, hit_rate, c->misses, miss_rate,
           c->evictions, c->writebacks);
}

void init_cache_system() {
    L1I_cache = init_cache(6, 8, "L1-Instruction"); // 32KB
    L1D_cache = init_cache(L1D_S, 8, "L1-Data");
    L2_cache  = init_cache(8, 16, "L2-Unified");    // 256KB
}

/* ── Cache coherency helpers ─────────────────────────────────────────────── */

/*
 * cache_snoop_invalidate — invalidate the cache line holding addr in
 * other_l1d.  Called after a write to addr by another core.
 * If the line is dirty, it is written back to L2 first (write-invalidate).
 */
void cache_snoop_invalidate(Cache *other_l1d, paddr_t addr)
{
    if (!other_l1d) return;

    uint64_t set_idx = (addr >> other_l1d->off) & ((1ULL << other_l1d->s) - 1);
    uint64_t tag     = addr >> (other_l1d->s + other_l1d->off);
    CacheSet *set    = &other_l1d->sets[set_idx];

    for (int i = 0; i < other_l1d->w; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag) {
            if (set->lines[i].dirty) {
                /* Write back dirty data to L2 before invalidating */
                paddr_t block_paddr = addr & ~(paddr_t)(BLOCK_SIZE - 1);
                access_l2(L2_cache, block_paddr, set->lines[i].data, true);
                other_l1d->writebacks++;
            }
            set->lines[i].valid = 0;
            set->lines[i].dirty = 0;
            break;
        }
    }
}

/*
 * cache_snoop_flush_dirty — if src_l1d has a dirty line for addr, flush it
 * to L2 (but keep the line valid/clean).  Called before another core reads
 * addr so it sees the latest value.
 */
void cache_snoop_flush_dirty(Cache *src_l1d, Cache *l2, paddr_t addr)
{
    if (!src_l1d) return;

    uint64_t set_idx = (addr >> src_l1d->off) & ((1ULL << src_l1d->s) - 1);
    uint64_t tag     = addr >> (src_l1d->s + src_l1d->off);
    CacheSet *set    = &src_l1d->sets[set_idx];

    for (int i = 0; i < src_l1d->w; i++) {
        if (set->lines[i].valid && set->lines[i].tag == tag
                && set->lines[i].dirty) {
            paddr_t block_paddr = addr & ~(paddr_t)(BLOCK_SIZE - 1);
            access_l2(l2, block_paddr, set->lines[i].data, true);
            set->lines[i].dirty = 0;
            src_l1d->writebacks++;
            break;
        }
    }
}
