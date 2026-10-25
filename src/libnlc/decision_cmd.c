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
#include "decision_model.h"
#include "rule_engine.h"
#include "path_safety.h"
#include "nlc_api.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* 只读默认参数表：name → 参数（"" 表示不带参数）。全部是 L1 只读工具。
 *
 * ADR-024 加了两种参数形态：
 *   LITERAL —— 参数是审查过的字面量（老路子，注入面为零）；
 *   DOMAIN  —— 参数是一个**按形状校验过的域名**（"结构化参数抽取"）。
 * 域名不是"猜"出来的：由 nl_extract_domain 抠形状、nl_domain_check 判合规，
 * 抽不到或不合规就跳过这个候选，绝不拼半条命令。
 * 为什么允许 DOMAIN：域名是外界名字，不是本机路径，不涉及越权；而 `dig` 的其余
 * 形态（-f 读文件、@服务器）都在 exec 的只读形态白名单里被拒。 */
typedef enum {
    NL_DEFAULT_LITERAL = 0,     /* 固定字面量参数 */
    NL_DEFAULT_DOMAIN,          /* 一个按形状校验的域名 */
    NL_DEFAULT_SERVICE,         /* 一个按形状校验的服务名 */
    NL_DEFAULT_PATH             /* 一个路径参数（只读，按 stat 结果选工具） */
} NlDefaultKind;

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
    /* 服务：systemctl 是 L2，但这两个形态是只读的（exec 层只放行这两条）。
     * 顺序有意义：先试 `status <服务名>`（句子里真有服务名时），
     * 没有服务名再退回"列出全部"。 */
    { "systemctl", "status %s",   NL_DEFAULT_SERVICE, NL_INTENT_SHOW_PROCESS, 2 },

    { "systemctl", "list-units --type=service --no-pager",
                                 NL_DEFAULT_LITERAL, NL_INTENT_SHOW_PROCESS, 2 },
    { NULL, NULL, NL_DEFAULT_LITERAL, NL_INTENT_OTHER, 0 }
};

/* 从输入里取第一个"像路径"的词（绝对路径或 ~ 开头），相对路径按 $PWD 展开。
 * 只做读取，所以不走写白名单；但必须是真实存在的路径（stat 成功）。 */
static int pick_existing_path(const char *input, char *out, size_t cap, int want_dir)
{
    const char *p = input;
    while (*p) {
        if (!(p[0] == '/' || p[0] == '~')) { p++; continue; }
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t len = (size_t)(p - start);
        if (len == 0 || len >= PATH_MAX) continue;

        char raw[PATH_MAX];
        snprintf(raw, sizeof(raw), "%.*s", (int)len, start);
        /* 去掉句尾标点（中文标点按字节收尾，这里只处理 ASCII 逗号/句号） */
        while (len > 1 && (raw[len-1] == ',' || raw[len-1] == '.'))
            raw[--len] = '\0';

        char resolved[PATH_MAX * 2];
        if (raw[0] == '~') {
            const char *home = getenv("HOME");
            if (!home) continue;
            snprintf(resolved, sizeof(resolved), "%s%s", home, raw + 1);
        } else if (raw[0] == '/') {
            snprintf(resolved, sizeof(resolved), "%s", raw);
        } else {
            char cwd[PATH_MAX];
            if (!getcwd(cwd, sizeof(cwd))) continue;
            snprintf(resolved, sizeof(resolved), "%s/%.500s", cwd, raw);
        }

        struct stat st;
        if (stat(resolved, &st) != 0) continue;
        if (want_dir && !S_ISDIR(st.st_mode)) continue;
        if (!want_dir && !S_ISREG(st.st_mode)) continue;
        snprintf(out, cap, "%s", resolved);
        return 1;
    }
    return 0;
}

/* 句子里有没有"约束条件"（大小/时间/数量/端口…）：字面量形态表达不了它们，
 * 硬回答就是答非所问（「把大于 100M 的文件列出来」曾兜底成裸 `ls -la`）。 */
static int has_constraint_of(const char *input) { return nl_input_has_constraint(input); }

/* 按表项生成命令。成功返回 1；形态不适用（抽不到合规参数等）返回 0。
 * 注意：不适用 ≠ 猜不出来 —— 抽不到参数就是"这条形态用不了"，由调用方试下一条。 */
