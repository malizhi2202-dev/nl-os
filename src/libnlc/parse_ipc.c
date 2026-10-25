/*
 * parse_ipc.c — 常驻解析守护进程的客户端与结果编解码
 *
 * 动机：模型冷启动一次要 ~1.3s CPU / 677MB。交互式终端里每条"规则未命中"的句子都要付一次。
 * 做法（ADR-021）：`nlsh --serve` 常驻，模型只在首次请求时加载，空闲 N 秒后卸载释放内存；
 * 客户端先跑本地规则（免费），规则未命中才问守护进程；连不上就自动拉起，拉不起就退回
 * 进程内加载（老行为），**任何情况下都不会让命令失败**。
 */

#include "parse_ipc.h"
#include "nlc_api.h"
#include "json_helper.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define NL_IPC_MAX_REQ (16 * 1024)
#define NL_IPC_MAX_RESP (64 * 1024)
#define NL_IPC_CONNECT_TIMEOUT_MS 5000
#define NL_IPC_READ_TIMEOUT_SEC 120   /* 冷启动加载 + 慢机器上的解码；比单次推理还短就等于白扔 */

int nl_inferd_socket_path(char *buf, size_t sz)
{
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    int n;
    if (xdg && *xdg) n = snprintf(buf, sz, "%s/nl-os/parse.sock", xdg);
    else             n = snprintf(buf, sz, "/tmp/nl-os-%u/parse.sock", (unsigned)getuid());
    return (n > 0 && (size_t)n < sz) ? 0 : -1;
}

/* 建父目录（0700）——客户端与守护进程共用 */
static int ensure_dir(const char *sock)
{
    char dir[512];
    size_t n = strlen(sock);
    if (n >= sizeof(dir)) return -1;
    memcpy(dir, sock, n + 1);
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir) return -1;
    *slash = '\0';
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* ============================================================
 * 结果编解码
 * ============================================================ */

char *nl_parse_result_to_json(const NlParseResult *r)
{
    if (!r) return NULL;
    cJSON *o = nl_json_new_object();
    if (!o) return NULL;
    nl_json_add_bool(o, "ok", r->command ? 1 : 0);
    nl_json_add_int(o, "intent", (int)r->intent);
    if (r->command) nl_json_add_string(o, "command", r->command);
    nl_json_add_int(o, "perm", (int)r->perm_level);
    nl_json_add_bool(o, "confirm", r->needs_confirm);
    nl_json_add_int(o, "source", (int)r->source);
    nl_json_add_float(o, "conf", r->confidence);
    nl_json_add_string(o, "debug", r->debug_info);

    cJSON *paths = nl_json_new_array();
    for (int i = 0; i < r->path_count; i++)
        nl_json_array_push_string(paths, r->normalized_paths[i]);
    nl_json_add_item(o, "paths", paths);

    cJSON *slots = nl_json_new_array();
    for (int i = 0; i < r->slot_count; i++) {
        cJSON *s = nl_json_new_object();
        nl_json_add_string(s, "n", r->slots[i].name ? r->slots[i].name : "");
        nl_json_add_string(s, "v", r->slots[i].value ? r->slots[i].value : "");
        nl_json_array_push_item(slots, s);
    }
    nl_json_add_item(o, "slots", slots);

    char *out = nl_json_print(o);
    nl_json_free_obj(o);            /* print 只产出字符串，不释放对象 */
    return out;
}

int nl_parse_result_from_json(const char *json, NlParseResult *r, char *err, size_t errsz)
{
    if (!json || !r) return -1;
    if (err && errsz) err[0] = '\0';
    memset(r, 0, sizeof(*r));

    NlJsonDoc doc;
    if (nl_json_parse(json, &doc) != 0) {
        if (err && errsz) snprintf(err, errsz, "守护进程响应不是合法 JSON：%s", doc.error_msg);
        return -1;
    }

    int ok = 0, v = 0;
    nl_json_get_bool(doc.root, "ok", &ok);
    if (!ok) {
        const char *e = NULL;
        nl_json_get_string(doc.root, "error", &e);
        if (err && errsz) snprintf(err, errsz, "%s", e ? e : "守护进程未能解析");
        nl_json_free(&doc);
        return -1;
    }

    if (nl_json_get_int(doc.root, "intent", &v) == 0) r->intent = (NlIntentType)v;
    if (nl_json_get_int(doc.root, "perm", &v) == 0) r->perm_level = (NlPermLevel)v;
    if (nl_json_get_int(doc.root, "source", &v) == 0) r->source = (NlSource)v;
    if (nl_json_get_bool(doc.root, "confirm", &v) == 0) r->needs_confirm = v;
    double d = 0;
    if (nl_json_get_float(doc.root, "conf", &d) == 0) r->confidence = (float)d;

    const char *s = NULL;
    if (nl_json_get_string(doc.root, "command", &s) == 0 && s) r->command = strdup(s);
    if (nl_json_get_string(doc.root, "debug", &s) == 0 && s)
        snprintf(r->debug_info, sizeof(r->debug_info), "%s", s);

    const cJSON *paths = nl_json_get_item(doc.root, "paths");
    int np = nl_json_array_size(paths);
    for (int i = 0; i < np && r->path_count < NL_MAX_PATHS; i++) {
        const char *sv = nl_json_string_value(nl_json_array_get(paths, i));
        if (sv) r->normalized_paths[r->path_count++] = strdup(sv);
    }

    const cJSON *slots = nl_json_get_item(doc.root, "slots");
    int ns = nl_json_array_size(slots);
    for (int i = 0; i < ns && r->slot_count < NL_MAX_SLOTS; i++) {
        const cJSON *e = nl_json_array_get(slots, i);
        const char *n = NULL, *v2 = NULL;
        if (!e) continue;
        nl_json_get_string(e, "n", &n);
        nl_json_get_string(e, "v", &v2);
        r->slots[r->slot_count].name = strdup(n ? n : "");
        r->slots[r->slot_count].value = strdup(v2 ? v2 : "");
        r->slot_count++;
    }
    nl_json_free(&doc);

    if (!r->command) {
        if (err && errsz) snprintf(err, errsz, "守护进程未给出命令");
        nl_parse_result_free(r);
        return -1;
    }
    return 0;
}

