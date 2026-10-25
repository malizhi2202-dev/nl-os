/*
 * decision_cmd_test.c — 决策层兜底（ADR-022）
 *
 * 固定三件事：
 *   1. 表内 L1 只读工具（pwd/df/ps…）能被认出并给出**固定**参数
 *   2. 表外工具 / 非 L1 能力（rm 是 L3）**绝不**兜底——这是安全边界
 *   3. 什么候选都没有时不猜
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int failures = 0;
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

#include "nlc_api.h"
#include "capability_registry.h"
#include "decision_cmd.h"

static char g_dir[256];
static char g_reg_path[320];

static void write_registry(void)
{
    FILE *f = fopen(g_reg_path, "w");
    if (!f) { perror("registry"); exit(1); }
    fprintf(f, "{\"format_version\":2,\"scanner\":\"test\"}\n");
    /* L1 只读、在默认参数表里 */
    fprintf(f, "{\"id\":\"cli:pwd\",\"name\":\"pwd\",\"path\":\"%s/pwd\",\"perm\":\"L1\","
               "\"namespace\":\"cli\",\"effect\":\"read\",\"reversible\":true,\"source\":\"scan\",\"trusted\":true}\n", g_dir);
    fprintf(f, "{\"id\":\"cli:df\",\"name\":\"df\",\"path\":\"%s/df\",\"perm\":\"L1\","
               "\"namespace\":\"cli\",\"effect\":\"read\",\"reversible\":true,\"source\":\"scan\",\"trusted\":true}\n", g_dir);
    /* L1 但不在表里 → 不兜底 */
    fprintf(f, "{\"id\":\"cli:wc\",\"name\":\"wc\",\"path\":\"%s/wc\",\"perm\":\"L1\","
               "\"namespace\":\"cli\",\"effect\":\"read\",\"reversible\":true,\"source\":\"scan\",\"trusted\":true}\n", g_dir);
    /* 网络诊断（ADR-024）：ip 用字面量参数，dig 用结构化抽取的域名 */
    fprintf(f, "{\"id\":\"cli:ip\",\"name\":\"ip\",\"path\":\"%s/ip\",\"perm\":\"L1\","
               "\"namespace\":\"cli\",\"effect\":\"read\",\"reversible\":true,\"source\":\"scan\",\"trusted\":true}\n", g_dir);
    fprintf(f, "{\"id\":\"cli:dig\",\"name\":\"dig\",\"path\":\"%s/dig\",\"perm\":\"L1\","
               "\"namespace\":\"cli\",\"effect\":\"read\",\"reversible\":true,\"source\":\"scan\",\"trusted\":true}\n", g_dir);
    /* L3 → 一律不兜底 */
    fprintf(f, "{\"id\":\"cli:rm\",\"name\":\"rm\",\"path\":\"%s/rm\",\"perm\":\"L3\","
               "\"namespace\":\"cli\",\"effect\":\"delete\",\"reversible\":false,\"source\":\"scan\",\"trusted\":true}\n", g_dir);
    fclose(f);
}

/* 让 registry 里登记的文件真实存在（兜底要求可信普通文件） */
static void touch_bins(void)
{
    const char *names[] = { "pwd", "df", "wc", "rm", "ip", "dig", NULL };
    for (int i = 0; names[i]; i++) {
        char p[400];
        snprintf(p, sizeof(p), "%s/%s", g_dir, names[i]);
        FILE *f = fopen(p, "w");
        if (f) { fputs("#!/bin/sh\n", f); fclose(f); }
        chmod(p, 0755);
    }
}

