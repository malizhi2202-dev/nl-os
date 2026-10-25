/*
 * rule_slot_test.c — 槽位抽取回归（`extract_after_key_long` 的守卫）
 *
 * 背景：槽位曾经把句尾修饰词一起吃进去（`创建一个叫 demo 的文件夹` → `demo 的文件夹`），
 * 生成 `mkdir -p demo 的文件夹` 这种必然失败的命令。这里把"最自然的中文说法 → 期望命令"
 * 固化成断言，任何改动 `extract_after_key_long` 都必须先过这一关。
 *
 * 两个方向都要测：
 *   1. 该剥的剥（的文件夹 / 的目录 / 的文件 / 的内容 / 文件 / 目录 / X 目录下）
 *   2. 不该剥的别剥（`删除 旧文件` 的目标就是 `旧文件`，不能啃成 `旧`）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return 1; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "nlc_api.h"

typedef struct { const char *input; const char *want; } Case;

static int check_cases(const char *title, const Case *cases, int n)
{
    int bad = 0;
    for (int i = 0; i < n; i++) {
        NlParseResult r;
        int rc = nl_parse(cases[i].input, &r);
        const char *got = (rc == 0) ? r.command : NULL;
        if (!got || strcmp(got, cases[i].want) != 0) {
            printf("\n    ✗ [%s]\n      期望: %s\n      实际: %s\n",
                   cases[i].input, cases[i].want, got ? got : "(解析失败)");
            bad++;
        }
        if (rc == 0) nl_parse_result_free(&r);
    }
    if (bad) printf("    (%s: %d/%d 不符)\n ", title, bad, n);
    return bad;
}

int main(void)
{
    printf("rule slot tests\n");

    /* 1. 创建目录：句尾 `的文件夹` / `的目录` / `文件夹` 都要剥掉 */
    TEST("create_dir strips trailing 的文件夹/文件夹");
    {
        static const Case c[] = {
            { "创建一个叫 uat 的文件夹",    "mkdir -p uat" },
            { "新建一个叫 src 的目录",      "mkdir -p src" },
            { "创建个叫 demo 的文件夹",     "mkdir -p demo" },
            { "建立一个叫 data 的目录",     "mkdir -p data" },
            { "建立个叫 backup 的文件夹",   "mkdir -p backup" },
            { "建一个 logs 文件夹",         "mkdir -p logs" },
        };
        ASSERT(check_cases("create_dir", c, (int)(sizeof(c) / sizeof(c[0]))) == 0,
               "create_dir slot extraction");
        PASS();
    }

    /* 2. 创建文件：`叫 X 的文件` → X */
    TEST("create_file strips trailing 的文件/文件");
    {
        static const Case c[] = {
            { "创建一个叫 README.md 的文件", "touch README.md" },
            { "新建一个叫 main.c 的文件",    "touch main.c" },
            { "新建文件叫 config.json",      "touch config.json" },
            { "创建一个叫 .env 的文件",      "touch .env" },
        };
        ASSERT(check_cases("create_file", c, (int)(sizeof(c) / sizeof(c[0]))) == 0,
               "create_file slot extraction");
        PASS();
    }

    /* 3. 查看文件：`X 的内容` / `X 文件` → X。
     *    注意 `旧文件` 这种"名字本身含 文件"不能剥（见第 5 组） */
    TEST("view_file strips trailing 的内容/文件");
    {
        static const Case c[] = {
            { "查看 README.md 的内容",      "cat README.md" },
            { "查看 demo 的内容",           "cat demo" },
            { "查看 README.md 文件",        "cat README.md" },
            { "打印 backup 的内容",         "cat backup" },
            { "读取 data",                  "cat data" },
        };
        ASSERT(check_cases("view_file", c, (int)(sizeof(c) / sizeof(c[0]))) == 0,
               "view_file slot extraction");
        PASS();
    }

    /* 4. 删除 / 切换目录 */
    TEST("delete and change_dir slots");
    {
        static const Case c[] = {
            { "删除 keepme",                "rm -rf keepme" },
            { "删除 temp 目录",             "rm -rf temp" },
            { "删掉 /tmp/uat-demo/x",       "rm -rf /tmp/uat-demo/x" },
            { "进入 /home",                 "cd /home" },
            { "到 logs 目录下",             "cd logs" },
        };
        ASSERT(check_cases("delete/change_dir", c, (int)(sizeof(c) / sizeof(c[0]))) == 0,
               "delete/change_dir slot extraction");
        PASS();
    }

    /* 5. 反向守卫：不能过度剥离 */
    TEST("does not over-strip names that end with 文件/目录");
    {
        static const Case c[] = {
            { "删除 旧文件",                "rm -rf 旧文件" },
            { "删除 my目录",                "rm -rf my目录" },
        };
        ASSERT(check_cases("no-overstrip", c, (int)(sizeof(c) / sizeof(c[0]))) == 0,
               "over-stripping regression");
        PASS();
    }

    /* 6. 英文命令式不受影响 */
    TEST("plain english commands unaffected");
    {
        static const Case c[] = {
            { "mkdir src",                  "mkdir -p src" },
            { "touch main.c",               "touch main.c" },
            { "cat /etc/hosts",             "cat /etc/hosts" },
            { "ls",                         "ls -la" },
        };
        ASSERT(check_cases("english", c, (int)(sizeof(c) / sizeof(c[0]))) == 0,
               "english commands");
        PASS();
    }

    nl_free();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