/* ============================================================
 * 客户端
 * ============================================================ */

static int connect_once(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof(a.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(fd);
        return -1;
    }
    struct timeval tv = { .tv_sec = NL_IPC_READ_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

/*
 * 双 fork 脱离：父进程只等中间进程（它 setsid 后立刻退出），所以毫秒级返回。
 * 真正的守护进程是孙子进程 —— 绝不能等它，否则调用方的终端会一直挂住。
 * 事故：旧实现单 fork + waitpid(守护进程)，终端在 do_wait 里挂了 2 分 21 秒。
 */
int nl_spawn_detached(const char *exe, char *const argv[])
{
    if (!exe || !*exe || !argv) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) {
        waitpid(pid, NULL, 0);       /* 只回收中间进程；它马上退出 */
        return 0;
    }

    /* ── 中间进程 ── */
    setsid();
    pid_t pid2 = fork();
    if (pid2 < 0) _exit(127);
    if (pid2 > 0) _exit(0);          /* 退出 → 上面的 waitpid 立刻返回 */

    /* ── 孙子进程 = 真正的守护进程（非会话首进程，不受终端信号波及）── */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > 2) close(devnull);
    }
    execv(exe, argv);
    _exit(127);
}

/* 拉起守护进程（脱离当前会话，不占用户的终端/管道） */
static void spawn_daemon(const char *path)
{
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return;
    exe[n] = '\0';

    char *argv[] = { (char *)"nlsh", (char *)"--serve", (char *)"--socket",
                     (char *)path, NULL };
    nl_spawn_detached(exe, argv);
}

static int ensure_running(const char *path)
{
    int probe = connect_once(path);             /* 探测：已在跑就不用拉起 */
    if (probe >= 0) { close(probe); return 0; }
    const char *off = getenv("NL_INFERD");
    if (off && strcmp(off, "0") == 0) return -1;
    /* 只在交互式终端自动拉起：脚本/测试/管道调用不该在后台悄悄养一个常驻进程
     * （CI 与回归脚本里每条规则未命中都拉起一个守护进程，既污染环境也干扰计时） */
    if (!isatty(STDIN_FILENO)) return -1;

    spawn_daemon(path);
    for (int i = 0; i < NL_IPC_CONNECT_TIMEOUT_MS / 20; i++) {
        usleep(20 * 1000);
        if (connect_once(path) >= 0) return 0;
    }
    return -1;
}

int nl_parse_remote(const char *input, NlParseResult *r, char *err, size_t errsz)
{
    if (err && errsz) err[0] = '\0';
    if (!input || !r) return -1;

    char path[512];
    if (nl_inferd_socket_path(path, sizeof(path)) != 0) return -1;
    if (ensure_dir(path) != 0) return -1;

    int fd = connect_once(path);
    if (fd < 0) {
        if (ensure_running(path) != 0) {
            if (err && errsz) snprintf(err, errsz, "常驻解析进程不可用");
            return -1;
        }
        fd = connect_once(path);
        if (fd < 0) return -1;
    }

    cJSON *req = nl_json_new_object();
    nl_json_add_int(req, "v", 1);
    nl_json_add_string(req, "input", input);
    char *payload = nl_json_print(req);
    if (!payload) { close(fd); return -1; }

    size_t len = strlen(payload);
    ssize_t w = write(fd, payload, len);
    free(payload);
    if (w != (ssize_t)len) { close(fd); return -1; }

    char *buf = calloc(1, NL_IPC_MAX_RESP);
    if (!buf) { close(fd); return -1; }
    size_t got = 0;
    while (got + 1 < NL_IPC_MAX_RESP) {
        ssize_t n = read(fd, buf + got, NL_IPC_MAX_RESP - 1 - got);
        if (n <= 0) break;
        got += (size_t)n;
        if (memchr(buf, '\n', got)) break;
    }
    buf[got] = '\0';
    close(fd);

    if (got == 0) {
        if (err && errsz) snprintf(err, errsz, "守护进程无响应");
        free(buf);
        return -1;
    }
    int rc = nl_parse_result_from_json(buf, r, err, errsz);
    free(buf);
    return rc;
}
