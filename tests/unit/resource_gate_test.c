/*
 * resource_gate_test.c — 加载模型前的内存闸门
 *
 * 真实事故：可用内存 657MB 时仍去加载 428MB 模型（RSS 800MB），整机被拖进内存回收，
 * 用户感受到的是"本来很快，现在全卡了"。闸门要保证：
 *   1. 内存不够就**不**加载，而不是"试试看会不会 OOM"；
 *   2. 判定是纯函数，边界能用数字锁死，不依赖当时的机器状态；
 *   3. 读数失败一律拒绝（fail-closed）—— 不知道有多少内存时不要赌。
 */

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

#include "resource_gate.h"
#include "model_loader.h"

static void test_gate_decision(void)
{
    /* 真实事故那台机器：可用 657MB，模型文件 428MB（需求 856MB），余量 512MB → 必须拒绝 */
    ASSERT(nl_resource_gate_allow(856, 657, 512) == 0, "657MB 可用时不得加载 856MB 的模型");

    /* 刚好够：avail - headroom == need */
    ASSERT(nl_resource_gate_allow(856, 1368, 512) == 1, "刚好等于 need+headroom 时应允许");

    /* 差 1MB 就不行 */
    ASSERT(nl_resource_gate_allow(856, 1367, 512) == 0, "差 1MB 时必须拒绝");

    /* 空闲的 8GB 机器：可用 5GB → 允许 */
    ASSERT(nl_resource_gate_allow(856, 5000, 512) == 1, "内存充裕时应允许");

    /* 读不到数字（-1）→ fail-closed */
    ASSERT(nl_resource_gate_allow(856, -1, 512) == 0, "可用内存读不到时必须拒绝");
    ASSERT(nl_resource_gate_allow(-1, 5000, 512) == 0, "模型大小读不到时必须拒绝");
    ASSERT(nl_resource_gate_allow(0, 5000, 512) == 0, "need=0 视为非法，必须拒绝");

    /* 余量为 0 是合法的（表示不留余量），但不能因此放行负数 */
    ASSERT(nl_resource_gate_allow(100, 100, 0) == 1, "headroom=0 时 avail==need 应允许");
    ASSERT(nl_resource_gate_allow(100, 5000, -5) == 0, "负余量非法，必须拒绝");

    /* 溢出陷阱：need+headroom 若用加法会变成负数而被误判为"够用" */
    ASSERT(nl_resource_gate_allow(4000000000L, 200, 4000000000L) == 0,
           "超大数值不得因加法溢出被误判为够用");
}

static void test_model_need(void)
{
    /* 真实模型文件（本机 0.5B q4_0 ≈ 409MiB）→ 需求按 2 倍估为约 818MB，
     * 与实测 RSS 677–815MB 对得上。没有模型的环境跳过这一段，不假装测过。 */
    const char *model = nl_model_default_path();
    if (model) {
        long need = nl_model_need_mb(model);
        ASSERT(need > 0, "真实模型文件的需求估算应大于 0");
        ASSERT(need >= 700 && need <= 1000,
               "0.5B q4_0 的需求估算应落在 700–1000MB（与实测 RSS 677–815MB 一致）");
    } else {
        printf("（跳过真实模型需求估算：本机没有 GGUF，用下面的 -1 用例代替）\n");
    }

    /* 文件不存在 → -1（而不是 0，0 会被误当成"不需要内存"） */
    ASSERT(nl_model_need_mb("/nonexistent/x.gguf") == -1, "不存在的文件应返回 -1");
    ASSERT(nl_model_need_mb(NULL) == -1, "NULL 应返回 -1");
    ASSERT(nl_model_need_mb("") == -1, "空串应返回 -1");
}

static void test_mem_available(void)
{
    long avail = nl_mem_available_mb();
    /* Linux 上有 MemAvailable；读不到说明 /proc 不可用，那也应当返回 -1 而不是瞎猜 */
    ASSERT(avail == -1 || avail > 0, "MemAvailable 应是正数或 -1");
}

int main(void)
{
    test_gate_decision();
    test_model_need();
    test_mem_available();

    if (failures) { fprintf(stderr, "%d 个断言失败\n", failures); return 1; }
    printf("resource_gate: 全部断言通过\n");
    return 0;
}
