/*
 * serve.c — 常驻解析守护进程（`nlsh --serve`）
 *
 * 单线程、一条请求一条响应（行分隔 JSON）。模型只在**首次请求**时加载，
 * 空闲 idle 秒后 nl_free() 归还内存；再空闲 max_idle 秒后进程退出。
 * 任何异常都不能让用户命令失败：解析失败就回一个 ok:false，客户端自行退回本地。
 */

#include "serve.h"
#include "nlc_api.h"
#include "parse_ipc.h"
#include "json_helper.h"
#include "model_loader.h"
#include "capability_registry.h"
#include "capability_scan.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define REQ_MAX (16 * 1024)

static volatile sig_atomic_t g_stop = 0;
static void on_stop(int sig) { (void)sig; g_stop = 1; }

static void send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n <= 0) break;
        off += (size_t)n;
    }
}

/* 一条请求：{"v":1,"input":"..."} → 一行 JSON 响应 */
static char *error_response(const char *msg)
{
    cJSON *o = nl_json_new_object();
    nl_json_add_bool(o, "ok", 0);
    nl_json_add_string(o, "error", msg);
    char *resp = nl_json_print(o);
    nl_json_free_obj(o);
    return resp;
}

static void handle_client(int cfd)
{
    char req[REQ_MAX];
    size_t got = 0;
    while (got + 1 < sizeof(req)) {
        ssize_t n = read(cfd, req + got, sizeof(req) - 1 - got);
        if (n <= 0) break;
        got += (size_t)n;
        if (memchr(req, '\n', got)) break;
    }
    req[got] = '\0';

    char *resp = NULL;
    NlJsonDoc doc;
    int parsed = 0;
    const char *input = NULL;

    if (got > 0 && nl_json_parse(req, &doc) == 0) {
        parsed = 1;
        if (nl_json_get_string(doc.root, "input", &input) != 0) input = NULL;
    }

    if (!parsed) {
        resp = error_response("请求不是合法 JSON");
    } else if (!input || !*input) {
        resp = error_response("缺少 input");
    } else {
        NlParseResult r;
        int rc = nl_parse(input, &r);
        if (rc == 0 && r.command) resp = nl_parse_result_to_json(&r);
        else                     resp = error_response("无法解析");
        nl_parse_result_free(&r);
    }

    if (parsed) nl_json_free(&doc);

    if (resp) {
        send_all(cfd, resp, strlen(resp));
        send_all(cfd, "\n", 1);
        free(resp);
    }
    close(cfd);
}

int nl_serve_run(const char *socket_path, int idle_secs, int max_idle_secs)
{
    if (!socket_path || !*socket_path) return -1;

    /* 父目录 0700（socket 里会传路径，不能放公共可写目录里裸露） */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", socket_path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;
    }

    /* 注册表：解析的第三层（受约束选择）需要它。守护进程里也装一份，
     * 否则常驻路径永远拿不到"候选收缩 + 受约束选择"（ADR-024）。 */
    {
        NlCapRegistry *reg = nl_cap_load(nl_cap_default_path(), NULL);
        if (reg) {
            if (nl_cap_count(reg) == 0) {
                int ndirs = 0;
                const char *const *dirs = nl_cap_default_dirs(&ndirs);
                if (nl_cap_scan_dirs(dirs, ndirs, reg) > 0)
                    (void)nl_cap_save_atomic(reg, nl_cap_default_path());
            }
            nl_set_registry(reg);       /* 借用：进程生命周期内常驻 */
        }
    }

    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) return -1;

    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof(a.sun_path), "%s", socket_path);

    if (bind(sfd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        if (errno == EADDRINUSE) {
            /* 已有守护进程在跑：本进程安静退出 */
            int probe = socket(AF_UNIX, SOCK_STREAM, 0);
            if (probe >= 0 && connect(probe, (struct sockaddr *)&a, sizeof(a)) == 0) {
                close(probe);
                close(sfd);
                return 0;
            }
            if (probe >= 0) close(probe);
            unlink(socket_path);            /* 陈旧 socket 文件 */
            if (bind(sfd, (struct sockaddr *)&a, sizeof(a)) != 0) {
                close(sfd);
                return -1;
            }
        } else {
            close(sfd);
            return -1;
        }
    }
    chmod(socket_path, 0600);
    if (listen(sfd, 16) != 0) { close(sfd); return -1; }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* 惰性：这里只记路径，第一条规则未命中的请求才真加载模型 */
    const char *mp = nl_model_default_path();
    if (mp) nl_init_lazy(mp);

    time_t last_active = time(NULL);
    time_t model_unloaded_at = 0;

    while (!g_stop) {
        struct pollfd p = { .fd = sfd, .events = POLLIN, .revents = 0 };
        int rc = poll(&p, 1, 1000);
        if (rc < 0 && errno == EINTR) continue;
        if (rc > 0 && (p.revents & POLLIN)) {
            int cfd = accept(sfd, NULL, NULL);
            if (cfd >= 0) {
                /* 客户端半死不能拖住整个守护进程（单线程） */
                struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
                setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                handle_client(cfd);
                last_active = time(NULL);
                model_unloaded_at = 0;
            }
            continue;
        }

        time_t now = time(NULL);
        time_t idle = now - last_active;
        if (idle_secs > 0 && model_unloaded_at == 0 && idle >= idle_secs &&
            nl_model_is_loaded()) {
            nl_free();                       /* 归还 ~677MB */
            model_unloaded_at = now;
        }
        if (max_idle_secs > 0 && idle >= max_idle_secs) break;
    }

    close(sfd);
    unlink(socket_path);
    nl_free();
    return 0;
}
