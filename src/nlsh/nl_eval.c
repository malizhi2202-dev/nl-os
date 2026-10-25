/*
 * eval.c — nlsh --eval：决策层中文评测（AC-6 / AC-9 / AC-10）
 *
 * 逐条跑「候选收缩 → 按档位选后端 → 受约束 Choice」，输出：
 *   - 每行一个 JSON：input / capability_id / probability / refused / method
 *   - 末尾汇总：ok / refused / schema_violations / fallbacks / p50 / p95 / intent 准确率
 *
 * 硬约束（AC-10）：capability_id 必须能在注册表里查到；查不到计入
 * schema_violations 并**不执行**。该计数必须为 0。
 */
#include "nl_eval.h"

#include "audit.h"
#include "cJSON.h"
#include "capability_registry.h"
#include "decision.h"
#include "decision_model.h"
#include "model_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 意图 → 期望能力（只收"单一能力可判定"的意图，其余不参与准确率） */
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
    /* 网络状态里只有「网卡/地址」这一半是单能力可判定的；域名解析要域名参数，
     * 判到 cli:dig 也没有意义，所以不参与准确率。 */
    { "show_network",   "cli:ip"    },
    { "show_process",   "cli:ps"    },
    { NULL, NULL }
};

static const char *cap_for_intent(const char *intent)
{
    for (int i = 0; INTENT_CAP[i].intent; i++)
        if (strcmp(intent, INTENT_CAP[i].intent) == 0) return INTENT_CAP[i].cap;
    return NULL;
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
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

/* 评测集的 input 可能含引号/反斜杠：必须转义，否则 --eval 输出不是合法 JSON */
static void json_escape(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 7 < cap; p++) {
        switch (*p) {
        case '"':  out[o++] = '\\'; out[o++] = '"';  break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n';  break;
        case '\r': out[o++] = '\\'; out[o++] = 'r';  break;
        case '\t': out[o++] = '\\'; out[o++] = 't';  break;
        default:
            if (*p < 0x20) { int n = snprintf(out + o, cap - o, "\\u%04x", *p); if (n > 0) o += (size_t)n; }
            else out[o++] = (char)*p;
        }
    }
    out[o] = '\0';
}

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
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

static int default_tier(void)
{
    const char *ov = getenv("NL_TIER");
    if (ov && ov[0]) {
        int t = atoi(ov);
        if (t >= 1 && t <= 4) return t;
    }
    return nl_tier_from_bytes(nl_total_ram_bytes());
}

static const char *backend_name(NlBackend b)
{
    switch (b) {
    case NL_BACKEND_LLM:       return "decoder";
    case NL_BACKEND_EMBEDDING: return "embedding";
    default:                   return "rules";
    }
}

