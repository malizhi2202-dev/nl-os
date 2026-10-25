/*
 * spawn_detached_test.c — 后台进程必须"立刻返回"，不能等它退出
 *
 * 真实事故（本测试就是为了让它不再发生）：
 *   nlsh 拉起常驻解析进程时用的是单 fork + waitpid(子进程)，而子进程 exec 成了
 *   常驻守护进程 —— 于是**用户敲一条没被规则/决策层接住的句子，终端就一直挂在
 *   wait 里**。实测卡在 do_wait 上 2 分 21 秒仍未返回（守护进程活多久就卡多久）。
 *
 * 这个 bug 之所以能活到线上，是因为之前的验证都是"我先手动把 --serve 起好，再测
 * 客户端"——spawn 这条分支从来没被真正走到过。所以这里直接测"返回得多快"。
 */

#include <stdio.h>
#include <time.h>

static int failures = 0;
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

#include "parse_ipc.h"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/* 子进程活 2 秒：双 fork 下父进程应当毫秒级返回；单 fork + waitpid 会整整等 2 秒 */
static void test_returns_immediately(void)
{
    char *argv[] = { (char *)"sleep", (char *)"2", NULL };

    double t0 = now_ms();
    int rc = nl_spawn_detached("/bin/sleep", argv);
    double dt = now_ms() - t0;

    ASSERT(rc == 0, "spawn 应返回 0");
    ASSERT(dt < 500.0,
           "spawn 必须立刻返回：旧实现会在 waitpid 里等满子进程的整个生命周期");
    printf("  spawn 返回耗时 %.1f ms（子进程活 2000 ms）\n", dt);
}

/* 可执行文件不存在时也不能挂住：只应快速返回，错误由子进程自己承担 */
static void test_missing_exec_returns_fast(void)
{
    char *argv[] = { (char *)"nope", NULL };

    double t0 = now_ms();
    int rc = nl_spawn_detached("/nonexistent/nl-no-such-binary", argv);
    double dt = now_ms() - t0;

    ASSERT(rc == 0, "fork 成功即返回 0（子进程 exec 失败是子进程的事）");
    ASSERT(dt < 500.0, "exec 目标不存在时也不能挂住");
}

static void test_bad_args(void)
{
    char *argv[] = { (char *)"sleep", (char *)"1", NULL };
    ASSERT(nl_spawn_detached(NULL, argv) == -1, "exe 为 NULL 应返回 -1");
    ASSERT(nl_spawn_detached("", argv) == -1, "exe 为空串应返回 -1");
    ASSERT(nl_spawn_detached("/bin/true", NULL) == -1, "argv 为 NULL 应返回 -1");
}

int main(void)
{
    test_returns_immediately();
    test_missing_exec_returns_fast();
    test_bad_args();

    if (failures) { fprintf(stderr, "%d 个断言失败\n", failures); return 1; }
    printf("spawn_detached: 全部断言通过（父进程不等守护进程）\n");
    return 0;
}
