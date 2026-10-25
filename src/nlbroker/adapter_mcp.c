/*
 * adapter_mcp.c — MCP stdio 客户端（ADR-018 / T11）
 */
#include "adapter_mcp.h"
#include "cJSON.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define NL_MCP_LINE_MAX 65536

struct NlMcpClient {
    char         server[NL_MCP_NAME_MAX];
    pid_t        pid;
    int          to_child;      /* 父写 */
    int          from_child;    /* 父读 */
    int          next_id;
    int          alive;
};

static int write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int send_json(NlMcpClient *c, cJSON *msg)
{
    char *s = cJSON_PrintUnformatted(msg);
    if (!s) return -1;
    int rc = write_all(c->to_child, s, strlen(s));
    if (rc == 0) rc = write_all(c->to_child, "\n", 1);
    free(s);
    return rc;
}

/* 读一行响应（带超时）；返回 0 成功 */
static int read_line(NlMcpClient *c, char *out, size_t cap, int timeout_ms)
{
    size_t used = 0;
    long waited = 0;
    while (used + 1 < cap) {
        struct pollfd p = { .fd = c->from_child, .events = POLLIN, .revents = 0 };
        int pr = poll(&p, 1, timeout_ms - (int)waited < 0 ? 0 : timeout_ms - (int)waited);
        if (pr <= 0) return -1;
        ssize_t r = read(c->from_child, out + used, cap - 1 - used);
        if (r <= 0) return -1;
        for (ssize_t i = 0; i < r; i++) {
            if (out[used + (size_t)i] == '\n') {
                out[used + (size_t)i] = '\0';
                return 0;
            }
        }
        used += (size_t)r;
        waited += 50;
        if (waited > timeout_ms) return -1;
    }
    return -1;
}

static int rpc_call(NlMcpClient *c, const char *method, cJSON *params,
                    cJSON **result, int timeout_ms)
{
    int id = c->next_id++;
    cJSON *msg = cJSON_CreateObject();
    if (!msg) return -1;
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(msg, "id", (double)id);
    cJSON_AddStringToObject(msg, "method", method);
    if (params) cJSON_AddItemToObject(msg, "params", params);
    int rc = send_json(c, msg);
    cJSON_Delete(msg);
    if (rc != 0) return -1;

    char line[NL_MCP_LINE_MAX];
    if (read_line(c, line, sizeof(line), timeout_ms) != 0) return -1;

    cJSON *resp = cJSON_Parse(line);
    if (!resp) return -1;
    cJSON *err = cJSON_GetObjectItem(resp, "error");
    if (err) {
        cJSON_Delete(resp);
        return -1;
    }
    cJSON *res = cJSON_GetObjectItem(resp, "result");
    if (!res) {
        cJSON_Delete(resp);
        return -1;
    }
    /* 把 result 深拷贝出来：重新序列化再解析，避免悬垂指针 */
    char *txt = cJSON_PrintUnformatted(res);
    cJSON_Delete(resp);
    if (!txt) return -1;
    cJSON *copy = cJSON_Parse(txt);
    free(txt);
    if (!copy) return -1;
    if (result) *result = copy;
    else cJSON_Delete(copy);
    return 0;
}

NlMcpClient *nl_mcp_start(const char *server_name, const char *cmd,
                          char *const *args, int nargs)
{
    if (!server_name || !cmd) return NULL;

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0) return NULL;
    if (pipe(out_pipe) != 0) { close(in_pipe[0]); close(in_pipe[1]); return NULL; }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return NULL;
    }
    if (pid == 0) {
        (void)dup2(in_pipe[0], STDIN_FILENO);
        (void)dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);

        char *argv[64];
        int n = 0;
        argv[n++] = (char *)cmd;
        for (int i = 0; i < nargs && n < 63; i++) argv[n++] = args[i];
        argv[n] = NULL;
        execv(cmd, argv);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    NlMcpClient *c = calloc(1, sizeof(*c));
    if (!c) {
        kill(pid, SIGKILL);
        close(in_pipe[1]); close(out_pipe[0]);
        return NULL;
    }
    snprintf(c->server, sizeof(c->server), "%s", server_name);
    c->pid = pid;
    c->to_child = in_pipe[1];
    c->from_child = out_pipe[0];
    c->next_id = 1;
    c->alive = 1;

    cJSON *params = cJSON_CreateObject();
    if (params) {
        cJSON_AddStringToObject(params, "protocolVersion", "2024-11-05");
        cJSON_AddItemToObject(params, "capabilities", cJSON_CreateObject());
        cJSON *ci = cJSON_CreateObject();
        cJSON_AddStringToObject(ci, "name", "nlbroker");
        cJSON_AddStringToObject(ci, "version", "0.2");
        cJSON_AddItemToObject(params, "clientInfo", ci);
    }
    cJSON *res = NULL;
    if (rpc_call(c, "initialize", params, &res, 5000) != 0) {
        nl_mcp_stop(c);
        return NULL;
    }
    cJSON_Delete(res);

    /* notifications/initialized（无 id，无需响应） */
    cJSON *note = cJSON_CreateObject();
    if (note) {
        cJSON_AddStringToObject(note, "jsonrpc", "2.0");
        cJSON_AddStringToObject(note, "method", "notifications/initialized");
        (void)send_json(c, note);
        cJSON_Delete(note);
    }
    return c;
}

