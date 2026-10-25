/*
 * decision_cmd.c — 决策层兜底：认出工具就用"审查过的只读默认参数"执行
 *
 * 背景：规则层未命中时，原来的唯一出路是模型的自由生成（实测单次 20s 量级，
 * 且对绝大多数输入返回"无法理解"）。而决策层（候选收缩 + 受约束 Choice）能在毫秒级
 * 认出"这句话在说哪个工具"——只是它只给能力 id，不给参数。
 *
 * 做法：维护一张**极小、逐条审查过**的只读默认参数表，命中即拼成命令，走原有的
 * 确认门/权限/快照/审计链路执行。表外的一律不猜（返回 0，让上层继续或如实拒绝）。
 *
 * 安全边界（ADR-022）：
 *   1. 只对 capability_perm_map.h 里 **L1** 且 trusted 的普通文件生效；
 *      注册表未登记 = L4 = 不放行（`ss` 就属于这类，不偷偷提权）。
 *   2. 参数来自本文件里的**字面量**，不接受任何来自输入/模型的参数拼接。
 *   3. 不在这张表里的工具，即使决策层认出来了也不执行。
 */

#include "decision.h"
#include "rule_engine.h"
#include "path_safety.h"
#include "nlc_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 只读默认参数表：name → 参数（"" 表示不带参数）。全部是 L1 只读工具。
 *
 * ADR-024 加了两种参数形态：
 *   LITERAL —— 参数是审查过的字面量（老路子，注入面为零）；
 *   DOMAIN  —— 参数是一个**按形状校验过的域名**（"结构化参数抽取"）。
 * 域名不是"猜"出来的：由 nl_extract_domain 抠形状、nl_domain_check 判合规，
 * 抽不到或不合规就跳过这个候选，绝不拼半条命令。
 * 为什么允许 DOMAIN：域名是外界名字，不是本机路径，不涉及越权；而 `dig` 的其余
 * 形态（-f 读文件、@服务器）都在 exec 的只读形态白名单里被拒。 */
typedef enum { NL_DEFAULT_LITERAL = 0, NL_DEFAULT_DOMAIN } NlDefaultKind;

typedef struct {
    const char *name;
    const char *args;        /* LITERAL：完整参数；DOMAIN：含 %s 的格式串 */
    NlDefaultKind kind;
    NlIntentType intent;
    int max_binary_perm;     /* 允许"二进制本身到几级"：见下方 readonly form 说明 */
} NlReadOnlyDefault;

/* 关于 max_binary_perm（ADR-024 续）：
 * 权限表按**二进制名**分级，`systemctl` 因此是 L2（它能 stop/restart）。
 * 但 `systemctl list-units --type=service` 是纯只读查询。这里表达的是：
 * "这个**形态**是审查过的只读形态，即使二进制是 L2，也可以按 L1 无人值守执行"。
 * 前提是 exec 层对同一个二进制做了形态白名单（只有登记形态能执行），
 * 否则 L1 就成了"整个 systemctl 免确认"的借口。 */

static const NlReadOnlyDefault READ_DEFAULTS[] = {
    { "pwd",       "",           NL_DEFAULT_LITERAL, NL_INTENT_OTHER,        1 },
    { "ls",        "-la",        NL_DEFAULT_LITERAL, NL_INTENT_LIST_DIR,     1 },
    { "ps",        "aux",        NL_DEFAULT_LITERAL, NL_INTENT_SHOW_PROCESS, 1 },
    { "df",        "-h",         NL_DEFAULT_LITERAL, NL_INTENT_OTHER,        1 },
    { "free",      "-m",         NL_DEFAULT_LITERAL, NL_INTENT_OTHER,        1 },
    { "uptime",    "",           NL_DEFAULT_LITERAL, NL_INTENT_OTHER,        1 },
    { "whoami",    "",           NL_DEFAULT_LITERAL, NL_INTENT_OTHER,        1 },
    { "uname",     "-a",         NL_DEFAULT_LITERAL, NL_INTENT_OTHER,        1 },
    { "date",      "",           NL_DEFAULT_LITERAL, NL_INTENT_OTHER,        1 },
    { "lsof",      "-i -P -n",   NL_DEFAULT_LITERAL, NL_INTENT_OTHER,        1 },  /* 端口/连接 */
    { "ip",        "-br addr",   NL_DEFAULT_LITERAL, NL_INTENT_SHOW_NETWORK, 1 },  /* 网卡/地址 */
    { "dig",       "+short %s",  NL_DEFAULT_DOMAIN,  NL_INTENT_SHOW_NETWORK, 1 },  /* 域名解析 */
    /* 服务列表：systemctl 是 L2，但这一个形态是只读的（exec 层只放行这一条） */
    { "systemctl", "list-units --type=service --no-pager",
                                 NL_DEFAULT_LITERAL, NL_INTENT_SHOW_PROCESS, 2 },
    { NULL, NULL, NL_DEFAULT_LITERAL, NL_INTENT_OTHER, 0 }
};