static int build_from_default(const NlReadOnlyDefault *d, const char *path,
                              const char *input, char *cmd, size_t cap)
{
    int n = -1;
    if (d->kind == NL_DEFAULT_DOMAIN) {
        char *dom = nl_extract_domain(input);
        if (!dom) return 0;
        char fmt[256];
        snprintf(fmt, sizeof(fmt), "%s %s", path, d->args);
        n = snprintf(cmd, cap, fmt, dom);
        free(dom);
    } else if (d->kind == NL_DEFAULT_SERVICE) {
        char *svc = nl_extract_service_name(input);
        if (!svc) return 0;
        char fmt[256];
        snprintf(fmt, sizeof(fmt), "%s %s", path, d->args);
        n = snprintf(cmd, cap, fmt, svc);
        free(svc);
    } else if (d->kind == NL_DEFAULT_PATH) {
        /* 路径类：目录给 ls，普通文件给 cat —— 工具适不适用由文件类型决定，
         * 不靠用户措辞猜（"看看 /etc/hosts" 和 "看看 /var/log" 用同一个句式）。 */
        int want_dir = (strcmp(d->name, "ls") == 0);
        char target[PATH_MAX * 2];
        if (!pick_existing_path(input, target, sizeof(target), want_dir)) return 0;
        char fmt[256];
        snprintf(fmt, sizeof(fmt), "%s %s", path, d->args);
        n = snprintf(cmd, cap, fmt, target);
    } else {
        n = d->args[0] ? snprintf(cmd, cap, "%s %s", path, d->args)
                       : snprintf(cmd, cap, "%s", path);
    }
    if (n < 0 || (size_t)n >= cap) return 0;
    return 1;
}

/* ============================================================
 * 带路径的只读询问（ADR-024 续）
 *
 * 「看看 /var/log 里有什么」「看看 /etc/hosts」这类句子以前一律拒绝，因为
 * 兜底的参数表只会拼"字面量"，表达不了句子里的路径。
 *
 * 这里的关键判断是**结构性的**：工具由目标类型决定 —— 目录用 ls、普通文件用 cat，
 * 不靠用户措辞去猜（同一个句式「看看 X」两种答案，区别只在 X 是文件还是目录）。
 * 路径必须真实存在（stat 成功），不存在就如实拒绝，绝不猜一个路径出来。
 * 只读，所以不需要写白名单；MUTATE 检查在调用点之前，改状态的说法到不了这里。
 * ============================================================ */
static int read_hint_present(const char *s)
{
    static const char *const W[] = {
        "看看", "查看", "瞧瞧", "看一下", "打开", "读取", "显示", "列出",
        "内容", "有什么", "清单", "里面", "底下", "列一下", NULL
    };
    for (int i = 0; W[i]; i++)
        if (strstr(s, W[i])) return 1;
    return 0;
}

/* 成功返回 1（r 填好）；没有可用路径 / 不是只读询问 → 0 */
static int try_path_query(const char *input, NlParseResult *r)
{
    if (!read_hint_present(input)) return 0;

    char target[PATH_MAX * 2];
    int is_dir = pick_existing_path(input, target, sizeof(target), 1);
    char target_file[PATH_MAX * 2];
    int is_file = !is_dir && pick_existing_path(input, target_file, sizeof(target_file), 0);
    if (!is_dir && !is_file) return 0;

    char cmd[PATH_MAX * 2 + 8];
    snprintf(cmd, sizeof(cmd), is_dir ? "ls -la %s" : "cat %s", is_dir ? target : target_file);

    memset(r, 0, sizeof(*r));
    r->command = strdup(cmd);
    if (!r->command) return 0;
    r->intent = is_dir ? NL_INTENT_LIST_DIR : NL_INTENT_VIEW_FILE;
    r->perm_level = NL_PERM_L1;
    r->source = NL_SOURCE_DECISION;
    r->confidence = 1.0f;
    r->needs_confirm = 0;
    snprintf(r->debug_info, sizeof(r->debug_info),
             "[决策层] 路径查询 → %s（按目标类型选工具，未加载模型）", is_dir ? "ls" : "cat");
    return 1;
}

/* 候选分数下限：词典命中一次就是 8，低于它等于没认出任何东西 */
#define NL_DECISION_MIN_SCORE 8

/* 只有**完全打平**（分数差 = 0）才送去问模型：真实语料里"差 1-4 分"通常仍是
 * 一个明确赢家，为此付 2s 解码不值得（实测「显示当前工作目录」就被这条拖成了
 * 平局 → 白等 4s）。平局才叫真的没有依据。 */
#define NL_DECISION_AMBIGUOUS_GAP 0

