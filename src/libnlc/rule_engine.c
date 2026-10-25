/*
 * rule_engine.c — 规则引擎 Layer 2 实现
 *
 * 关键词 + 模板匹配。O(n) 遍历规则（n ≈ 100，足够快）。
 * 命中返回 intent + slots，未命中返回 -1 触发 Layer 3 模型推理。
 */

#include "rule_engine.h"
#include "rules_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
 * 辅助函数
 * ============================================================ */

/* 分词：将输入按空格/标点分割，返回 malloc 的 token 数组 */
static char **tokenize(const char *input, int *count) {
    if (!input) { *count = 0; return NULL; }
    char *work = strdup(input);
    if (!work) { *count = 0; return NULL; }

    int cap = 32;
    char **tokens = malloc(cap * sizeof(char *));
    int n = 0;

    char *p = work;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (n >= cap) { cap *= 2; tokens = realloc(tokens, cap * sizeof(char *)); }
        tokens[n++] = strndup(start, p - start);
    }
    tokens[n] = NULL;
    *count = n;
    free(work);
    return tokens;
}

/* 释放 token 数组 */
static void free_tokens(char **tokens, int count) {
    for (int i = 0; i < count; i++) free(tokens[i]);
    free(tokens);
}

/* 关键词是否「纯 ASCII 单词」（cp / ls / ps / top 这类命令名） */
static int is_ascii_word(const char *w) {
    if (!w || !*w) return 0;
    for (const char *p = w; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/')) return 0;
    }
    return 1;
}

/* ASCII 单词必须在词边界上：`cp` 不能命中 `CPU`，`top` 不能命中 `desktop`。
 * 中文关键词走子串匹配（中文没有空格分词），只有 ASCII 命令名需要这条。
 * 事故：`{"cp"}` 是子串匹配 → 「看看谁在占CPU」被解析成 `cp -r`（一句提问变成拷贝）。 */
static int ascii_boundary_ok(const char *s, const char *match, size_t len) {
    if (match > s) {
        unsigned char prev = (unsigned char)match[-1];
        if (isalnum(prev) || prev == '_' || prev == '-' || prev == '.' || prev == '/') return 0;
    }
    unsigned char next = (unsigned char)match[len];
    if (next && (isalnum(next) || next == '_' || next == '-' || next == '.' || next == '/')) return 0;
    return 1;
}

/* 输入原文中是否包含某个关键词（中文子串匹配；ASCII 命令名要求词边界，忽略大小写） */
static int has_keyword(const char *input, const char *word) {
    if (!input || !word) return 0;
    size_t wlen = strlen(word);
    if (wlen == 0) return 0;

    int ascii = is_ascii_word(word);
    for (const char *p = input; *p; p++) {
        if (strncasecmp(p, word, wlen) == 0) {
            if (!ascii || ascii_boundary_ok(input, p, wlen)) return 1;
        }
    }
    return 0;
}

/* 查找关键词后的内容（如 "叫" 后面的词） */
static char *extract_after_key(const char *input, const char *key) {
    if (!input || !key) return NULL;
    const char *pos = strstr(input, key);
    if (!pos) return NULL;
    pos += strlen(key);
    while (*pos && isspace((unsigned char)*pos)) pos++;
    if (!*pos) return NULL;
    /* 取下一个词 */
    const char *end = pos;
    while (*end && !isspace((unsigned char)*end)) end++;
    return strndup(pos, end - pos);
}

/* 句尾修饰后缀：`创建一个叫 demo 的文件夹` 的目标是 demo，不是 `demo 的文件夹`。
 * 只在一个"结构词"前面剥离：要么后缀以 `的` 开头（`的文件夹`），
 * 要么后缀前是空白（`README.md 文件`）——否则 `删除 旧文件` 会被啃成 `旧`。 */
static const char *const SLOT_SUFFIXES[] = {
    "的内容", "的文件", "的目录", "的文件夹", "的文件名", "的名称",
    /* 组合形式必须排在前面：`logs 目录下` 要先匹配 `目录下` 才轮不到 `下` */
    "文件夹下面", "文件夹里", "文件夹下", "文件夹中",
    "目录下面", "目录里", "目录下", "目录中",
    "文件夹", "目录", "文件", "内容", "名字", "下面", "下", "里面", "里", "中", NULL
};
static const char *const SLOT_SUFFIX_ALWAYS[] = {
    "的内容", "的文件", "的目录", "的文件夹", "的文件名", "的名称", NULL
};