int nl_eval_run(const char *reg_path, const char *bench_path)
{
    if (!reg_path || !bench_path) return 1;

    char *raw = read_all(bench_path);
    if (!raw) {
        fprintf(stderr, "nlsh: 无法读取评测集 %s\n", bench_path);
        return 1;
    }
    cJSON *arr = cJSON_Parse(raw);
    free(raw);
    if (!arr || !cJSON_IsArray(arr)) {
        fprintf(stderr, "nlsh: 评测集不是 JSON 数组\n");
        if (arr) cJSON_Delete(arr);
        return 1;
    }

    NlCapRegistry *reg = nl_cap_load(reg_path, NULL);
    if (!reg) {
        fprintf(stderr, "nlsh: 无法加载注册表 %s（先跑 nlsh --scan）\n", reg_path);
        cJSON_Delete(arr);
        return 1;
    }

    int tier = default_tier();
    NlBackend be = nl_backend_for_tier(tier);
    NlModel *m = NULL;
    if (be == NL_BACKEND_LLM) {
        const char *mp = nl_model_default_path();
        if (!mp) mp = "/usr/share/nl-os/model.gguf";
        m = nl_model_init(mp);
        if (!m) {
            fprintf(stderr, "nlsh: 模型不可用，降档到规则层继续评测\n");
            be = NL_BACKEND_RULE;
        }
    }

    int total = cJSON_GetArraySize(arr);
    int ok = 0, refused = 0, violations = 0, fallbacks = 0, errors = 0;
    int labelled = 0, intent_ok = 0;
    double lat[4096];
    int nlat = 0;

    printf("{\"eval\":\"%s\",\"tier\":%d,\"backend\":\"%s\",\"model\":\"%s\"}\n",
           bench_path, tier, backend_name(be), m ? "loaded" : "none");

    for (int i = 0; i < total; i++) {
        cJSON *e = cJSON_GetArrayItem(arr, i);
        const char *input = cJSON_GetStringValue(cJSON_GetObjectItem(e, "input"));
        const char *intent = cJSON_GetStringValue(cJSON_GetObjectItem(e, "intent"));
        if (!input) continue;

        NlCandidates c;
        if (nl_decide_candidates(input, reg, &c) != 0) { errors++; continue; }

        char chosen[NL_CAP_MAX_ID_LEN] = "";
        const char *method = "none";
        char top3[256] = "";
        const char *status = "refused";
        double prob = -1.0;
        double t0 = now_ms();

        if (c.count == 0) {
            method = "no-candidate";
        } else if (be == NL_BACKEND_LLM && m) {
            NlChoiceStatus st = nl_choose_with_model(m, input, &c, chosen, sizeof(chosen));
            if (st == NL_CHOICE_OK) {
                method = "choice-llm";
                status = "ok";
                prob = nl_model_last_confidence(m);
            } else if (st == NL_CHOICE_REFUSE) {
                method = "model-refuse";
            } else if (st == NL_CHOICE_UNCERTAIN) {
                /* AC-8：低置信度不执行，只留候选 */
                method = "low-confidence";
                prob = nl_model_last_confidence(m);
            } else {
                snprintf(chosen, sizeof(chosen), "%s", c.items[0].id);
                method = "rule-fallback";
                status = "ok";
                fallbacks++;
            }
        } else {
            snprintf(chosen, sizeof(chosen), "%s", c.items[0].id);
            method = (be == NL_BACKEND_EMBEDDING) ? "rule-narrowed" : "rule";
            status = "ok";
            fallbacks++;
        }
        double dt = now_ms() - t0;
        if (nlat < (int)(sizeof(lat) / sizeof(lat[0]))) lat[nlat++] = dt;

        /* AC-10：能力 id 必须在注册表内；不在即 schema violation，且绝不放行 */
        const NlCapability *cap = chosen[0] ? nl_cap_find(reg, chosen) : NULL;
        if (chosen[0] && !cap) {
            violations++;
            status = "invalid";
            method = "schema-violation";
            chosen[0] = '\0';
        }

        if (strcmp(status, "ok") == 0) ok++;
        else if (strcmp(status, "refused") == 0) refused++;

        const char *want_cap = intent ? cap_for_intent(intent) : NULL;
        if (want_cap && chosen[0]) {
            labelled++;
            if (strcmp(chosen, want_cap) == 0) intent_ok++;
        }

        const char *sep = "";
        for (int k = 0; k < 3 && k < c.count; k++) {
            int n = snprintf(top3 + strlen(top3), sizeof(top3) - strlen(top3),
                             "%s\"%s\"", sep, c.items[k].id);
            if (n < 0 || (size_t)n >= sizeof(top3) - strlen(top3)) break;
            sep = ",";
        }

        char input_esc[2048];
        json_escape(input, input_esc, sizeof(input_esc));
        printf("{\"input\":\"%s\",\"capability_id\":\"%s\",\"probability\":%.4f,"
               "\"refused\":%s,\"method\":\"%s\",\"tier\":%d,\"top3\":[%s]}\n",
               input_esc, chosen, prob,
               strcmp(status, "ok") == 0 ? "false" : "true", method, tier, top3);

        NlAuditRec r;
        memset(&r, 0, sizeof(r));
        r.capability_id = chosen[0] ? chosen : NULL;
        r.verdict = "allow";
        r.effect = "read";
        r.fast_path = (method[0] == 'r');
        r.probability = prob;
        r.duration_us = (long)(dt * 1000.0);
        r.result = status;
        r.reason = method;
        r.input = input;
        r.corr = "";
        (void)nl_audit_emit(&r);
    }

    double p50 = 0.0, p95 = 0.0;
    if (nlat > 0) {
        qsort(lat, (size_t)nlat, sizeof(double), cmp_double);
        p50 = lat[nlat / 2];
        p95 = lat[(int)(0.95 * (double)(nlat - 1))];
    }

    printf("{\"summary\":{\"total\":%d,\"ok\":%d,\"refused\":%d,"
           "\"schema_violations\":%d,\"fallbacks\":%d,\"errors\":%d,"
           "\"intent_checked\":%d,\"intent_ok\":%d,\"accuracy\":%.1f,"
           "\"p50_ms\":%.1f,\"p95_ms\":%.1f,\"tier\":%d,\"backend\":\"%s\"}}\n",
           total, ok, refused, violations, fallbacks, errors,
           labelled, intent_ok,
           labelled ? 100.0 * intent_ok / labelled : 0.0,
           p50, p95, tier, backend_name(be));

    if (m) nl_model_free(m);
    nl_cap_free(reg);
    cJSON_Delete(arr);
    return violations == 0 ? 0 : 1;
}
