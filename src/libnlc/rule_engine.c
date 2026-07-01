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

/* 输入原文中是否包含某个关键词（子串匹配，忽略大小写） */
static int has_keyword(const char *input, const char *word) {
    if (!input || !word) return 0;
    /* 简单的子串搜索（忽略大小写） */
    size_t wlen = strlen(word);
    if (wlen == 0) return 0;
    for (const char *p = input; *p; p++) {
        if (strncasecmp(p, word, wlen) == 0) return 1;
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

/* 查找关键词后的内容（如 "删除" 后面的剩余内容作为 target） */
static char *extract_after_key_long(const char *input, const char *key) {
    if (!input || !key) return NULL;
    const char *pos = strstr(input, key);
    if (!pos) return NULL;
    pos += strlen(key);
    while (*pos && isspace((unsigned char)*pos)) pos++;
    if (!*pos) return NULL;
    /* 取到句尾（或下一个关键词） */
    return strdup(pos);
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
