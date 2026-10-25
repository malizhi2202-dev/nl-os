/*
 * template_fill.c — 模板填充 Layer 4 实现
 *
 * intent + entities → 填充命令模板 → 命令字符串 + 权限级别 + 路径列表。
 */

#include "template_fill.h"
#include "templates_db.h"
#include "path_safety.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 槽位查找 */
static const char *find_slot(const NlSlot *slots, int count, const char *name) {
    for (int i = 0; i < count; i++)
        if (slots[i].name && strcmp(slots[i].name, name) == 0)
            return slots[i].value;
    return NULL;
}

NlPermLevel nl_intent_perm_level(NlIntentType intent) {
    for (int i = 0; TEMPLATES[i].template_id != 0; i++)
        if (TEMPLATES[i].intent == intent)
            return TEMPLATES[i].perm_level;
    return NL_PERM_L2;
}

/* 模板需要几个槽位 */
static int template_slot_count(const NlCmdTemplate *t) {
    int n = 0;
    while (n < 6 && t->slot_names[n]) n++;
    return n;
}

/* 模板的每个槽位都能填上（且域名过校验）才算"可满足" */
static int template_satisfiable(const NlCmdTemplate *t, const NlSlot *slots, int count) {
    for (int i = 0; i < 6 && t->slot_names[i]; i++) {
        const char *v = find_slot(slots, count, t->slot_names[i]);
        if (!v || !*v) return 0;
        if (strcmp(t->slot_names[i], "domain") == 0 && nl_domain_check(v) != 0) return 0;
    }
    return 1;
}

/* 模板选择（ADR-024）
 *
 * 旧语义：template_id==0 就是"该 intent 的第一张模板"。规则层用它无妨（规则点名的
 * 模板 id 就是答案），但**模型层用它会丢实体**：模型说 list_dir + location=/var/log，
 * 结果仍然是裸 `ls -la`——理解对了，命令里却没有那个路径。这是"模型层帮不上忙"的
 * 直接原因之一，不能靠再加规则解决。
 *
 * 新语义：template_id==0 时按槽位选 ——
 *   1) 在所有"槽位能填满"的模板里挑字面槽位最多的（最具体的那张）；
 *   2) 没有可填满的，退到不需要任何槽位的通用那张；
 *   3) 连通用那张都没有，就整条失败（宁可如实说"无法理解"，也不执行半条命令）。
 *   顶层 intent=other 不参与自动选择：OTHER 里放模板会让 template_id==0 的
 *   泛规则（如 {"怎么"}）静默拿到一张命令模板。
 */
static const NlCmdTemplate *pick_template(NlIntentType intent, const NlSlot *slots, int count) {
    if (intent == NL_INTENT_OTHER) return NULL;

    const NlCmdTemplate *best = NULL;
    const NlCmdTemplate *generic = NULL;
    int best_need = -1;

    for (int i = 0; TEMPLATES[i].template_id != 0; i++) {
        const NlCmdTemplate *t = &TEMPLATES[i];
        if (t->intent != intent || !t->template_str) continue;
        if (!template_satisfiable(t, slots, count)) continue;

        int need = template_slot_count(t);
        if (need == 0) {
            if (!generic) generic = t;
            continue;
        }
        if (need > best_need) { best_need = need; best = t; }
    }
    return best ? best : generic;
}

int nl_template_fill(NlIntentType intent, int template_id,
                     const NlSlot *slots, int slot_count, NlCommand *result) {
    if (!result) return -1;
    memset(result, 0, sizeof(NlCommand));

    /* 提供了 domain 槽位却不合法 → 整条失败（fail-closed）。
     * 不能"退到不需要槽位的那张模板"：那样「解析 -f xx 这个域名」会静默变成
     * `ip -br addr`（回答的完全是另一个问题）。缺参数可以退，参数非法只能拒。 */
    {
        const char *dom = find_slot(slots, slot_count, "domain");
        if (dom && nl_domain_check(dom) != 0) return -1;
    }

    /* 查找模板 */
    const NlCmdTemplate *tmpl = NULL;
    if (template_id != 0) {
        for (int i = 0; TEMPLATES[i].template_id != 0; i++) {
            if (TEMPLATES[i].intent == intent && TEMPLATES[i].template_id == template_id) {
                tmpl = &TEMPLATES[i];
                break;
            }
        }
    } else {
        tmpl = pick_template(intent, slots, slot_count);
    }
    if (!tmpl || !tmpl->template_str) return -1;

    /* 域名槽位预检（ADR-024）：域名是模板里唯一的自由文本参数，两道关都要过。
     *   1) 必须存在 —— 缺了就不是一次完整查询，不能生成 `dig +short` 这种半条命令；
     *   2) 必须通过 nl_domain_check —— 挡住 `-f /etc/passwd` 这类"参数当选项"的注入。
     * 任一不满足就整条失败，让上层如实说"无法理解"，而不是执行一条残缺命令。 */
    if (strstr(tmpl->template_str, "{domain}") != NULL) {
        const char *dom = find_slot(slots, slot_count, "domain");
        if (!dom || nl_domain_check(dom) != 0) return -1;
    }

    /* 构建命令：替换 {slot} */
    const char *src = tmpl->template_str;
    size_t cap = strlen(src) + 256;
    char *cmd = malloc(cap);
    if (!cmd) return -1;
    size_t pos = 0;

    while (*src) {
        if (*src == '{') {
            const char *end = strchr(src, '}');
            if (end) {
                size_t nlen = end - src - 1;
                char sname[64];
                size_t cp = nlen < sizeof(sname)-1 ? nlen : sizeof(sname)-1;
                memcpy(sname, src + 1, cp);
                sname[cp] = '\0';
                const char *val = find_slot(slots, slot_count, sname);
                if (val) {
                    size_t vlen = strlen(val);
                    while (pos + vlen + 1 > cap) { cap *= 2; cmd = realloc(cmd, cap); }
                    memcpy(cmd + pos, val, vlen);
                    pos += vlen;
                }
                src = end + 1;
                continue;
            }
        }
        if (pos + 1 >= cap) { cap *= 2; cmd = realloc(cmd, cap); }
        cmd[pos++] = *src++;
    }
    cmd[pos] = '\0';
    result->command = cmd;
    result->perm_level = tmpl->perm_level;
    result->needs_confirm = (tmpl->perm_level >= NL_PERM_L2);

    /* 提取路径 */
    const char *path_keys[] = {"target", "location", "source", "dest", "name", NULL};
    for (int i = 0; path_keys[i]; i++) {
        const char *v = find_slot(slots, slot_count, path_keys[i]);
        if (v && result->path_count < NL_MAX_PATHS) {
            /* 去重 */
            int dup = 0;
            for (int j = 0; j < result->path_count; j++)
                if (strcmp(result->paths[j], v) == 0) { dup = 1; break; }
            if (!dup) result->paths[result->path_count++] = strdup(v);
        }
    }
    return 0;
}

void nl_command_free(NlCommand *cmd) {
    if (!cmd) return;
    free(cmd->command);
    for (int i = 0; i < cmd->path_count; i++) free(cmd->paths[i]);
    memset(cmd, 0, sizeof(NlCommand));
}
