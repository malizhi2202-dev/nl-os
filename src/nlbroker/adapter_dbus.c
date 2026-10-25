/*
 * adapter_dbus.c — busctl 子进程桥（ADR-018 / T09、T10）
 */
#include "adapter_dbus.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const BUSCTL_CANDIDATES[] = {
    "/usr/bin/busctl", "/bin/busctl", "/usr/local/bin/busctl", NULL
};

int nl_dbus_tool_path(char *out, size_t cap)
{
    for (int i = 0; BUSCTL_CANDIDATES[i] != NULL; i++) {
        if (access(BUSCTL_CANDIDATES[i], X_OK) == 0) {
            snprintf(out, cap, "%s", BUSCTL_CANDIDATES[i]);
            return 0;
        }
    }
    return -1;
}

/* 只允许字母数字与给定分隔符；拒绝 ; $ ` 空格 等一切 shell 元字符 */
static int valid_token(const char *s, const char *extra)
{
    if (!s || *s == '\0') return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (isalnum(*p)) continue;
        if (strchr("_.-", (int)*p) != NULL) continue;
        if (extra && strchr(extra, (int)*p) != NULL) continue;
        return 0;
    }
    return 1;
}

static int push(NlPlan *plan, int *n, const char *s)
{
    if (*n >= NL_CLI_MAX_ARGV) return -1;
    plan->argv[*n] = strdup(s);
    if (!plan->argv[*n]) return -1;
    (*n)++;
    plan->argv[*n] = NULL;
    return 0;
}

int nl_dbus_build_plan(const NlCapability *cap, const char *args_json, NlPlan *plan)
{
    if (!cap || !plan) return -1;
    memset(plan, 0, sizeof(*plan));
    if (strncmp(cap->id, "dbus:", 5) != 0 || !cap->trusted) return -1;

    /* v1 仅支持无参调用 */
    if (args_json && args_json[0] != '\0' && strcmp(args_json, "{}") != 0) return -1;

    char body[NL_CAP_MAX_ID_LEN];
    snprintf(body, sizeof(body), "%s", cap->id + 5);

    char *parts[4] = { NULL, NULL, NULL, NULL };
    int nparts = 0;
    char *save = NULL;
    for (char *t = strtok_r(body, "|", &save); t != NULL && nparts < 4;
         t = strtok_r(NULL, "|", &save)) {
        parts[nparts++] = t;
    }

    const char *service = NULL, *path = NULL, *iface = NULL, *method = NULL;
    if (nparts == 2) {
        service = parts[0];
        path = "/";
        iface = parts[0];
        method = parts[1];
    } else if (nparts == 4) {
        service = parts[0];
        path = parts[1];
        iface = parts[2];
        method = parts[3];
    } else {
        return -1;
    }

    if (!valid_token(service, NULL) || !valid_token(iface, NULL) ||
        !valid_token(method, NULL) || !valid_token(path, "/"))
        return -1;

    char tool[256];
    if (nl_dbus_tool_path(tool, sizeof(tool)) != 0) return -1;
    plan->exe_path = strdup(tool);
    if (!plan->exe_path) return -1;

    int n = 0;
    if (push(plan, &n, "busctl") != 0 || push(plan, &n, "call") != 0 ||
        push(plan, &n, service) != 0 || push(plan, &n, path) != 0 ||
        push(plan, &n, iface) != 0 || push(plan, &n, method) != 0) {
        nl_cli_plan_free(plan);
        return -1;
    }
    return 0;
}

/* 解析 busctl introspect 输出中的方法行 */
int nl_dbus_discover(NlCapRegistry *reg)
{
    if (!reg) return -1;
    char tool[256];
    if (nl_dbus_tool_path(tool, sizeof(tool)) != 0) return -1;

    NlPlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.exe_path = strdup(tool);
    if (!plan.exe_path) return -1;
    int n = 0;
    const char *av[] = { "busctl", "--no-pager", "introspect",
                         "org.freedesktop.DBus", "/org/freedesktop/DBus",
                         "org.freedesktop.DBus" };
    for (size_t i = 0; i < sizeof(av) / sizeof(av[0]); i++)
        if (push(&plan, &n, av[i]) != 0) { nl_cli_plan_free(&plan); return -1; }

    NlCliResult r;
    int rc = nl_cli_execute(&plan, 3000, &r);
    nl_cli_plan_free(&plan);
    if (rc != 0 || r.status != NL_CLI_OK) return -1;

    int added = 0;
    char *line = r.output;
    while (line && *line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        char *m = strstr(line, " method ");
        if (m != NULL) {
            char name[NL_DBUS_METHOD_MAX];
            size_t k = 0;
            while (line[k] != '\0' && !isspace((unsigned char)line[k]) &&
                   k < sizeof(name) - 1) {
                name[k] = line[k];
                k++;
            }
            name[k] = '\0';
            /* busctl introspect 的成员名带前导 '.'，去掉 */
            char *nm = name;
            while (*nm == '.') nm++;
            static const char ID_PREFIX[] =
                "dbus:org.freedesktop.DBus|/org/freedesktop/DBus|org.freedesktop.DBus|";
            size_t plen = sizeof(ID_PREFIX) - 1;
            size_t mlen = strlen(nm);
            if (mlen > 0 && valid_token(nm, NULL) && plen + mlen < sizeof(((NlCapability *)0)->id)) {
                NlCapability c;
                memset(&c, 0, sizeof(c));
                memcpy(c.id, ID_PREFIX, plen);
                memcpy(c.id + plen, nm, mlen + 1);
                snprintf(c.name, sizeof(c.name), "%.127s", nm);
                snprintf(c.namespace, sizeof(c.namespace), "dbus");
                c.perm_level = 2;
                c.effect = NL_CAP_EFFECT_NETWORK;
                c.effect_defaulted = 1;      /* 缺省推断，非人工标注 */
                c.reversible = 1;
                c.trusted = 1;
                c.source = (char *)"bus";
                c.path = (char *)"/usr/bin/busctl";
                if (nl_cap_find(reg, c.id) == NULL && nl_cap_add(reg, &c) == NL_CAP_OK)
                    added++;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
    return added;
}
