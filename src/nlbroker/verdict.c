/*
 * verdict.c — 裁决规则（ADR-016 / T04）
 *
 * 顺序：L4/特权 → 路径（保护路径与白名单）→ 影响面/权限等级 → allow
 */
#include "verdict.h"
#include "path_safety.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *nl_verdict_str(NlVerdict v)
{
    switch (v) {
    case NL_VERDICT_ALLOW:   return "allow";
    case NL_VERDICT_CONFIRM: return "confirm";
    case NL_VERDICT_DENY:    return "deny";
    default:                 return "unknown";
    }
}

/* args 里是否出现 effect 字段（不采信，只做标记） */
static int args_declare_effect(const char *args_json)
{
    return (args_json && strstr(args_json, "\"effect\"") != NULL) ? 1 : 0;
}

/* 是否为需要做路径校验的字符串（绝对路径或 ~/ 开头） */
static int is_path_like(const char *v)
{
    return v && (v[0] == '/' || (v[0] == '~' && v[1] == '/'));
}

/* 展开 ~/ 前缀；非 ~ 路径原样复制 */
static void expand_home(const char *v, char *out, size_t cap)
{
    if (v[0] == '~' && v[1] == '/') {
        const char *home = getenv("HOME");
        snprintf(out, cap, "%s%s", (home && home[0]) ? home : "/root", v + 1);
    } else {
        snprintf(out, cap, "%.1023s", v);
    }
}

/*
 * 递归扫描 args。注意 vendor cJSON 子集：对 object 调用 cJSON_GetArraySize 返回 0，
 * 必须手动遍历 child/next（LESSONS 2026-10-25）。
 */
static int scan_node(const cJSON *node, int allow_write, char *reason, size_t cap)
{
    if (!node) return 0;

    if (cJSON_IsString(node)) {
        const char *v = cJSON_GetStringValue(node);
        if (is_path_like(v)) {
            char expanded[1024];
            NlPathResult pr;
            expand_home(v, expanded, sizeof(expanded));
            pr = nl_path_check(expanded, allow_write);
            int bad = (pr.status != NL_PATH_PASS);
            if (bad) snprintf(reason, cap, "%.200s", pr.reason);
            nl_path_result_free(&pr);
            return bad;
        }
        return 0;
    }

    if (cJSON_IsArray(node) || cJSON_IsObject(node)) {
        for (const cJSON *it = node->child; it != NULL; it = it->next) {
            if (scan_node(it, allow_write, reason, cap)) return 1;
        }
    }
    return 0;
}

static void fill_alternatives(NlVerdictResult *out)
{
    static const char *const alts[] = { "cli:ls", "cli:stat", "cli:cat" };
    out->alt_count = 0;
    for (size_t i = 0; i < sizeof(alts) / sizeof(alts[0]) && out->alt_count < 3; i++) {
        snprintf(out->alternative_ids[out->alt_count], NL_CAP_MAX_ID_LEN, "%s", alts[i]);
        out->alt_count++;
    }
}

static int deny(NlVerdictResult *out, const char *why)
{
    out->verdict = NL_VERDICT_DENY;
    snprintf(out->reason, sizeof(out->reason), "%s", why);
    fill_alternatives(out);
    return 0;
}

int nl_verdict_eval(const NlCapability *cap, const char *args_json, NlVerdictResult *out)
{
    if (!cap || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->perm_level = cap->perm_level;
    out->effect = cap->effect;
    out->untrusted_effect_attempt = args_declare_effect(args_json);

    /* 1) L4 与特权能力：直接拒绝 */
    if (cap->perm_level >= 4) return deny(out, "L4 禁止执行");
    if (cap->effect == NL_CAP_EFFECT_PRIVILEGE) return deny(out, "特权操作需人工提权");

    /* 2) 路径校验（保护路径 / 白名单 / 穿越） */
    if (args_json && args_json[0] != '\0') {
        cJSON *root = cJSON_Parse(args_json);
        if (root) {
            char reason[256];
            int allow_write = (cap->effect != NL_CAP_EFFECT_READ) ? 1 : 0;
            reason[0] = '\0';
            int bad = scan_node(root, allow_write, reason, sizeof(reason));
            cJSON_Delete(root);
            if (bad) return deny(out, reason[0] ? reason : "路径校验未通过");
        }
    }

    /* 3) 影响面与权限等级 → 需要确认 */
    if (cap->effect != NL_CAP_EFFECT_READ || cap->perm_level >= 2) {
        out->verdict = NL_VERDICT_CONFIRM;
        snprintf(out->reason, sizeof(out->reason), "需确认（effect=%s, L%d）",
                 nl_cap_effect_str(cap->effect), cap->perm_level);
        return 0;
    }

    out->verdict = NL_VERDICT_ALLOW;
    snprintf(out->reason, sizeof(out->reason), "只读且无需确认");
    return 0;
}
