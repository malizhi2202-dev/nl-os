/*
 * cli 适配器单元测试（T06）
 * 覆盖：正常执行 / 超时 kill / 非法 plan / ';' 不被解释 / 退出码透传
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "../../src/nlbroker/adapter_cli.c"

static void make_cap(NlCapability *c, const char *id, const char *path)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->id, sizeof(c->id), "%s", id);
    c->path = (char *)path;
    c->source = (char *)"scan";
    c->trusted = 1;
    c->perm_level = 2;
    c->effect = NL_CAP_EFFECT_WRITE;
}

static void cleanup_dir(const char *p) { rmdir(p); }

static void test_exec_ok(void)
{
    TEST("mkdir executes and creates dir");
    NlCapability c;
    NlPlan plan;
    NlCliResult r;
    const char *dir = "/tmp/nlcli-t06-a";
    cleanup_dir(dir);
    make_cap(&c, "cli:mkdir", "/bin/mkdir");
    ASSERT(nl_cli_build_plan(&c, "{\"paths\":[\"/tmp/nlcli-t06-a\"]}", &plan) == 0, "plan failed");
    ASSERT(nl_cli_execute(&plan, 3000, &r) == 0, "exec failed");
    ASSERT(r.status == NL_CLI_OK, "status not ok");
    ASSERT(r.exit_code == 0, "exit code not 0");
    struct stat st;
    ASSERT(stat(dir, &st) == 0, "dir not created");
    nl_cli_plan_free(&plan);
    cleanup_dir(dir);
    PASS();
}

static void test_timeout(void)
{
    TEST("timeout kills child");
    NlCapability c;
    NlPlan plan;
    NlCliResult r;
    make_cap(&c, "cli:sleep", "/bin/sleep");
    ASSERT(nl_cli_build_plan(&c, "{\"args\":[\"5\"]}", &plan) == 0, "plan failed");
    ASSERT(nl_cli_execute(&plan, 200, &r) == 0, "exec call failed");
    ASSERT(r.status == NL_CLI_E_TIMEOUT, "must time out");
    nl_cli_plan_free(&plan);
    PASS();
}

static void test_bad_plan(void)
{
    TEST("non-cli capability rejected");
    NlCapability c;
    NlPlan plan;
    make_cap(&c, "http:GET", "/bin/true");
    ASSERT(nl_cli_build_plan(&c, "{}", &plan) != 0, "must reject non-cli id");
    PASS();
}

static void test_no_shell_interpretation(void)
{
    TEST("';' stays literal (no shell)");
    NlCapability c;
    NlPlan plan;
    NlCliResult r;
    const char *weird = "/tmp/nlcli-t06-b;rm -rf /tmp/nlcli-t06-victim";
    const char *victim = "/tmp/nlcli-t06-victim";
    rmdir(weird);
    rmdir(victim);
    make_cap(&c, "cli:mkdir", "/bin/mkdir");
    ASSERT(nl_cli_build_plan(&c, "{\"paths\":[\"/tmp/nlcli-t06-b;rm -rf /tmp/nlcli-t06-victim\"]}",
                             &plan) == 0, "plan failed");
    ASSERT(plan.argv[1] != NULL && strcmp(plan.argv[1], weird) == 0, "arg must stay literal");
    ASSERT(nl_cli_execute(&plan, 3000, &r) == 0, "exec failed");
    struct stat st;
    ASSERT(stat(victim, &st) != 0, "victim must not exist (no shell)");
    nl_cli_plan_free(&plan);
    rmdir(weird);
    PASS();
}

static void test_exit_code_passthrough(void)
{
    TEST("exit code passthrough");
    NlCapability c;
    NlPlan plan;
    NlCliResult r;
    make_cap(&c, "cli:false", "/bin/false");
    ASSERT(nl_cli_build_plan(&c, "{}", &plan) == 0, "plan failed");
    ASSERT(nl_cli_execute(&plan, 3000, &r) == 0, "exec failed");
    ASSERT(r.exit_code == 1, "exit code must be 1");
    nl_cli_plan_free(&plan);
    PASS();
}

int main(void)
{
    printf("adapter_cli tests\n");
    test_exec_ok();
    test_timeout();
    test_bad_plan();
    test_no_shell_interpretation();
    test_exit_code_passthrough();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