int nl_parse_by_decision(const char *input, const NlCapRegistry *reg, NlParseResult *r)
{
    if (!input || !reg || !r) return -1;

    /* 带路径的只读询问先判：工具由目标类型决定，不依赖词典认出了什么。
     * 「显示 /etc/hosts」没有任何提示词命中，但它显然是在问这个文件。 */
    char **paths0 = nl_extract_paths(input);
    int has_path0 = (paths0 && paths0[0]) ? 1 : 0;
    nl_free_paths(paths0);
    if (has_path0 && try_path_query(input, r) == 1) return 1;

    NlCandidates c;
    if (nl_decide_candidates(input, reg, &c) != 0) return -1;
    if (c.count == 0) return 0;


    /* 句子里带了路径 → 表内的字面量参数表达不了它。
     * 「看看 /var/log 里有什么」如果兜底成裸 `ls -la`，列的是当前目录 ——
     * 那不是"不完整"，那是**回答了另一个问题**。这类只能如实拒绝（等路径参数那一层做）。 */
    char **paths = nl_extract_paths(input);
    int has_path = (paths && paths[0]) ? 1 : 0;
    nl_free_paths(paths);

    /* 句子里带**约束条件**（大小/时间/数量/端口…）时，字面量形态也在答非所问：
     * 「把大于 100M 的文件列出来」曾兜底成裸 `ls -la`（约束被整条忽略）。
     * 这类只有"吃参数的形态"（路径/域名/服务名）能回答，字面量形态必须让路。 */
    int has_constraint = has_constraint_of(input);

    /* 句子在要求**改状态**时，只读兜底就是在回答另一个问题：
     * 「把服务停掉」曾兜底成 `systemctl list-units`（用户要停，我们列了一遍）。
     * 兜底只做只读查询，所以这类句子一律不猜。 */
    {
        static const char *const MUTATE[] = {
            "启动", "停止", "停掉", "关掉", "关闭", "重启", "重载", "启用", "禁用",
            "安装", "卸载", "删", "创建", "新建", "修改", "改成", "设置成",
            "杀掉", "结束进程", "移动", "挪到", "搬到", "复制", "拷贝", "重命名", "改名",
            "覆盖", "清理", "压缩", "解压", "写入", "追加", NULL
        };
        for (int i = 0; MUTATE[i]; i++) {
            if (strstr(input, MUTATE[i])) return 0;   /* 不兜底，交给上层（规则/确认/模型） */
        }
    }

    /* 带路径的只读询问：工具由目标类型决定，绕开候选打分 */
    if (has_path && try_path_query(input, r) == 1) return 1;

    /* 第一遍：收集前两名"能拼出命令"的候选。分数紧咬就**不硬选**，
     * 交给上层用模型做受约束选择（否则 10ms 的 top-1 会静默选错）。 */
    const NlCapability *caps[2] = { NULL, NULL };
    const NlReadOnlyDefault *forms[2] = { NULL, NULL };
    char cmds[2][1024];
    int scores[2] = { 0, 0 };
    int found = 0;

    for (int k = 0; k < c.count && found < 2; k++) {
        if (c.items[k].score < NL_DECISION_MIN_SCORE) break;   /* 已按分数降序 */

        const NlCapability *cap = nl_cap_find(reg, c.items[k].id);
        if (!cap || !cap->path || !cap->trusted) continue;
        if (cap->kind != NL_CAP_KIND_FILE) continue;
        const char *local = strrchr(cap->id, ':');
        local = local ? local + 1 : cap->id;

        /* 同一工具可以登记多张只读形态，按表内顺序逐个试：
         * systemctl 先试 `status <服务名>`（句子真有服务名时），再退回"列出全部"。 */
        for (int di = 0; READ_DEFAULTS[di].name; di++) {
            if (strcmp(READ_DEFAULTS[di].name, local) != 0) continue;
            /* 只兜底"审查过的只读形态"：二进制本身可以比 L1 高（systemctl=L2），
             * 但只到表里写明的上限；L3/L4 一律不碰。 */
            if (cap->perm_level > READ_DEFAULTS[di].max_binary_perm) continue;
            /* 句子里带路径时，字面量形态答非所问（除非这张形态本来就吃参数） */
            if (has_path && (READ_DEFAULTS[di].kind == NL_DEFAULT_LITERAL ||
                             READ_DEFAULTS[di].kind == NL_DEFAULT_DOMAIN ||
                             READ_DEFAULTS[di].kind == NL_DEFAULT_SERVICE))
                continue;
            /* 带约束时只有吃参数的形态能表达它 */
            if (has_constraint && READ_DEFAULTS[di].kind == NL_DEFAULT_LITERAL) continue;
            if (!build_from_default(&READ_DEFAULTS[di], cap->path, input,
                                    cmds[found], sizeof(cmds[found])))
                continue;
            caps[found] = cap;
            forms[found] = &READ_DEFAULTS[di];
            scores[found] = c.items[k].score;
            found++;
            break;
        }
    }
    if (found == 0) return 0;                      /* 表外工具 / 形态都不适用：不猜 */

    if (found == 2 && scores[0] - scores[1] <= NL_DECISION_AMBIGUOUS_GAP && scores[0] < 50) {
        /* 模糊：本层不硬选，返回"不确定"，让模型在候选内选（受约束解码） */
        return 0;
    }

    memset(r, 0, sizeof(*r));
    r->command = strdup(cmds[0]);
    if (!r->command) return -1;
    r->intent = forms[0]->intent;
    r->perm_level = NL_PERM_L1;   /* 审查过的只读形态：即使二进制是 L2，也不需要确认 */
    r->source = NL_SOURCE_DECISION;
    r->confidence = 1.0f;
    r->needs_confirm = 0;
    snprintf(r->debug_info, sizeof(r->debug_info),
             "[决策层] 认出 %s（score=%d）→ 只读默认参数，未加载模型",
             caps[0]->id, scores[0]);
    return 1;
}

