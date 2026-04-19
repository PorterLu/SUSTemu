# Memory Ordering Lab

## Overview

本 lab 通过在真实 OOO 双核模拟器上运行三个实验，**定量**观察 TSO（Total Store Order）内存模型带来的并发问题，并学习如何用 `fence` 指令修复它们。

模拟器实现了 **TSO 内存模型**：每个核有一个 store buffer，committed store 先进入 store buffer，延迟一个周期才写入共享 cache。这与真实 x86/ARMv8 处理器的行为一致。

---

## 背景知识

### Sequential Consistency (SC) vs TSO

| 属性 | SC | TSO |
|------|----|-----|
| Store 全局可见时机 | commit 时立即 | commit 后延迟（store buffer） |
| Load 可以越过 Store | 否 | 是（同一核） |
| Store-Store 重排 | 否 | 否 |
| `fence` 作用 | 无需 | 排空 store buffer |

在 TSO 下，每个核执行：
```
STORE x = 1          → 进入 store buffer（其他核看不见）
LOAD  r1 = y         → 从 cache 读，看不到其他核 store buffer 中的内容
```
这导致两个核同时看到对方的"旧值"，产生 **TSO violation**。

### `fence rw, rw` 指令

RISC-V 的 `fence rw, rw` 是一个 serializing 指令：
- 排空当前核的 store buffer（所有 pending store 写入 cache）
- 确保 fence 之前的 load/store 对其他核全局可见
- fence 之后的操作保证在此之后才执行

---

## Q1：Store-Load 重排（Litmus Test）

**目录**：`order_lab/Q1/litmus/`

**实验设计**：经典 Dekker litmus test

```
初始：x = 0, y = 0

Core 0:          Core 1:
  x = 1;           y = 1;
  r1 = y;          r2 = x;
```

在 SC 下：至少有一个 store 先于对方的 load 完成，所以 r1=0 && r2=0 **不可能**同时出现。

在 TSO 下：两个 store 都可能停在各自的 store buffer 里，两个 load 都从 cache 读到 0，导致 r1=0 && r2=0 **有可能**同时出现（violation）。

**运行**：
```bash
cd order_lab/Q1/litmus
make
make run          # 不加 fence，观察 violation
make run-fence    # 加 fence，验证修复
```

### Question 1

运行 `make run`（不加 fence），记录输出的 Violations 次数和百分比。

> 思考：为什么在顺序执行（in-order）模型下不会出现 violation？OOO + TSO 的哪个特性使 violation 成为可能？

### Question 2

运行 `make run-fence`（加 `fence rw, rw`）。解释为什么加 fence 后 r1=0 && r2=0 消失。

> 提示：fence 如何改变 store 的全局可见时机？画出加/不加 fence 时两个核的执行时序图。

---

## Q2：非原子计数器（Lost Updates）

**目录**：`order_lab/Q2/counter/`

**实验设计**：两个核各自对共享计数器执行 1000 次自增。

每次自增操作：
```c
tmp = *counter;   // LOAD
tmp = tmp + 1;
*counter = tmp;   // STORE（进入 store buffer）
```

这是一个**读-改-写**序列，不是原子操作。在 TSO 下：
- Core 0 的 STORE（`counter=k+1`）停在 store buffer 里
- Core 1 的 LOAD 读到 cache 中的旧值 `k`
- 两核都写入 `k+1`，一次更新丢失

**运行**：
```bash
cd order_lab/Q2/counter
make run
```

### Question 3

运行实验，记录实际 counter 值和期望值（2000）。计算丢失的更新次数。

> 思考：为什么丢失的更新接近 N_ITER（1000）次？store buffer 的存在如何导致 Core 1 总是读到 Core 0 的旧值？

---

## Q3：Peterson 互斥锁（TSO 下的锁失效）

**目录**：`order_lab/Q3/peterson/`

**实验设计**：Peterson 算法是一个经典的纯软件互斥锁，不需要原子指令：

```c
lock(i):
    want[i] = 1;          // "我想进入临界区"
    turn    = 1 - i;      // "你先"
    while (want[1-i] && turn == 1-i) {}   // 等待

unlock(i):
    want[i] = 0;
```

在 SC 下，这个算法可以**证明**是正确的（互斥性 + 无死锁）。

在 TSO 下，没有 fence 时算法**失效**：
- `want[i]=1` 停在 store buffer，其他核看到的仍是 0
- 两个核都认为对方不想进入，同时进入临界区
- 临界区内的 counter++ 不受保护，产生丢失更新

**代码骨架**（`peterson.c`）：

```c
static void lock(int i) {
    int j = 1 - i;
    want[i] = 1;
    /* TODO (Q4): 在此添加 fence rw,rw */
    *turn = j;
    /* TODO (Q4): 在此添加 fence rw,rw */
    while (want[j] && *turn == j) {}
}
```

**运行**：
```bash
cd order_lab/Q3/peterson
make
make run          # 不加 fence：锁失效，counter 错误
make run-fence    # 加 fence：锁正确，counter = 2*N_ITER
```

### Question 4

运行 `make run`（`USE_FENCE=0`）。观察实际 counter 值与期望值（1000）的差距。

> 解释：在 TSO 下，`lock()` 的哪一步失效了？画出两个核同时进入临界区的执行时序。

### Question 5

在 `peterson.c` 的 `lock()` 函数中，找到两处 `TODO` 注释，分别在 `want[i]=1` 之后和 `*turn=j` 之后添加 `fence rw, rw`。

运行 `make run-fence` 验证 counter = 1000（所有更新不丢失）。

> 解释：为什么需要**两个** fence？只加一个 fence 够吗？分别说明每个 fence 防止了什么重排序。

---

## 总结

| 实验 | 问题 | 根因 | 修复 |
|------|------|------|------|
| Q1 Litmus | r1=0 && r2=0 | store 延迟对其他核可见 | store 后加 fence |
| Q2 Counter | lost updates | LOAD 读到 store buffer 中的旧值 | 需要原子 RMW 或锁 |
| Q3 Peterson | 锁失效 | want[] 写不立即全局可见 | lock() 中加 fence |

**核心结论**：TSO 的 store buffer 使得多核程序在没有正确同步的情况下产生不确定行为。`fence` 指令通过强制排空 store buffer 来恢复必要的内存顺序保证。