/* 命中则返回表项，未命中返回 NULL */
static const NlReadOnlyDefault *default_for(const char *name)
{
    for (int i = 0; READ_DEFAULTS[i].name; i++)
        if (strcmp(READ_DEFAULTS[i].name, name) == 0) return &READ_DEFAULTS[i];
    return NULL;
}

/* 候选分数下限：词典命中一次就是 8，低于它等于没认出任何东西 */
#define NL_DECISION_MIN_SCORE 8

int nl_parse_by_decision(const char *input, const NlCapRegistry *reg, NlParseResult *r)
{
    if (!input || !reg || !r) return -1;

    NlCandidates c;
    if (nl_decide_candidates(input, reg, &c) != 0) return -1;
    if (c.count == 0) return 0;

    /* 句子里带了路径 → 表内的字面量参数表达不了它。
     * 「看看 /var/log 里有什么」如果兜底成裸 `ls -la`，列的是当前目录 ——
     * 那不是"不完整"，那是**回答了另一个问题**。这类只能如实拒绝（等路径参数那一层做）。 */
    char **paths = nl_extract_paths(input);
    int has_path = (paths && paths[0]) ? 1 : 0;
    nl_free_paths(paths);

    /* 句子在要求**改状态**时，只读兜底就是在回答另一个问题：
     * 「把服务停掉」曾兜底成 `systemctl list-units`（用户要停，我们列了一遍）。
     * 兜底只做只读查询，所以这类句子一律不猜。 */
    {
        static const char *const MUTATE[] = {
            "启动", "停止", "停掉", "关掉", "关闭", "重启", "重载", "启用", "禁用",
            "安装", "卸载", "删除", "删掉", "创建", "新建", "修改", "改成", "设置成",
            "杀掉", "结束进程", NULL
        };
        for (int i = 0; MUTATE[i]; i++) {
            if (strstr(input, MUTATE[i])) return 0;   /* 不兜底，交给上层（规则/确认/模型） */
        }
    }

    for (int k = 0; k < c.count; k++) {
        if (c.items[k].score < NL_DECISION_MIN_SCORE) break;   /* 已按分数降序 */

        const NlCapability *cap = nl_cap_find(reg, c.items[k].id);
        if (!cap || !cap->path || !cap->trusted) continue;
        if (cap->kind != NL_CAP_KIND_FILE) continue;
        const char *local = strrchr(cap->id, ':');
        local = local ? local + 1 : cap->id;
        const NlReadOnlyDefault *d = default_for(local);
        if (!d) continue;                  /* 表外工具：不猜 */

        /* 只兜底"审查过的只读形态"：二进制本身可以比 L1 高（systemctl=L2），
         * 但只到表里写明的上限；L3/L4 一律不碰。 */
        if (cap->perm_level > d->max_binary_perm) continue;
        if (has_path && d->kind == NL_DEFAULT_LITERAL) continue;  /* 带路径：字面量参数答非所问 */

        char cmd[1024];
        int n;
        if (d->kind == NL_DEFAULT_DOMAIN) {
            /* 结构化参数抽取：句子里的域名按形状抠出来，校验不过就不执行这条 */
            char *dom = nl_extract_domain(input);
            if (!dom) continue;
            char fmt[256];
            snprintf(fmt, sizeof(fmt), "%s %s", cap->path, d->args);
            n = snprintf(cmd, sizeof(cmd), fmt, dom);
            free(dom);
        } else {
            n = d->args[0] ? snprintf(cmd, sizeof(cmd), "%s %s", cap->path, d->args)
                           : snprintf(cmd, sizeof(cmd), "%s", cap->path);
        }
        if (n < 0 || (size_t)n >= sizeof(cmd)) continue;

        memset(r, 0, sizeof(*r));
        r->command = strdup(cmd);
        if (!r->command) return -1;
        r->intent = d->intent;
        r->perm_level = NL_PERM_L1;   /* 审查过的只读形态：即使二进制是 L2，也不需要确认 */
        r->source = NL_SOURCE_DECISION;
        r->confidence = 1.0f;
        r->needs_confirm = 0;
        snprintf(r->debug_info, sizeof(r->debug_info),
                 "[决策层] 认出 %s（score=%d）→ 只读默认参数，未加载模型",
                 cap->id, c.items[k].score);
        return 1;
    }
    return 0;
}