const char *nl_mcp_server_name(const NlMcpClient *c)
{
    return c ? c->server : NULL;
}

void nl_mcp_stop(NlMcpClient *c)
{
    if (!c) return;
    if (c->to_child >= 0) close(c->to_child);
    if (c->from_child >= 0) close(c->from_child);
    if (c->pid > 0) {
        kill(c->pid, SIGKILL);
        (void)waitpid(c->pid, NULL, 0);
    }
    free(c);
}

int nl_mcp_list_tools(NlMcpClient *c, NlMcpTool *out, int max, int *count)
{
    if (!c || !out || !count) return -1;
    *count = 0;

    cJSON *res = NULL;
    if (rpc_call(c, "tools/list", cJSON_CreateObject(), &res, 5000) != 0) return -1;

    cJSON *tools = cJSON_GetObjectItem(res, "tools");
    if (!cJSON_IsArray(tools)) {
        cJSON_Delete(res);
        return -1;
    }
    int n = cJSON_GetArraySize(tools);
    for (int i = 0; i < n && *count < max; i++) {
        cJSON *t = cJSON_GetArrayItem(tools, i);
        cJSON *nm = cJSON_GetObjectItem(t, "name");
        const char *nmv = cJSON_IsString(nm) ? cJSON_GetStringValue(nm) : NULL;
        if (!nmv) continue;
        NlMcpTool *o = &out[*count];
        memset(o, 0, sizeof(*o));
        snprintf(o->name, sizeof(o->name), "%.95s", nmv);

        cJSON *d = cJSON_GetObjectItem(t, "description");
        const char *dv = cJSON_IsString(d) ? cJSON_GetStringValue(d) : "";
        snprintf(o->description, sizeof(o->description), "%.255s", dv ? dv : "");

        cJSON *schema = cJSON_GetObjectItem(t, "inputSchema");
        if (schema) {
            char *txt = cJSON_PrintUnformatted(schema);
            if (txt) {
                snprintf(o->schema, sizeof(o->schema), "%.1023s", txt);
                free(txt);
            }
        }
        (*count)++;
    }
    cJSON_Delete(res);
    return 0;
}

int nl_mcp_call_tool(NlMcpClient *c, const char *tool, const char *args_json,
                     char *out, size_t cap)
{
    if (!c || !tool || !out) return -1;
    cJSON *params = cJSON_CreateObject();
    if (!params) return -1;
    cJSON_AddStringToObject(params, "name", tool);
    cJSON *arguments = (args_json && args_json[0]) ? cJSON_Parse(args_json) : cJSON_CreateObject();
    if (!arguments) arguments = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "arguments", arguments);

    cJSON *res = NULL;
    if (rpc_call(c, "tools/call", params, &res, 8000) != 0) return -1;

    /* 优先取 content[0].text，否则整体序列化 */
    const char *text = NULL;
    cJSON *content = cJSON_GetObjectItem(res, "content");
    if (cJSON_IsArray(content)) {
        cJSON *first = cJSON_GetArrayItem(content, 0);
        cJSON *tv = first ? cJSON_GetObjectItem(first, "text") : NULL;
        if (cJSON_IsString(tv)) text = cJSON_GetStringValue(tv);
    }
    if (text) {
        snprintf(out, cap, "%.*s", (int)(cap - 1), text);
    } else {
        char *txt = cJSON_PrintUnformatted(res);
        if (!txt) { cJSON_Delete(res); return -1; }
        snprintf(out, cap, "%.*s", (int)(cap - 1), txt);
        free(txt);
    }
    cJSON_Delete(res);
    return 0;
}

int nl_mcp_add_to_registry(NlMcpClient *c, NlCapRegistry *reg)
{
    if (!c || !reg) return -1;
    NlMcpTool tools[NL_MCP_MAX_TOOLS];
    int count = 0;
    if (nl_mcp_list_tools(c, tools, NL_MCP_MAX_TOOLS, &count) != 0) return -1;

    int added = 0;
    for (int i = 0; i < count; i++) {
        NlCapability cap;
        memset(&cap, 0, sizeof(cap));
        size_t sl = strlen(c->server);
        size_t tl = strlen(tools[i].name);
        if (4 + sl + 1 + tl + 1 > sizeof(cap.id)) continue;   /* 超长跳过，不截断 */
        memcpy(cap.id, "mcp:", 4);
        memcpy(cap.id + 4, c->server, sl);
        cap.id[4 + sl] = '/';
        memcpy(cap.id + 4 + sl + 1, tools[i].name, tl + 1);
        snprintf(cap.name, sizeof(cap.name), "%.127s", tools[i].name);
        snprintf(cap.namespace, sizeof(cap.namespace), "mcp");
        cap.perm_level = 2;
        cap.effect = NL_CAP_EFFECT_WRITE;     /* 工具可能改状态 */
        cap.effect_defaulted = 1;
        cap.reversible = 0;
        cap.trusted = 1;
        cap.source = (char *)"mcp";
        cap.path = (char *)"mcp";
        if (nl_cap_find(reg, cap.id) == NULL && nl_cap_add(reg, &cap) == NL_CAP_OK) added++;
    }
    return added;
}
