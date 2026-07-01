/*
 * ipc_server.c — Unix socket IPC 服务端
 */
#include "ipc_server.h"
#include "perm_engine.h"
#include "audit_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

static int g_fd = -1;
static const char *g_path = NULL;

int nl_ipc_server_start(const char *socket_path) {
    if(!socket_path) return -1;
    g_path = socket_path;
    unlink(socket_path);
    g_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(g_fd < 0) return -1;
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);
    if(bind(g_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(g_fd); g_fd=-1; return -1; }
    chmod(socket_path, 0660);
    if(listen(g_fd, 5) < 0) { close(g_fd); g_fd=-1; return -1; }
    return 0;
}

void nl_ipc_server_stop(void) {
    if(g_fd >= 0) { close(g_fd); g_fd = -1; }
    if(g_path) unlink(g_path);
}

static int handle_client(int client_fd) {
    char buf[4096];
    ssize_t n = read(client_fd, buf, sizeof(buf)-1);
    if(n <= 0) return -1;
    buf[n] = '\0';

    /* 解析 JSON 请求 */
    cJSON *req_json = cJSON_Parse(buf);
    if(!req_json) { dprintf(client_fd, "{\"error\":\"invalid json\"}\n"); close(client_fd); return -1; }

    NlPermRequest req; memset(&req,0,sizeof(req));
    const cJSON *p;

    p = cJSON_GetObjectItem(req_json, "command_type");
    if(p && cJSON_IsString(p)) req.command_type = cJSON_GetStringValue(p);
    p = cJSON_GetObjectItem(req_json, "nl_input");
    if(p && cJSON_IsString(p)) req.nl_input = cJSON_GetStringValue(p);
    p = cJSON_GetObjectItem(req_json, "generated_cmd");
    if(p && cJSON_IsString(p)) req.generated_cmd = cJSON_GetStringValue(p);
    p = cJSON_GetObjectItem(req_json, "user");
    if(p && cJSON_IsString(p)) req.user = cJSON_GetStringValue(p);
    p = cJSON_GetObjectItem(req_json, "cwd");
    if(p && cJSON_IsString(p)) req.cwd = cJSON_GetStringValue(p);

    /* 解析 paths 数组 */
    p = cJSON_GetObjectItem(req_json, "paths");
    if(p && cJSON_IsArray(p)) {
        int sz = cJSON_GetArraySize(p);
        req.paths = calloc(sz + 1, sizeof(char*));
        for(int i=0; i<sz && i<16; i++) {
            cJSON *item = cJSON_GetArrayItem(p, i);
            if(cJSON_IsString(item)) req.paths[req.path_count++] = strdup(cJSON_GetStringValue(item));
        }
    }

    NlPermResponse resp;
    nl_perm_check(&req, &resp);
    nl_audit_log(&req, &resp);

    /* 构建 JSON 响应 */
    cJSON *resp_json = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_json, "decision", resp.decision==0?"allow":(resp.decision==1?"confirm":"deny"));
    cJSON_AddNumberToObject(resp_json, "level", resp.level);
    cJSON_AddStringToObject(resp_json, "reason", resp.reason);
    cJSON_AddStringToObject(resp_json, "audit_id", resp.audit_id);

    char *out = cJSON_PrintUnformatted(resp_json);
    dprintf(client_fd, "%s\n", out);

    /* 清理 */
    for(int i=0; i<req.path_count; i++) free(req.paths[i]);
    free(req.paths);
    free(out);
    cJSON_Delete(resp_json);
    cJSON_Delete(req_json);
    close(client_fd);
    return 0;
}

int nl_ipc_server_accept(void) {
    if(g_fd < 0) return -1;
    int client = accept(g_fd, NULL, NULL);
    if(client < 0) return -1;
    return handle_client(client);
}
