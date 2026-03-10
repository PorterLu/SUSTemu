# SUSTemu Cache Labs

> 实验环境：`make bench-ooo` 运行 Dhrystone，`make bench-dual` 运行双核测试。
> 缓存统计在每次运行结束时自动打印（`=== Cache Statistics ===`）。
> 当前默认配置：L1I/L1D 各 32 KB（s=6, w=8），L2 256 KB（s=8, w=16），块大小 64 B。

---

## Lab 1：Cache 参数对命中率与 IPC 的影响

### 背景

Cache 的容量（组数 `2^s`）与关联度（路数 `w`）直接影响冲突缺失（conflict miss）和容量缺失（capacity miss）的比例。本实验通过修改 `init_cache_system()` 中的参数，观察不同配置下 Dhrystone 的 L1D 命中率与 OOO 引擎 IPC 的变化。

### 实验步骤

1. **修改 L1D 容量**：在 `src/memory/cache.c` 的 `init_cache_system()` 中，把 L1D 的 `s` 参数从 6 改为 3、4、5、6、7（对应 512 B、1 KB、2 KB、4 KB、8 KB……直至 128 KB），`w` 保持 8 路不变。

   ```c
   // 修改这一行：
   L1D_cache = init_cache(s_val, 8, "L1-Data");
   ```

2. **修改 L1D 关联度**：固定 `s=6`（32 KB），把 `w` 改为 1、2、4、8、16，观察关联度对冲突缺失的影响。

3. 每次修改后执行：
   ```bash
   make bench-ooo
   ```
   记录输出中的 `L1-Data` 命中率与 `IPC`。

### 记录表格

| L1D 大小 | s  | w  | L1D Hit Rate | L1D Miss Rate | IPC   |
|----------|----|----|--------------|---------------|-------|
| 512 B    | 3  | 8  |              |               |       |
| 2 KB     | 5  | 8  |              |               |       |
| 32 KB    | 6  | 8  | *(baseline)* | *(baseline)*  | 0.979 |
| 128 KB   | 7  | 8  |              |               |       |
| DM (1-way)| 6 | 1  |              |               |       |
| 4-way    | 6  | 4  |              |               |       |

### 思考题

1. L1D 命中率从何时开始趋于饱和？说明了 Dhrystone 的工作集（working set）大约多大？
2. 直接映射（w=1）与 8 路组相联相比，命中率差距来自哪类缺失（3C 模型：cold / capacity / conflict）？
3. L2 命中率始终接近 0%，说明了什么？（提示：观察 L2 access 总次数）

---

## Lab 2：矩阵乘法访问顺序对缓存命中率的影响

### 背景

矩阵乘法 `C = A × B`（N×N，`double`）的三重循环有多种等价写法，但访存模式完全不同。行优先存储（row-major）下，`B[k][j]` 的列访问是典型的 cache-unfriendly 访问，会产生大量 capacity/conflict 缺失。

### 实验程序

在 `test/matmul/` 下创建以下文件（参考 `test/dhrystone/` 目录结构），在裸金属 RISC-V 上运行三种循环顺序：

```c
// 三种循环顺序（改变 i/j/k 的嵌套次序）

// (A) ijk — 经典写法，B 列访问 cache-unfriendly
for (i=0; i<N; i++)
  for (j=0; j<N; j++)
    for (k=0; k<N; k++)
      C[i][j] += A[i][k] * B[k][j];   // B[k][j]: 跨行跨步访问

// (B) ikj — B 行访问 cache-friendly
for (i=0; i<N; i++)
  for (k=0; k<N; k++)
    for (j=0; j<N; j++)
      C[i][j] += A[i][k] * B[k][j];   // B[k][j]: j 连续，行访问

// (C) 分块 (tiled) — 提升时间局部性
#define TILE 8
for (i=0; i<N; i+=TILE)
  for (k=0; k<N; k+=TILE)
    for (j=0; j<N; j+=TILE)
      for (ii=i; ii<i+TILE; ii++)
        for (kk=k; kk<k+TILE; kk++)
          for (jj=j; jj<j+TILE; jj++)
            C[ii][jj] += A[ii][kk] * B[kk][jj];
```

