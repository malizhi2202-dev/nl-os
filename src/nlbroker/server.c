/*
 * server.c — Unix socket 服务端（ADR-017 / T02）
 *
 * v1 单线程：一次处理一个连接直到对端关闭。
 * 行分帧：上限 1MB，超长且无换行则断开（防止内存被撑爆）。
 */
#include "server.h"
#include "cip.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define NL_BROKER_MAX_LINE  (1024 * 1024)
#define NL_BROKER_BACKLOG   8
#define NL_BROKER_OUT_CAP   8192

static volatile sig_atomic_t g_stop = 0;
static NlLineHandler g_handler;
static void *g_handler_ctx;

void nl_server_set_handler(NlLineHandler h, void *ctx) { g_handler = h; g_handler_ctx = ctx; }

void nl_server_request_stop(void) { g_stop = 1; }

static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/* T02：只有 discover 有实现（空能力表）；T08 换成 dispatch.c */
int nl_server_handle_line(const char *line, char *out, size_t out_cap)
{
    if (g_handler) return g_handler(g_handler_ctx, line, out, out_cap);

    NlCipReq  req;
    NlCipResp resp;
    memset(&resp, 0, sizeof(resp));
    resp.v = NL_CIP_VERSION;

    int rc = nl_cip_parse(line, &req);
    if (rc != NL_CIP_OK) {
        resp.ok = 0;
        resp.error = (NlCipErr)rc;
        /* 解析已取到信封 id 时回带，便于客户端关联失败请求 */
        if (req.id[0] != '\0') snprintf(resp.id, sizeof(resp.id), "%s", req.id);
        else snprintf(resp.id, sizeof(resp.id), "-");
        return nl_cip_render(&resp, out, out_cap);
    }

    snprintf(resp.id, sizeof(resp.id), "%s", req.id);

    switch (req.op) {
    case NL_CIP_OP_DISCOVER:
        resp.ok = 1;
        snprintf(resp.payload_json, sizeof(resp.payload_json),
                 "{\"tier\":\"A\",\"capabilities\":[],\"cursor\":null}");
        break;
    default:
        resp.ok = 0;
        resp.error = NL_CIP_E_BAD_REQ;
        break;
    }
    return nl_cip_render(&resp, out, out_cap);
}

static int serve_connection(int cfd, char *buf)
{
    size_t used = 0;
    for (;;) {
        if (used >= (size_t)NL_BROKER_MAX_LINE) return -1;   /* 超长无换行 */
        ssize_t n = recv(cfd, buf + used, (size_t)NL_BROKER_MAX_LINE - used, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return 0;                                /* 对端关闭 */
        used += (size_t)n;

        size_t start = 0;
        for (size_t i = 0; i < used; i++) {
            if (buf[i] != '\n') continue;
            buf[i] = '\0';
            if (i > start) {
                char out[NL_BROKER_OUT_CAP];
                int rn = nl_server_handle_line(buf + start, out, sizeof(out));
                if (rn > 0) {
                    if (send_all(cfd, out, (size_t)rn) != 0) return -1;
                } else {
                    static const char fallback[] =
                        "{\"v\":1,\"id\":\"-\",\"ok\":false,\"error\":\"E_BAD_REQ\"}\n";
                    if (send_all(cfd, fallback, sizeof(fallback) - 1) != 0) return -1;
                }
            }
            start = i + 1;
        }
        if (start > 0) {
            memmove(buf, buf + start, used - start);
            used -= start;
        }
    }
}

int nl_server_run(const NlServerOpts *opts)
{
    if (!opts || !opts->socket_path) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t plen = strlen(opts->socket_path);
    if (plen == 0 || plen >= sizeof(addr.sun_path)) return -1;
    memcpy(addr.sun_path, opts->socket_path, plen + 1);

    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) return -1;

    unlink(opts->socket_path);                 /* 清理上次异常退出的残留 */
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(lfd);
        return -1;
    }
    if (chmod(opts->socket_path, 0600) != 0) {   /* AC: socket 权限 0600 */
        close(lfd);
        unlink(opts->socket_path);
        return -1;
    }
    if (listen(lfd, NL_BROKER_BACKLOG) != 0) {
        close(lfd);
        unlink(opts->socket_path);
        return -1;
    }

    char *buf = malloc(NL_BROKER_MAX_LINE);
    if (!buf) {
        close(lfd);
        unlink(opts->socket_path);
        return -1;
    }

    while (!g_stop) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        (void)serve_connection(cfd, buf);
        close(cfd);
    }

    free(buf);
    close(lfd);
    unlink(opts->socket_path);
    return 0;
}
