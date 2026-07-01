/*
 * template_fill.c — 模板填充 Layer 4 实现
 *
 * intent + entities → 填充命令模板 → 命令字符串 + 权限级别 + 路径列表。
 */

#include "template_fill.h"
#include "templates_db.h"
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

int nl_template_fill(NlIntentType intent, int template_id,
                     const NlSlot *slots, int slot_count, NlCommand *result) {
    if (!result) return -1;
    memset(result, 0, sizeof(NlCommand));

    /* 查找模板 */
    const NlCmdTemplate *tmpl = NULL;
    for (int i = 0; TEMPLATES[i].template_id != 0; i++) {
        if (TEMPLATES[i].intent == intent &&
            (template_id == 0 || TEMPLATES[i].template_id == template_id)) {
            tmpl = &TEMPLATES[i];
            break;
        }
    }
    if (!tmpl || !tmpl->template_str) return -1;

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
