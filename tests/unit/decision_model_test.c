/*
 * decision_model 测试（T11b/T13）
 *
 * 1) 档位 → 后端映射（A1 不上 decoder）
 * 2) Choice prompt 必须列出全部候选（模型看不见的能力不会出现在输出里）
 * 3) 解码结果解析：引号/空白/refuse/越界/乱码
 * 4) 有模型时分档评测：粗筛命中率、漏救率、Choice 准确率、P50/P95 延迟
 *    无模型时整段 SKIP（不阻塞 CI）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define TEST(n) do { tests_run++; printf("  TEST %s ... ", n); fflush(stdout); } while (0)
#define PASS()  do { tests_passed++; printf("PASS\n"); } while (0)
#define SKIP(m) do { tests_passed++; printf("SKIP (%s)\n", m); } while (0)
#define FAIL(m) do { tests_failed++; printf("FAIL: %s\n", m); return 1; } while (0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while (0)

#include "cJSON.h"
#include "capability_registry.h"
#include "decision.h"
#include "decision_model.h"
#include "model_loader.h"

#ifndef BENCHMARK_PATH
#define BENCHMARK_PATH "models/benchmarks/nl-commands-zh-v2.json"
#endif

#define MB ((size_t)1024 * 1024)

/* ── 意图 → 期望能力（只收"单一能力可判定"的意图） ── */
typedef struct { const char *intent; const char *cap; } IntentCap;

static const IntentCap INTENT_CAP[] = {
    { "create_dir",     "cli:mkdir" },
    { "create_file",    "cli:touch" },
    { "delete",         "cli:rm"    },
    { "list_dir",       "cli:ls"    },
    { "view_file",      "cli:cat"   },
    { "find_files",     "cli:find"  },
    { "move",           "cli:mv"    },
    { "copy",           "cli:cp"    },
    { "search_content", "cli:grep"  },
    { "show_process",   "cli:ps"    },
    { NULL, NULL }
};

static const char *cap_for_intent(const char *intent)
{
    for (int i = 0; INTENT_CAP[i].intent; i++)
        if (strcmp(INTENT_CAP[i].intent, intent) == 0) return INTENT_CAP[i].cap;
    return NULL;
}

static char *read_file(const char *path, long *len)
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
    if (len) *len = (long)got;
    fclose(f);
    return buf;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/* 合成注册表：不依赖本机装了哪些命令，保证 CI 可复现 */
static NlCapRegistry *synthetic_registry(void)
{
    NlCapRegistry *r = nl_cap_create();
    if (!r) return NULL;
    for (int i = 0; INTENT_CAP[i].intent; i++) {
        NlCapability c;
        memset(&c, 0, sizeof(c));
        snprintf(c.id, sizeof(c.id), "%s", INTENT_CAP[i].cap);
        const char *colon = strchr(c.id, ':');
        snprintf(c.name, sizeof(c.name), "%s", colon ? colon + 1 : c.id);
        c.kind = NL_CAP_KIND_FILE;
        c.perm_level = 2;
        c.trusted = 1;
        c.effect = NL_CAP_EFFECT_WRITE;
        snprintf(c.namespace, sizeof(c.namespace), "cli");
        if (nl_cap_add(r, &c) != 0) { nl_cap_free(r); return NULL; }
    }
    return r;
}

/* 候选集在注册表之外也要能被构造（解析用例用） */
static void candidates_of(NlCandidates *out, const char **ids, int n)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < n && i < NL_DECIDE_K; i++) {
        snprintf(out->items[i].id, NL_CAP_MAX_ID_LEN, "%s", ids[i]);
        out->items[i].score = 100 - i;
        out->count = i + 1;
    }
    out->total_scanned = n;
}

