/*
 * decision.c — 决策层的模型无关部分（T10/T11/T12）
 */
#include "decision.h"
#include "rule_engine.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 有合法域名时的偏置分：高于任何"提示词累加"（单个工具最多累到 30 左右），
 * 低于规则命中的 +50。它表达的是**判别性证据**：句子里出现一个合法域名，
 * 就是在问"这个名字指向哪"，比「IP」这种软词强得多（「帮我查 example.org 的 IP」
 * 曾因为 ip 的提示词累加到 20 与 dig 打平，最后选了 ip）。 */
#define NL_DECISION_DOMAIN_BIAS 45

/* 词典粗筛：中文动作词 → 能力名（只用于排候选，不做最终判定）
 *
 * weight 的作用：区分"泛指"和"特指"。`显示` 这种泛词给 6，`工作目录` 这种特指给 10，
 * 否则"显示当前工作目录"会在 pwd 和 ls 之间打平（曾导致 pwd 候选丢失）。 */
typedef struct {
    const char *keyword;
    const char *cap_name;
    int         weight;
} NlDecideHint;

static const NlDecideHint HINTS[] = {
    /* ── 目录/路径 ──
     * 词典的验收标准不是"我记得的说法"，而是 tests/paraphrase/nl-paraphrase-zh.json
     * 里那一簇说法全都能落到同一个能力。加词时先跑 scripts/verify_paraphrase.py。 */
    { "列出", "ls", 8 },        { "列举", "ls", 10 },      { "列一下", "ls", 10 },
    { "列目录", "ls", 10 },     { "查看目录", "ls", 8 },   { "当前目录", "ls", 8 },
    { "现在的目录", "ls", 8 },  { "文件列表", "ls", 10 },   { "有哪些文件", "ls", 10 },
    { "都有什么", "ls", 8 },    { "都有啥", "ls", 8 },     { "有些什么", "ls", 8 },
    { "看看有什么", "ls", 10 }, { "目录内容", "ls", 10 },   { "文件夹内容", "ls", 10 },
    { "目录里有什么", "ls", 10 },{ "文件夹里有什么", "ls", 10 },
    { "有什么东西", "ls", 10 }, { "文件", "ls", 6 },
    { "有啥", "ls", 10 },      { "文件清单", "ls", 10 },  { "清单", "ls", 8 },
    { "工作目录", "pwd", 10 },  { "当前路径", "pwd", 10 }, { "所在目录", "pwd", 10 },
    { "在哪个目录", "pwd", 10 },{ "工作路径", "pwd", 10 }, { "现在的位置", "pwd", 10 },
    { "我在哪", "pwd", 10 },    { "什么地方", "pwd", 10 },
    { "哪个文件夹", "pwd", 10 },
    /* 「在哪」「位置」「处在」这种 1-2 字的虚词碎片**不能**当提示词：
     * 「本机现在连在哪个网段」被其中的「在哪」判成 pwd，答案是当前目录。
     * 提示词必须是带语境的说法（我在哪 / 什么位置），碎片越短越容易撞车。 */
    { "什么位置", "pwd", 8 },   { "哪个位置", "pwd", 8 },
    { "路径", "pwd", 10 },       { "打印", "pwd", 10 },
    /* ── 文件内容 ── */
    { "查看", "cat", 6 },       { "内容", "cat", 8 },      { "读取", "cat", 8 },
    { "创建目录", "mkdir", 8 }, { "文件夹", "mkdir", 8 },  { "新建目录", "mkdir", 8 },
    { "创建文件", "touch", 8 }, { "空文件", "touch", 10 },
    { "删除", "rm", 8 },        { "删掉", "rm", 8 },
    { "移动", "mv", 8 },        { "重命名", "mv", 10 },
    { "复制", "cp", 8 },        { "拷贝", "cp", 8 },
    { "查找", "find", 8 },      { "搜索", "find", 8 },     { "找到", "find", 8 },
    /* ── 进程/资源 ── */
    { "进程", "ps", 10 },       { "在跑", "ps", 8 },       { "跑着", "ps", 8 },
    { "进程列表", "ps", 10 },   { "占CPU", "ps", 10 },     { "CPU", "ps", 8 },
    { "cpu", "ps", 8 },         { "谁在占", "ps", 10 },    { "异常的进程", "ps", 8 },
    { "内存", "free", 10 },     { "剩余内存", "free", 10 },{ "内存占用", "free", 10 },
    { "磁盘", "df", 8 },        { "空间", "df", 8 },       { "磁盘空间", "df", 10 },
    { "磁盘占用", "df", 10 },   { "硬盘", "df", 10 },      { "分区", "df", 10 },
    { "存储空间", "df", 10 },   { "还剩多少", "df", 6 },
    { "负载", "uptime", 10 },   { "开机多久", "uptime", 10 },
    { "运行时间", "uptime", 10 },{ "运行了多长时间", "uptime", 10 },
    { "运行了多久", "uptime", 10 },{ "开了多久", "uptime", 10 },
    { "稳定", "uptime", 8 },    { "负载高", "uptime", 10 },{ "跑了多久", "uptime", 10 },
    { "持续运行", "uptime", 10 },{ "负载情况", "uptime", 10 },
    /* ── 系统/身份 ── */
    { "系统信息", "uname", 10 },{ "内核", "uname", 10 },   { "系统版本", "uname", 10 },
    { "什么系统", "uname", 10 },{ "版本", "uname", 8 },    { "架构", "uname", 10 },
    { "系统详情", "uname", 10 },{ "详情", "uname", 8 },    { "操作系统", "uname", 10 },
    { "当前用户", "whoami", 10 }, { "我是谁", "whoami", 10 },
    { "什么用户", "whoami", 10 }, { "用户名", "whoami", 10 },
    { "账号", "whoami", 10 },   { "账户", "whoami", 10 },  { "登录", "whoami", 8 },
    { "用户", "whoami", 8 },
    { "时间", "date", 8 },      { "日期", "date", 8 },
    { "几点", "date", 10 },     { "现在时间", "date", 10 },{ "几号", "date", 10 },
    { "什么时候", "date", 10 }, { "当前时间", "date", 10 },{ "时刻", "date", 10 },
    /* ── 网络/端口（lsof 是 L1；ss/netstat 未登记 → L4 不放行）── */
    { "端口", "lsof", 10 },     { "监听", "lsof", 10 },    { "网络连接", "lsof", 10 },
    { "连接", "lsof", 8 },      { "开放", "lsof", 8 },
    /* ── 网卡 / 域名解析（ADR-024）
     * 这一层认的是"这句话在问哪一类东西"，不是"这句话怎么写"：
     * 出现网卡/接口/地址/连通 → ip；出现域名/解析/dns/指向 → dig。
     * 参数不在这里拼：ip 用审查过的字面量参数，dig 的域名走结构化抽取。
     * 「解析一下这个域名」这种没有域名的句子会在参数抽取那步被跳过（不猜），
     * 所以这里不设"必须有域名"的门槛。 */
    { "网卡", "ip", 10 },       { "网络接口", "ip", 10 },  { "网口", "ip", 10 },
    { "地址", "ip", 10 },       { "网络", "ip", 8 },       { "连通", "ip", 8 },
    { "联网", "ip", 8 },        { "对外", "ip", 8 },       { "ip地址", "ip", 10 },
    { "上网", "ip", 8 },        { "连上", "ip", 8 },       { "本机", "ip", 8 },
    { "ip", "ip", 10 },
    { "域名", "dig", 10 },      { "解析", "dig", 10 },      { "dns", "dig", 10 },
    { "指向", "dig", 10 },      { "解析到", "dig", 10 },    { "域名解析", "dig", 10 },
    /* ── 其它工具 ── */
    { "压缩", "tar", 8 },       { "打包", "tar", 8 },      { "解压", "tar", 8 },
    { "下载", "curl", 8 },      { "抓取", "curl", 8 },
    { "权限", "chmod", 8 },     { "授权", "chmod", 8 },
    { "帮助", "man", 8 },       { "文档", "man", 8 },
    { NULL, NULL, 0 }
};