建议 N=16（16×16 double 矩阵，占 2 KB，适合在 32 KB L1 内观察冲突但不被 capacity 掩盖）。

### 步骤

1. 创建 `test/matmul/matmul.c`（包含三个函数 `matmul_ijk`、`matmul_ikj`、`matmul_tiled`）及对应的 `start.S`、`linker.ld`、`Makefile`（参考 `test/dhrystone/`）。
2. 在 `Makefile` 中新增 `bench-matmul` 目标。
3. 分别用 `--ooo --bpred` 运行，记录：

| 变体    | L1D Hit Rate | L1D Misses | IPC   |
|---------|--------------|------------|-------|
| ijk     |              |            |       |
| ikj     |              |            |       |
| tiled   |              |            |       |

### 思考题

1. `ijk` 与 `ikj` 的 L1D 缺失数差距主要来自 `B` 矩阵的哪种缺失类型？
2. 分块后命中率提升的原因是什么？块大小 `TILE` 与 cache 行大小（64 B）有何关系？
3. 如果把 N 增大到 64（超出 L1D 容量），三种变体的命中率差距会变大还是变小？为什么？

---

## Lab 3：TODO — 实现流水线预取器（Prefetcher）消除冷缺失

### 背景

Lab 1 和 Lab 2 中 L1 的第一次访问（cold miss）无法通过增大 cache 消除。**预取器**在数据被实际使用前提前把块装入 cache，将冷缺失的延迟隐藏到执行时间内。

### 目标

在 `src/memory/cache.c` 中实现一个 **步幅预取器（stride prefetcher）**，在每次 L1 缺失时预取接下来 `N_PREFETCH` 个连续块。

### 接口设计

```c
// include/memory/cache.h 新增
#define PREFETCH_DEGREE 2   /* 每次缺失预取向后 2 块 */

void cache_prefetch(Cache *l1, Cache *l2, paddr_t miss_addr);
```

### 实现要点

```c
void cache_prefetch(Cache *l1, Cache *l2, paddr_t miss_addr)
{
    /* 对 miss 块之后的 PREFETCH_DEGREE 个块，
     * 若不在 L1 中则静默填充（不影响命中/缺失统计）。
     * 提示：复用 fetch_block()，但需新增一个 prefetch_hits 计数器
     * 统计"预取命中"（prefetch block was later accessed）。 */

    paddr_t block_base = miss_addr & ~(paddr_t)(BLOCK_SIZE - 1);
    for (int i = 1; i <= PREFETCH_DEGREE; i++) {
        paddr_t next = block_base + i * BLOCK_SIZE;
        // TODO: 检查 next 是否在合法地址范围内
        // TODO: 检查 next 是否已在 L1（避免无效预取）
        // TODO: 调用 fetch_block() 填充，但不更新 hits/misses 计数
    }
}
```

### 调用位置

在 `cache_read()` / `cache_write()` 的 `!hit` 分支末尾调用：

```c
word_t cache_read(Cache *l1, Cache *l2, paddr_t addr, int len) {
    int hit;
    CacheLine *line = find_line(l1, addr, &hit);
    if (!hit) {
        fetch_block(l1, l2, addr, line);
        cache_prefetch(l1, l2, addr);   // <-- 新增
    }
    ...
}
```

### 验证指标

实现后在 Lab 2 的 `ijk` 矩阵乘法上对比：

| 配置              | L1D Cold Misses | L1D Total Misses | IPC   |
|-------------------|-----------------|------------------|-------|
| 无预取（baseline）|                 |                  |       |
| degree=1          |                 |                  |       |
| degree=2          |                 |                  |       |
| degree=4          |                 |                  |       |

### 思考题

1. 预取度过大会带来什么负面影响（提示：cache 污染、带宽浪费）？
2. 步幅预取器对 `ijk` 变体的哪个矩阵（A/B/C）效果最明显？为什么？
3. 如何统计"无效预取"（预取块在被使用前被驱逐）？需要在数据结构里增加什么标志位？