int main(void)
{
    printf("decision_model tests\n");

    /* ── 1. 档位 / 后端 ── */
    TEST("tier and backend mapping follows ADR-015");
    ASSERT(nl_backend_for_tier(nl_tier_from_bytes((size_t)512 * MB)) == NL_BACKEND_EMBEDDING,
           "A1 (<1GB) must not run a decoder");
    ASSERT(nl_backend_for_tier(nl_tier_from_bytes((size_t)2 * 1024 * MB)) == NL_BACKEND_LLM,
           "A2 must allow decoder");
    ASSERT(nl_backend_for_tier(nl_tier_from_bytes((size_t)12 * 1024 * MB)) == NL_BACKEND_LLM,
           "B must allow decoder");
    ASSERT(nl_backend_for_tier(nl_tier_from_bytes((size_t)64 * 1024 * MB)) == NL_BACKEND_LLM,
           "C must allow decoder");
    ASSERT(nl_backend_for_tier(nl_tier_from_bytes(0)) == NL_BACKEND_EMBEDDING,
           "unknown RAM must degrade conservatively");
    ASSERT(nl_backend_for_tier(99) == NL_BACKEND_RULE,
           "unknown tier must degrade to rules");
    PASS();

    /* ── 2. prompt 必须列出全部候选 ── */
    TEST("choice prompt lists every candidate id");
    const char *ids[] = { "cli:ls", "cli:cat", "cli:mkdir" };
    NlCandidates cands;
    candidates_of(&cands, ids, 3);
    char prompt[2048];
    int n = nl_choose_user_prompt("列出当前目录", &cands, prompt, sizeof(prompt));
    ASSERT(n > 0, "prompt build failed");
    ASSERT(strstr(prompt, "cli:ls") && strstr(prompt, "cli:cat") &&
           strstr(prompt, "cli:mkdir"), "prompt dropped a candidate");
    ASSERT(strstr(prompt, "列出当前目录") != NULL, "prompt dropped the input");
    ASSERT(strstr(prompt, "输出：") != NULL, "prompt missing output anchor");
    /* 缓冲区不足必须失败，不能返回半截 prompt */
    char tiny[16];
    ASSERT(nl_choose_user_prompt("列出当前目录", &cands, tiny, sizeof(tiny)) < 0,
           "truncated prompt must fail");
    PASS();

    /* ── 3. 解码结果解析 ── */
    TEST("choice parse accepts only candidates or refuse");
    ASSERT(nl_choose_parse("\"cli:ls\"", &cands) == 0, "quoted literal");
    ASSERT(nl_choose_parse("cli:cat", &cands) == 1, "bare id");
    ASSERT(nl_choose_parse("  \"cli:mkdir\"\n", &cands) == 2, "whitespace");
    ASSERT(nl_choose_parse("\"<refuse>\"", &cands) == -2, "refuse quoted");
    ASSERT(nl_choose_parse("<refuse>", &cands) == -2, "refuse bare");
    ASSERT(nl_choose_parse("\"cli:rm\"", &cands) == -1, "id outside candidates must be rejected");
    ASSERT(nl_choose_parse("\"\"", &cands) == -1, "empty literal");
    ASSERT(nl_choose_parse("cli:ls because I said so", &cands) == -1, "trailing noise");
    ASSERT(nl_choose_parse(NULL, &cands) == -1, "NULL raw");
    PASS();

    /* ── 4. 有模型则评测 ── */
    TEST("candidate funnel and (optionally) constrained choice");
    {
        const char *model_path = getenv("NL_MODEL_PATH");
        NlModel *m = NULL;
        if (model_path && model_path[0]) m = nl_model_init(model_path);

        long blen = 0;
        char *raw = read_file(BENCHMARK_PATH, &blen);
        ASSERT(raw != NULL, "benchmark file unreadable");
        cJSON *arr = cJSON_Parse(raw);
        free(raw);
        ASSERT(arr != NULL && cJSON_IsArray(arr), "benchmark JSON invalid");

        NlCapRegistry *real = nl_cap_load(nl_cap_default_path(), NULL);
        NlCapRegistry *reg = real ? real : synthetic_registry();
        ASSERT(reg != NULL, "no registry available");

        int total = cJSON_GetArraySize(arr);
        int decidable = 0, hit = 0, miss = 0, rule_ok = 0;
        int model_calls = 0, model_ok = 0, model_refuse = 0, model_err = 0;
        int outside = 0, cand_sum = 0;
        double lat[512];
        int nlat = 0;

        for (int i = 0; i < total; i++) {
            cJSON *e = cJSON_GetArrayItem(arr, i);
            const char *inv = cJSON_GetStringValue(cJSON_GetObjectItem(e, "input"));
            const char *want = cJSON_GetStringValue(cJSON_GetObjectItem(e, "intent"));
            if (!inv || !want) continue;
            const char *want_cap = cap_for_intent(want);
            if (!want_cap) continue;   /* change_dir / run_cmd / other 无法用单一能力判定 */
            decidable++;

            NlCandidates c;
            ASSERT(nl_decide_candidates(inv, reg, &c) == 0, "candidate narrowing failed");
            cand_sum += c.count;

            int want_idx = -1;
            for (int k = 0; k < c.count; k++)
                if (strcmp(c.items[k].id, want_cap) == 0) want_idx = k;
            if (want_idx < 0) { miss++; continue; }   /* 漏救：正确能力没进 top-K */
            hit++;

            if (c.count > 0 && strcmp(c.items[0].id, want_cap) == 0) rule_ok++;

            if (!m) continue;

            double t0 = now_ms();
            char chosen[NL_CAP_MAX_ID_LEN];
            NlChoiceStatus st = nl_choose_with_model(m, inv, &c, chosen, sizeof(chosen));
            double dt = now_ms() - t0;
            model_calls++;
            if (nlat < (int)(sizeof(lat) / sizeof(lat[0]))) lat[nlat++] = dt;

            if (st == NL_CHOICE_OK) {
                int in = 0;
                for (int k = 0; k < c.count; k++)
                    if (strcmp(c.items[k].id, chosen) == 0) in = 1;
                if (!in) outside++;      /* 安全性质：输出必须是候选之一 */
                if (strcmp(chosen, want_cap) == 0) model_ok++;
            } else if (st == NL_CHOICE_REFUSE) {
                model_refuse++;
            } else {
                model_err++;
            }
        }

        cJSON_Delete(arr);
        if (real) nl_cap_free(real); else nl_cap_free(reg);

        double p50 = 0.0, p95 = 0.0;
        if (nlat > 0) {
            qsort(lat, (size_t)nlat, sizeof(double), cmp_double);
            p50 = lat[nlat / 2];
            p95 = lat[(int)(0.95 * (double)(nlat - 1))];
        }

        printf("\n  [可判定 %d/%d · 粗筛命中 %d · 漏救 %d (%.1f%%) · 平均候选 %.2f"
               " · rule top-1 %d/%d = %.1f%%",
               decidable, total, hit, miss,
               decidable ? 100.0 * miss / decidable : 0.0,
               decidable ? (double)cand_sum / decidable : 0.0,
               rule_ok, hit, hit ? 100.0 * rule_ok / hit : 0.0);
        if (model_calls > 0)
            printf(" · choice %d/%d = %.1f%% (refuse %d err %d) · P50 %.0fms P95 %.0fms",
                   model_ok, model_calls, 100.0 * model_ok / model_calls,
                   model_refuse, model_err, p50, p95);
        printf("]\n ");

        /* 门槛：召回与规则 top-1 都要有底线，防止悄悄退化 */
        ASSERT(decidable >= 90, "decidable set too small");
        ASSERT(hit * 100 >= decidable * 80, "coarse-screen recall below 80%");
        ASSERT(rule_ok * 100 >= hit * 85, "rule top-1 below 85% on hit set");
        if (model_calls > 0) {
            ASSERT(outside == 0, "choice returned an id outside the candidate set");
            ASSERT(model_err * 10 <= model_calls, "too many decode failures");
            ASSERT(model_ok * 100 >= model_calls * 20, "accuracy below sanity threshold");
        }
        if (m) nl_model_free(m);
        PASS();
    }

    printf("\n%d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
