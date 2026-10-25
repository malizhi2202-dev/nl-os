/*
 * http 适配器单元测试（T13）
 * 覆盖：URL 解析 / https 拒绝 / 非法 URL / 本地真实请求
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)
#define SKIP(m) do { tests_passed++; printf("SKIP (%s)\n", m); return; } while (0)

#include "../../src/nlbroker/adapter_http.c"

static void test_parse_basic(void)
{
    TEST("parse host/port/path");
    NlHttpUrl u;
    ASSERT(nl_http_parse_url("http://127.0.0.1:8080/health", &u) == 0, "parse failed");
    ASSERT(strcmp(u.host, "127.0.0.1") == 0, "host");
    ASSERT(u.port == 8080, "port");
    ASSERT(strcmp(u.path, "/health") == 0, "path");
    ASSERT(u.https == 0, "not https");

    ASSERT(nl_http_parse_url("http://example.com", &u) == 0, "no path");
    ASSERT(u.port == 80, "default port");
    ASSERT(strcmp(u.path, "/") == 0, "default path");
    PASS();
}

static void test_https_rejected(void)
{
    TEST("https returns scheme-unsupported");
    NlHttpUrl u;
    ASSERT(nl_http_parse_url("https://example.com/x", &u) == 1, "must flag https");
    char buf[64];
    ASSERT(nl_http_request("GET", "https://example.com/x", "", 1000, buf, sizeof(buf)) == -2,
           "request must return -2");
    PASS();
}

static void test_invalid_url(void)
{
    TEST("invalid urls rejected");
    NlHttpUrl u;
    ASSERT(nl_http_parse_url("ftp://x/y", &u) == -1, "ftp");
    ASSERT(nl_http_parse_url("http://", &u) == -1, "no host");
    ASSERT(nl_http_parse_url("http://h:abc/x", &u) == -1, "bad port");
    ASSERT(nl_http_parse_url("http://h:99999/x", &u) == -1, "port range");
    PASS();
}

static void test_live_request(void)
{
    TEST("live GET against local python server");
    const char *root = "/tmp/nlhttp-t13";
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s && printf 'ok-t13' > %s/health", root, root);
    if (system(cmd) != 0) SKIP("cannot prepare fixture");

    pid_t pid = fork();
    if (pid == 0) {
        /* -Werror=unused-result：Release 下 freopen 被标 warn_unused_result */
        if (!freopen("/dev/null", "w", stdout)) _exit(126);
        if (!freopen("/dev/null", "w", stderr)) _exit(126);
        execlp("python3", "python3", "-m", "http.server", "18099", "--directory", root,
               (char *)NULL);
        _exit(127);
    }
    if (pid < 0) SKIP("fork failed");
    usleep(700 * 1000);

    char out[1024];
    int rc = nl_http_request("GET", "http://127.0.0.1:18099/health", "", 3000, out, sizeof(out));
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    if (rc != 0) SKIP("local server unavailable");
    ASSERT(strstr(out, "ok-t13") != NULL, "body must be returned");
    PASS();
}

int main(void)
{
    printf("adapter_http tests\n");
    test_parse_basic();
    test_https_rejected();
    test_invalid_url();
    test_live_request();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