/* 去掉两侧空白与句末标点 */
static size_t trim_slot_span(const char *s, size_t len) {
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    while (len > 0 && (s[len - 1] == '\'' || s[len - 1] == ',' || s[len - 1] == '.' ||
                       (unsigned char)s[len - 1] >= 0x80)) {
        /* 中文标点：。，！？； 的 UTF-8 尾字节也 >=0x80，这里只在明确是标点时截断 */
        if ((unsigned char)s[len - 1] == 0x82 && len >= 3 && (unsigned char)s[len - 2] == 0x80 &&
            ((unsigned char)s[len - 3] == 0xA1 || (unsigned char)s[len - 3] == 0xEF)) { len -= 3; continue; }
        break;
    }
    return len;
}

/* 去掉包裹的引号（"x" / 'x' / “x”） */
static char *strip_wrapping_quotes(const char *s) {
    size_t len = strlen(s);
    if (len >= 2) {
        const char *open = s;
        const char *close = s + len - 1;
        int quoted = (open[0] == '"' && close[0] == '"') || (open[0] == '\'' && close[0] == '\'');
        /* 中文引号恒为 3 字节： “ ” ‘ ’ */
        if (!quoted && len >= 6) {
            int o3 = (unsigned char)open[0] == 0xE2 && (unsigned char)open[1] == 0x80 &&
                     ((unsigned char)open[2] == 0x9C || (unsigned char)open[2] == 0x98);
            int c3 = (unsigned char)close[-2] == 0xE2 && (unsigned char)close[-1] == 0x80 &&
                     ((unsigned char)close[0] == 0x9D || (unsigned char)close[0] == 0x99);
            if (o3 && c3) return strndup(s + 3, len - 6);
        }
        if (quoted) return strndup(s + 1, len - 2);
    }
    return strdup(s);
}

/* 查找关键词后的内容（如 "删除" 后面的剩余内容作为 target） */
static char *extract_after_key_long(const char *input, const char *key) {
    if (!input || !key) return NULL;
    const char *pos = strstr(input, key);
    if (!pos) return NULL;
    pos += strlen(key);
    while (*pos && isspace((unsigned char)*pos)) pos++;
    if (!*pos) return NULL;

    char *work = strip_wrapping_quotes(pos);
    if (!work) return NULL;

    /* 反复剥离后缀，直到不再变化 */
    for (int guard = 0; guard < 4; guard++) {
        size_t len = trim_slot_span(work, strlen(work));
        work[len] = '\0';
        int stripped = 0;

        for (int i = 0; SLOT_SUFFIX_ALWAYS[i]; i++) {
            size_t sl = strlen(SLOT_SUFFIX_ALWAYS[i]);
            if (len > sl && strcmp(work + len - sl, SLOT_SUFFIX_ALWAYS[i]) == 0) {
                work[len - sl] = '\0';
                stripped = 1;
                break;
            }
        }
        if (!stripped) {
            for (int i = 0; SLOT_SUFFIXES[i]; i++) {
                size_t sl = strlen(SLOT_SUFFIXES[i]);
                if (len <= sl || strcmp(work + len - sl, SLOT_SUFFIXES[i]) != 0) continue;
                /* 后缀前必须是空白，避免把 `旧文件` 啃成 `旧` */
                if (!isspace((unsigned char)work[len - sl - 1])) continue;
                work[len - sl] = '\0';
                stripped = 1;
                break;
            }
        }
        if (!stripped) {
            /* 结尾单独的 `的` 也去掉（`叫 x 的`） */
            size_t dl = strlen("的");
            if (len > dl && strcmp(work + len - dl, "的") == 0 &&
                isspace((unsigned char)work[len - dl - 1])) {
                work[len - dl] = '\0';
                stripped = 1;
            }
        }
        if (!stripped) break;
    }

    size_t final_len = trim_slot_span(work, strlen(work));
    while (final_len > 0 && isspace((unsigned char)work[final_len - 1])) final_len--;
    if (final_len == 0) { free(work); return NULL; }
    work[final_len] = '\0';

    /* 前导量词/介绍词：`建一个 logs 文件夹` 的目标是 logs */
    static const char *const LEAD_WORDS[] = { "一个", "叫做", "名为", "叫", "个", NULL };
    for (int i = 0; LEAD_WORDS[i]; i++) {
        size_t ll = strlen(LEAD_WORDS[i]);
        size_t cur = strlen(work);
        if (cur > ll && strncmp(work, LEAD_WORDS[i], ll) == 0 &&
            (isspace((unsigned char)work[ll]) || (unsigned char)work[ll] >= 0x80)) {
            memmove(work, work + ll, cur - ll + 1);
            while (isspace((unsigned char)work[0])) memmove(work, work + 1, strlen(work));
            break;
        }
    }

    char *out = strip_wrapping_quotes(work);
    free(work);
    if (out && out[0] == '\0') { free(out); return NULL; }
    return out;
}