/* 大小写不敏感的子串查找（中文按字节比较，等价于 strstr）。
 * ADR-024 的教训：「我电脑的IP是多少」里的 IP 是大写，而提示词表里是 "ip"，
 * strstr 一区分大小写就判成"没认出任何东西"。这类大小写差异对用户毫无意义，
 * 对识别结果却致命 —— 所以词典匹配一律不区分大小写。 */
static int contains_ci(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return 0;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

/* "这句话在问域名"的直接证据：出现下面这些词之一 */
static int dns_hint_present(const char *input)
{
    static const char *const WORDS[] = { "域名", "解析", "dns", "指向", NULL };
    for (int i = 0; WORDS[i]; i++)
        if (contains_ci(input, WORDS[i])) return 1;
    return 0;
}

/* 常见顶级域：只用来在没有上面那些词时，判断一个"长得像域名"的串到底是主机名还是文件名。
 * 不在表里不是错，只是不下结论（宁可交给下一层，也不把 README.md 当域名去 dig）。
 * 这不是在枚举用户说法 —— TLD 是有限、公开、且与"怎么说这句话"无关的集合。 */
static int tld_is_common(const char *domain)
{
    static const char *const TLDS[] = {
        "com", "cn", "net", "org", "io", "edu", "gov", "info", "biz", "me",
        "co", "top", "xyz", "dev", "app", "ai", "cc", "tv", "cloud", "site",
        "online", "tech", "uk", "jp", "de", "fr", "ru", "kr", "hk", "tw",
        "sg", "us", "eu", "in", "br", "au", "ca", NULL
    };
    const char *dot = strrchr(domain, '.');
    if (!dot || !dot[1]) return 0;
    for (int i = 0; TLDS[i]; i++)
        if (strcasecmp(dot + 1, TLDS[i]) == 0) return 1;
    return 0;
}

static int hint_score_for(const char *name, const char *input)
{
    int score = 0;
    for (int i = 0; HINTS[i].keyword != NULL; i++) {
        if (strcmp(HINTS[i].cap_name, name) != 0) continue;
        if (contains_ci(input, HINTS[i].keyword)) score += HINTS[i].weight;
    }
    return score;
}

/* 把能力 ID 的本地名取出（cli:mkdir → mkdir） */
static const char *local_name(const NlCapability *c)
{
    const char *colon = strchr(c->id, ':');
    return colon ? colon + 1 : c->id;
}

/* 保持 top-K，插入排序（K=8，开销可忽略） */
static void offer(NlCandidates *out, const char *id, int score)
{
    if (score <= 0) return;

    int pos = out->count;
    for (int i = 0; i < out->count; i++) {
        if (strcmp(out->items[i].id, id) == 0) {
            if (score > out->items[i].score) out->items[i].score = score;
            else return;
            pos = i;
            goto bubble;
        }
    }
    if (out->count < NL_DECIDE_K) {
        snprintf(out->items[pos].id, NL_CAP_MAX_ID_LEN, "%s", id);
        out->items[pos].score = score;
        out->count++;
    } else if (score <= out->items[NL_DECIDE_K - 1].score) {
        return;
    } else {
        snprintf(out->items[NL_DECIDE_K - 1].id, NL_CAP_MAX_ID_LEN, "%s", id);
        out->items[NL_DECIDE_K - 1].score = score;
        pos = NL_DECIDE_K - 1;
    }

bubble:
    while (pos > 0 && out->items[pos].score > out->items[pos - 1].score) {
        NlCandidate t = out->items[pos];
        out->items[pos] = out->items[pos - 1];
        out->items[pos - 1] = t;
        pos--;
    }
}

/* 规则层意图 → 默认能力（与 nlsh -c 的执行路径保持同一判断，避免"能执行但选不出来"）。
 * change_dir 是 shell 内建、run_cmd/other 没有单一能力，这三类不出提示。 */
static const char *cap_for_intent(NlIntentType intent)
{
    switch (intent) {
    case NL_INTENT_CREATE_DIR:     return "cli:mkdir";
    case NL_INTENT_CREATE_FILE:    return "cli:touch";
    case NL_INTENT_DELETE:         return "cli:rm";
    case NL_INTENT_LIST_DIR:       return "cli:ls";
    case NL_INTENT_VIEW_FILE:      return "cli:cat";
    case NL_INTENT_FIND_FILES:     return "cli:find";
    case NL_INTENT_SHOW_PROCESS:   return "cli:ps";
    case NL_INTENT_MOVE:           return "cli:mv";
    case NL_INTENT_COPY:           return "cli:cp";
    case NL_INTENT_SEARCH_CONTENT: return "cli:grep";
    case NL_INTENT_SHOW_NETWORK:   return "cli:ip";
    default:                       return NULL;
    }
}

int nl_decide_candidates(const char *input, const NlCapRegistry *reg, NlCandidates *out)
{
    if (!input || !reg || !out) return -1;
    memset(out, 0, sizeof(*out));

    int total = nl_cap_count(reg);
    out->total_scanned = total;

    /* 规则层先判一次：命中的意图给出强提示（+50），远高于词典的 +8/+10。
     * 这样"能执行的说法"与"决策层选得出来的说法"是同一套判断。 */
    NlRuleResult rr;
    if (nl_rule_match(input, &rr) == 0) {
        const char *hint = cap_for_intent(rr.intent);
        if (hint && nl_cap_find(reg, hint)) {
            const NlCapability *hc = nl_cap_find(reg, hint);
            if (hc && hc->perm_level < 4) offer(out, hint, 50);
        }
        nl_rule_result_free(&rr);
    }

    for (int i = 0; i < total; i++) {
        const NlCapability *c = nl_cap_at(reg, i);
        if (!c) continue;
        /* L4 能力不进候选：模型无从选择"禁止执行" */
        if (c->perm_level >= 4) continue;

        const char *name = local_name(c);
        int score = 0;
        if (name[0] != '\0' && contains_ci(input, name)) score += 10;
        score += hint_score_for(name, input);
        offer(out, c->id, score);
    }

    /* 结构性偏置：句子里出现一个**合法域名**（形状校验过的），这句话大概率是在问
     * "这个名字指向哪"，那就是 DNS 查询而不是"看本机地址"。
     * 「我的 IP 是多少」和「baidu.com 的 IP 是多少」句式几乎一样，区别只在
     * 有没有域名 —— 这种判断只能做在结构上，写进关键词规则必然在其中一半上判错。
     * 除非句子里明确指向本机（我的/本机/自己/本地/网卡）。 */
    {
        int local_hint = contains_ci(input, "我的") || contains_ci(input, "本机") ||
                         contains_ci(input, "自己") || contains_ci(input, "本地") ||
                         contains_ci(input, "网卡") || contains_ci(input, "网口");
        if (!local_hint) {
            char *dom = nl_extract_domain(input);
            if (dom && (dns_hint_present(input) || tld_is_common(dom)))
                offer(out, "cli:dig", NL_DECISION_DOMAIN_BIAS);
            free(dom);
        }
    }
    return 0;
}

int nl_decide_grammar(const NlCandidates *c, char *buf, size_t cap)
{
    if (!c || !buf || cap == 0) return -1;
    if (c->count <= 0) return -1;

    size_t used = (size_t)snprintf(buf, cap, "root ::= choice\nchoice ::= ");
    for (int i = 0; i < c->count; i++) {
        int n = snprintf(buf + used, cap - used, "%s\"\\\"%s\\\"\"",
                         (i ? " | " : ""), c->items[i].id);
        if (n < 0 || (size_t)n >= cap - used) return -1;
        used += (size_t)n;
    }
    /* 兜底选项：允许模型拒绝 */
    int n = snprintf(buf + used, cap - used, " | \"\\\"<refuse>\\\"\"\n");
    if (n < 0 || (size_t)n >= cap - used) return -1;
    used += (size_t)n;
    return (int)used;
}

NlNoulLevel nl_noul_combine(NlNoulLevel rule_level, NlNoulLevel model_level)
{
    /* 单调性：取较严者。模型只能让结果更严格，永远不能放宽规则判定 */
    return (model_level > rule_level) ? model_level : rule_level;
}

const char *nl_noul_str(NlNoulLevel level)
{
    switch (level) {
    case NL_NOUL_ALLOW:   return "allow";
    case NL_NOUL_CONFIRM: return "confirm";
    case NL_NOUL_DENY:    return "deny";
    default:              return "deny";
    }
}