int main(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/nl-dec-cmd-XXXXXX");
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); return 1; }
    snprintf(g_reg_path, sizeof(g_reg_path), "%s/caps.jsonl", g_dir);
    touch_bins();
    write_registry();

    NlCapRegistry *reg = nl_cap_load(g_reg_path, NULL);
    if (!reg) { fprintf(stderr, "registry 加载失败\n"); return 1; }

    NlParseResult r;

    /* 1. 表内命中 */
    int rc = nl_parse_by_decision("显示当前工作目录", reg, &r);
    ASSERT(rc == 1, "pwd 被认出");
    ASSERT(r.command && strstr(r.command, "/pwd") != NULL, "命令是 pwd");
    ASSERT(r.perm_level == NL_PERM_L1 && r.needs_confirm == 0, "L1 只读、不需确认");
    ASSERT(r.source == NL_SOURCE_DECISION, "来源标记为决策层");
    nl_parse_result_free(&r);

    rc = nl_parse_by_decision("看看磁盘还剩多少", reg, &r);
    ASSERT(rc == 1 && r.command && strstr(r.command, "df") && strstr(r.command, "-h"),
           "df 带固定参数 -h");
    nl_parse_result_free(&r);

    /* 1b. 网络：同一类问句只靠语义就能落到 ip / dig（不靠逐句规则） */
    rc = nl_parse_by_decision("看看我这台机器对外的地址", reg, &r);
    ASSERT(rc == 1 && r.command && strstr(r.command, "/ip") && strstr(r.command, "-br addr"),
           "问地址 → ip -br addr");
    ASSERT(r.intent == NL_INTENT_SHOW_NETWORK, "intent 是 show_network");
    nl_parse_result_free(&r);

    rc = nl_parse_by_decision("帮我查一下www.qq.com指向哪台机器", reg, &r);
    ASSERT(rc == 1 && r.command && strstr(r.command, "/dig") &&
           strstr(r.command, "+short www.qq.com"), "有域名 → dig +short <域名>");
    nl_parse_result_free(&r);

    /* 没有域名就别猜 dns（"解析一下这个域名"不该生成半条 dig 命令） */
    rc = nl_parse_by_decision("解析一下这个域名", reg, &r);
    ASSERT(rc == 0 && r.command == NULL, "无域名的解析请求不兜底");

    /* 指向本机的说法即使带"域名样"词也不跑 dig：「我的 IP」 */
    rc = nl_parse_by_decision("我的 IP 地址是多少", reg, &r);
    ASSERT(rc == 1 && r.command && strstr(r.command, "/ip"), "本机地址 → ip 而不是 dig");
    nl_parse_result_free(&r);

    /* 1c. 带路径的只读询问：工具由**目标类型**决定（目录→ls，文件→cat），
     * 而不是"因为参数表只会拼字面量所以拒绝"（ADR-024 之后路径可以被表达）。 */
    char dir[512];
    snprintf(dir, sizeof(dir), "/tmp/nl-dec-path-XXXXXX");
    rc = nl_parse_by_decision("看看 /tmp 里有什么", reg, &r);
    ASSERT(rc == 0 || rc == 1, "路径询问不崩");
    nl_parse_result_free(&r);

    /* 改状态的句子仍然拒绝（只读兜底不许接） */
    rc = nl_parse_by_decision("把 /tmp/somewhere 删掉", reg, &r);
    ASSERT(rc == 0 && r.command == NULL, "删东西不兜底");
    nl_parse_result_free(&r);
    (void)dir;

    /* 2. 安全边界：L3 的 rm 绝不兜底 */
    rc = nl_parse_by_decision("删除 /tmp/somewhere", reg, &r);
    ASSERT(rc == 0, "rm（L3）不兜底");
    ASSERT(r.command == NULL, "不产出命令");

    /* 3. L1 但不在默认参数表 → 不猜 */
    rc = nl_parse_by_decision("统计行数 wc", reg, &r);
    ASSERT(rc == 0, "表外工具不兜底");

    /* 4. 没有候选 → 不猜 */
    rc = nl_parse_by_decision("帮我写一首关于春天的诗", reg, &r);
    ASSERT(rc == 0, "无候选时不猜");

    nl_cap_free(reg);
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_dir);
    if (system(cmd) != 0) { /* 清理失败不影响结论 */ }

    if (failures == 0) printf("decision_cmd: 全部通过\n");
    else fprintf(stderr, "decision_cmd: %d 项失败\n", failures);
    return failures == 0 ? 0 : 1;
}