/* ============================================================
 * 意图名称
 * ============================================================ */
static const char *INTENT_NAMES[] = {
    "create_file", "create_dir", "delete", "move", "copy",
    "list_dir", "view_file", "find_files", "change_dir",
    "show_process", "search_content", "run_cmd", "other"
};

const char *nl_intent_name(NlIntentType intent) {
    if (intent < 0 || intent > NL_INTENT_OTHER) return "unknown";
    return INTENT_NAMES[intent];
}

/* ============================================================
 * 公共 API
 * ============================================================ */

/* 槽位值里是否含 ASCII 字母数字（即"看起来像个命令/文件名"） */
static int slots_have_ascii(const NlRuleResult *r) {
    for (int i = 0; i < r->slot_count; i++) {
        const char *v = r->slots[i].value;
        if (!v) continue;
        for (const char *p = v; *p; p++) {
            if (isalnum((unsigned char)*p)) return 1;
        }
    }
    return 0;
}

int nl_rule_match(const char *input, NlRuleResult *result) {
    if (!input || !result) return -1;
    memset(result, 0, sizeof(NlRuleResult));

    /* 遍历规则，找到第一个所有 keywords 都匹配的（子串匹配） */
    for (int i = 0; RULES[i].id != 0; i++) {
        const NlRule *rule = &RULES[i];
        int all_matched = 1;

        /* 检查所有必须关键词（子串搜索，适配中英文混排） */
        for (int k = 0; rule->keywords[k]; k++) {
            if (!has_keyword(input, rule->keywords[k])) {
                all_matched = 0;
                break;
            }
        }

        if (!all_matched) continue;

        /* 命中！提取槽位 */
        result->matched = 1;
        result->rule_id = rule->id;
        result->intent = rule->intent;
        result->template_id = rule->template_id;
        result->confidence = 1.0f;

        /* 提取槽位值 */
        for (int s = 0; rule->slots[s].name; s++) {
            const NlRuleSlotDef *slot = &rule->slots[s];
            if (result->slot_count >= NL_MAX_SLOTS) break;

            char *value = NULL;
            if (strcmp(slot->match_type, "word") == 0) {
                value = extract_after_key(input, slot->key ? slot->key : rule->keywords[0]);
            } else if (strcmp(slot->match_type, "after_key") == 0) {
                value = extract_after_key_long(input, slot->key);
            }

            if (value) {
                result->slots[result->slot_count].name = strdup(slot->name);
                result->slots[result->slot_count].value = value;
                result->slot_count++;
            }
        }

        /* 「运行/执行 X」要求 X 像个命令（含 ASCII 字母数字）。
         * 否则「查看运行时间」会把「时间」当成要执行的命令，报「本机没有该能力：cli:时间」，
         * 而它其实是在问 uptime。无槽位的规则（如 {"make"}）不受影响。 */
        if (rule->intent == NL_INTENT_RUN_CMD && result->slot_count > 0 &&
            !slots_have_ascii(result)) {
            nl_rule_result_free(result);   /* 这次命中不成立：丢弃后继续试后面的规则 */
            continue;
        }

        return 0;
    }

    return -1; /* 未命中 */
}

void nl_rule_result_free(NlRuleResult *result) {
    if (!result) return;
    for (int i = 0; i < result->slot_count; i++) {
        free(result->slots[i].name);
        free(result->slots[i].value);
    }
    memset(result, 0, sizeof(NlRuleResult));
}

int nl_rule_count(void) {
    return RULE_COUNT;
}
