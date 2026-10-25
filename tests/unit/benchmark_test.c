/*
 * 中文评测集完整性 + 规则层命中率（T13）
 *
 * 1) 评测集 ≥100 条，intent 合法
 * 2) 用 nl_parse 跑一遍，报告 intent 准确率（粗筛命中率基线）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return 1; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "cJSON.h"
#include "nlc_api.h"

#ifndef BENCHMARK_PATH
#define BENCHMARK_PATH "models/benchmarks/nl-commands-zh-v2.json"
#endif

static const char *VALID_INTENTS[] = {
    "create_file", "create_dir", "delete", "move", "copy", "list_dir",
    "view_file", "find_files", "change_dir", "show_process",
    "search_content", "run_cmd", "other", NULL
};

static int valid_intent(const char *s)
{
    for (int i = 0; VALID_INTENTS[i]; i++)
        if (strcmp(s, VALID_INTENTS[i]) == 0) return 1;
    return 0;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int main(void)
{
    printf("benchmark tests\n");

    char *raw = read_file(BENCHMARK_PATH);
    TEST("benchmark file loads and has >=100 entries");
    if (!raw) FAIL("cannot read benchmark file");
    cJSON *arr = cJSON_Parse(raw);
    free(raw);
    ASSERT(arr != NULL, "invalid JSON");
    ASSERT(cJSON_IsArray(arr), "must be an array");
    int total = cJSON_GetArraySize(arr);
    ASSERT(total >= 100, "must have at least 100 entries");
    PASS();

    TEST("every entry has a valid intent");
    for (int i = 0; i < total; i++) {
        cJSON *e = cJSON_GetArrayItem(arr, i);
        cJSON *in = cJSON_GetObjectItem(e, "input");
        cJSON *it = cJSON_GetObjectItem(e, "intent");
        const char *inv = cJSON_IsString(in) ? cJSON_GetStringValue(in) : NULL;
        const char *itv = cJSON_IsString(it) ? cJSON_GetStringValue(it) : NULL;
        ASSERT(inv && inv[0], "entry without input");
        ASSERT(itv && valid_intent(itv), "invalid intent");
    }
    PASS();

    /* 规则层命中率：这是决策层上线前的基线 */
    int correct = 0, parsed = 0;
    for (int i = 0; i < total; i++) {
        cJSON *e = cJSON_GetArrayItem(arr, i);
        const char *inv = cJSON_GetStringValue(cJSON_GetObjectItem(e, "input"));
        const char *want = cJSON_GetStringValue(cJSON_GetObjectItem(e, "intent"));
        NlParseResult r;
        if (nl_parse(inv, &r) == 0 && r.command) {
            parsed++;
            const char *got = nl_intent_name(r.intent);
            if (got && strcmp(got, want) == 0) correct++;
            nl_parse_result_free(&r);
        }
    }
    cJSON_Delete(arr);

    TEST("rule-layer baseline recorded (accuracy >= 30%)");
    printf("[%d/%d 解析出命令，intent 正确 %d/%d = %.1f%%] ",
           parsed, total, correct, total, total ? 100.0 * correct / total : 0.0);
    ASSERT(total >= 100, "entry count");
    ASSERT(correct * 100 >= total * 30, "accuracy below sanity threshold");
    PASS();

    nl_free();
    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
