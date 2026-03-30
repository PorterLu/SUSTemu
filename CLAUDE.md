# sustemu OOO Lab — Session Notes

## 项目结构
- `src/cpu/ooo.c` — OOO核心逻辑（IF/ID/RN/IS/EX/MEM/COMMIT）
- `src/cpu/pipeline.c` — 顺序5段流水线
- `include/cpu/exec_cfg.h` — 关键参数
- `include/cpu/bpred.h` / `src/cpu/bpred.c` — 分支预测器
- `ooo_lab/` — 三个benchmark: Q1/ilp, Q2/tomasulo, Q3/memlat

## 当前配置 (exec_cfg.h)
```c
#define ISSUE_WIDTH     2
#define NUM_INT_UNITS   2
#define NUM_MUL_UNITS   1
#define NUM_LSU_UNITS   2
#define LAT_L1_HIT      1
#define LAT_L2_HIT      8
#define LAT_DRAM       40
```
> 修改头文件后必须 `make clean && make`

## OOO 流水线结构

7级流水：IF → ID → RN → IS → EX → MEM → COMMIT

| 阶段 | 功能 | 关键结构 |
|------|------|----------|
| IF | 取指，调用 bpred_predict，更新 fetch_pc | latch_if_id |
| ID | ir_decode，将 raw 指令解码为 IR_Inst | latch_id_rn |
| RN | 寄存器重命名：RAT查表、freelist分配物理寄存器、分配ROB槽 | RAT[32], freelist, ROB[32] |
| IS | 从RS中选最旧的就绪指令（oldest-first）；LOAD候选在此检查memory ordering | RS[16], latch_is_ex[2] |
| EX | ir_execute；ALU结果写PRF并CDB广播唤醒；分支在此计算dnpc | PRF[64], latch_ex_mem[2] |
| MEM | LOAD: ir_mem_access + STLF扫描ROB；STORE: 仅保存store_data到ROB | ROB[32].store_data |
| COMMIT | 顺序提交ROB head（**1条/周期**）；STORE在此 vaddr_write；分支mispred在此检测并flush | RRAT[32] |

**关键尺寸**：ROB_SIZE=32，RS_SIZE=16，NUM_PHYS_REGS=64，ISSUE_WIDTH=2

**分支mispred**：COMMIT阶段检测 dnpc≠predicted → 调用 `ooo_flush_after()` 清空ROB/RS/latches，恢复RAT←RRAT，设fetch_pc=dnpc。

## 分支预测器结构（--bpred 开启）

**Tournament Predictor + BTB**

| 组件 | 规格 |
|------|------|
| BTB | 512项，index=PC[10:2]，tag=PC[high]；命中时提供taken目标地址 |
| Local predictor | LHT[1024]×10-bit per-PC历史 → LPHT[1024] 2-bit饱和计数器 |
| Global predictor (gshare) | GHR(12-bit) XOR PC[high] → GPHT[4096] 2-bit饱和计数器 |
| Meta selector | META[4096] 2-bit饱和计数器；≥2选global，<2选local |

**预测流程**（IF阶段）：`bpred_predict()` → `{taken, target, btb_hit}`
→ 若 taken && btb_hit：fetch_pc = target（重定向），bp_predicted_pc = target
→ 否则：bp_predicted_pc = pc+4

**更新**（COMMIT阶段）：`bpred_update()` 更新BTB/LHT/GHR/PHT/meta

> **⚠️ 不加 --bpred 时**：所有分支预测not-taken，每个循环回跳都mispred → ROB清空重填（~16周期），性能严重劣于in-order。**lab run-ooo必须加--bpred**。

## Memory Ordering（IS阶段）

LOAD候选在RS扫描时检查：若ROB中存在 older 且 `ready=0` 的 STORE（raw&0x7f==0x23），跳过该LOAD候选（不浪费issue slot，继续找其他指令）。IS阶段保证LOAD进MEM时所有older STORE均ready=1，STLF才安全。

## 已完成 ✓
- STLF（MEM阶段ROB扫描）
- IS阶段memory ordering保守检查
- deferred fault（投机fetch越过ret进入.rodata）
- IS阶段blocked-load修复（check移入RS扫描循环内部）
- 所有lab Makefile的run-ooo加--bpred

## 运行
```bash
make clean && make
cd ooo_lab/Q1/ilp    && make run-inorder && make run-ooo
cd ooo_lab/Q2/tomasulo && make run-ooo
cd ooo_lab/Q3/memlat   && make run-ooo
```