/* ============================================================
 * 受约束选择：模型只能在"可执行的候选"里选（ADR-024 续）
 *
 * 为什么需要这一层：词典是固定词表，遇到"说法模糊但候选不止一个"时只能按权重
 * 取 top-1，选错的代价是执行了另一件事。这里的做法是把 GBNF 约束加在模型上 ——
 * 它只能在**本机真实存在、且能用审查过的只读形态生成命令**的候选里选一个，
 * 选不出就如实放弃（继续走后续层）。
 *
 * 注意：只挑"能拼出命令"的候选。选出来再发现没有参数可用，等于白花一次解码。
 * ============================================================ */
int nl_parse_by_choice(struct NlModel *m, const char *input,
                       const NlCapRegistry *reg, NlParseResult *r)
{
    if (!m || !input || !reg || !r) return -1;

    NlCandidates all;
    if (nl_decide_candidates(input, reg, &all) != 0) return -1;
    if (all.count < 2) return 0;      /* 只有一个候选时词典已经给了答案，不必解码 */

    /* 只保留"能拼出命令"的候选；顺手记住每条候选对应的命令 */
    NlCandidates usable;
    memset(&usable, 0, sizeof(usable));
    usable.total_scanned = all.total_scanned;
    char cmds[NL_DECIDE_K][1024];
    NlIntentType intents[NL_DECIDE_K];
    int n_usable = 0;

    char **paths = nl_extract_paths(input);
    int has_path = (paths && paths[0]) ? 1 : 0;
    nl_free_paths(paths);

    for (int k = 0; k < all.count && n_usable < NL_DECIDE_K; k++) {
        const NlCapability *cap = nl_cap_find(reg, all.items[k].id);
        if (!cap || !cap->path || !cap->trusted) continue;
        if (cap->kind != NL_CAP_KIND_FILE) continue;

        const char *local = strrchr(cap->id, ':');
        local = local ? local + 1 : cap->id;

        for (int di = 0; READ_DEFAULTS[di].name; di++) {
            if (strcmp(READ_DEFAULTS[di].name, local) != 0) continue;
            if (cap->perm_level > READ_DEFAULTS[di].max_binary_perm) continue;
            if (has_path && (READ_DEFAULTS[di].kind == NL_DEFAULT_LITERAL ||
                             READ_DEFAULTS[di].kind == NL_DEFAULT_DOMAIN ||
                             READ_DEFAULTS[di].kind == NL_DEFAULT_SERVICE))
                continue;
            /* 带约束时只有吃参数的形态能表达它 */
            if (has_constraint_of(input) && READ_DEFAULTS[di].kind == NL_DEFAULT_LITERAL) continue;
            if (!build_from_default(&READ_DEFAULTS[di], cap->path, input,
                                    cmds[n_usable], sizeof(cmds[n_usable])))
                continue;
            intents[n_usable] = READ_DEFAULTS[di].intent;
            snprintf(usable.items[n_usable].id, NL_CAP_MAX_ID_LEN, "%s", cap->id);
            usable.items[n_usable].score = all.items[k].score;
            n_usable++;
            break;
        }
    }
    usable.count = n_usable;
    if (n_usable < 2) return 0;       /* 只有一条可执行候选：没有可"选择"的东西 */

    char chosen[NL_CAP_MAX_ID_LEN];
    NlChoiceStatus st = nl_choose_with_model(m, input, &usable, chosen, sizeof(chosen));
    if (st != NL_CHOICE_OK) return 0;  /* 拒绝/低置信度/解码失败：交给后续层 */

    for (int k = 0; k < n_usable; k++) {
        if (strcmp(usable.items[k].id, chosen) != 0) continue;

        memset(r, 0, sizeof(*r));
        r->command = strdup(cmds[k]);
        if (!r->command) return 0;
        r->intent = intents[k];
        r->perm_level = NL_PERM_L1;
        r->source = NL_SOURCE_MODEL;
        r->confidence = (float)nl_model_last_confidence(m);
        r->needs_confirm = 0;
        snprintf(r->debug_info, sizeof(r->debug_info),
                 "[受约束选择] 候选 %d 里模型选 %s（未越出候选集）", n_usable, chosen);
        return 1;
    }
    return 0;
}
