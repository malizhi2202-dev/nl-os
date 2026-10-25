#include "dispatch.h"
#include "history.h"
#include "line_edit.h"
#include "nlc_api.h"
#include "capability_registry.h"
#include "capability_scan.h"
#include "audit.h"
#include "snapshot.h"
#include "confirm.h"
#include "decision.h"
#include "decision_model.h"
#include "nl_eval.h"
#include "serve.h"
#include "parse_ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
static volatile int g_running=1;
static void sigint_handler(int sig) { (void)sig; g_running=0; }
static void print_usage(void) {
    printf("nlsh - NL-Shell\n");
    printf("用法: nlsh [--dry-run] [--yes] [--verbose] [--no-model] [--cmdgen|--no-cmdgen] [--registry PATH]\n");
    printf("           [--scan] [--self-test] [--audit] [--undo [N]] [-c cmd]\n");
    printf("           [--decide 自然语言]   只做决策（候选收缩 + 受约束 Choice），不执行\n");
    printf("           [--eval FILE]         跑中文评测集，输出逐条决策 + 汇总\n");
    printf("           [--serve] [--socket P] [--idle N] [--max-idle N]\n");
    printf("                                 常驻解析守护进程（模型只加载一次，空闲卸载）\n");
    printf("环境变量: NL_MODEL_PATH / NL_TIER(1-4，覆盖档位探测)\n");
}
/* 档位探测：NL_TIER 可覆盖（容器/cgroup 会误判，ADR-015 要求允许人工覆盖） */
static int nl_resolve_tier(void) {
    const char *ov = getenv("NL_TIER");
    if (ov && ov[0]) { int t = atoi(ov); if (t >= 1 && t <= 4) return t; }
    return nl_tier_from_bytes(nl_total_ram_bytes());
}

static const char *nl_backend_name(NlBackend b) {
    switch (b) {
    case NL_BACKEND_LLM:       return "decoder";
    case NL_BACKEND_EMBEDDING: return "embedding";
    default:                   return "rules";
    }
}

/* --decide：只决策不执行。候选收缩 → 按档位选后端 → 受约束 Choice（可选） */
static int run_decide(const char *reg_path, const char *input, int no_model) {
    NlCapRegistry *reg = nl_cap_load(reg_path, NULL);
    if (!reg) reg = nl_cap_create();
    if (!reg) { fprintf(stderr, "nlsh: 无法加载注册表\n"); return 1; }
    if (nl_cap_count(reg) == 0) {
        fprintf(stderr, "nlsh: 注册表为空，先跑 nlsh --scan\n");
        nl_cap_free(reg);
        return 1;
    }

    NlCandidates c;
    if (nl_decide_candidates(input, reg, &c) != 0) {
        fprintf(stderr, "nlsh: 候选收缩失败\n");
        nl_cap_free(reg);
        return 1;
    }
    int tier = nl_resolve_tier();
    NlBackend be = nl_backend_for_tier(tier);

    char chosen[NL_CAP_MAX_ID_LEN] = "";
    const char *method = "none";
    const char *status = "refused";

    if (c.count == 0) {
        method = "no-candidate";
    } else if (be == NL_BACKEND_LLM && !no_model) {
        const char *mp = nl_model_default_path();
        if (!mp) mp = "/usr/share/nl-os/model.gguf";
        NlModel *m = nl_model_init(mp);
        if (!m) {
            /* 模型缺席：降档到规则 top-1，不阻塞 */
            snprintf(chosen, sizeof(chosen), "%s", c.items[0].id);
            method = "rule-fallback";
            status = "ok";
        } else {
            NlChoiceStatus st = nl_choose_with_model(m, input, &c, chosen, sizeof(chosen));
            /* 规则已确定时模型根本不解码（confidence 保持 -1）——如实标成 rule-decisive，
             * 否则把"没问模型"说成 choice-llm，会让 --decide 的可解释性失真 */
            double p = nl_model_last_confidence(m);
            nl_model_free(m);
            if (st == NL_CHOICE_OK)       { method = (p >= 0.0) ? "choice-llm" : "rule-decisive"; status = "ok"; }
            else if (st == NL_CHOICE_REFUSE) { method = "model-refuse"; }
            else if (st == NL_CHOICE_UNCERTAIN) { method = "low-confidence"; }
            else {
                /* 解码失败同样降档：宁可走规则，也不猜 */
                snprintf(chosen, sizeof(chosen), "%s", c.items[0].id);
                method = "rule-fallback";
                status = "ok";
            }
        }
    } else {
        snprintf(chosen, sizeof(chosen), "%s", c.items[0].id);
        /* --no-model 时明确走规则，不当成 embedding 档 */
        method = (be == NL_BACKEND_EMBEDDING) ? "rule-narrowed" : "rule";
        status = "ok";
    }

    char cands[1024];
    size_t used = 0;
    cands[0] = '\0';
    for (int i = 0; i < c.count && used + 3 < sizeof(cands); i++) {
        int n = snprintf(cands + used, sizeof(cands) - used, "%s\"%s\"",
                         i ? "," : "", c.items[i].id);
        if (n < 0 || (size_t)n >= sizeof(cands) - used) break;
        used += (size_t)n;
    }

    printf("{\"status\":\"%s\",\"chosen\":\"%s\",\"method\":\"%s\","
           "\"tier\":%d,\"backend\":\"%s\",\"candidates\":[%s]}\n",
           status, chosen, method, tier, nl_backend_name(be), cands);

    NlAuditRec r;
    memset(&r, 0, sizeof(r));
    r.capability_id = chosen[0] ? chosen : NULL;
    r.perm_level = 0;
    r.verdict = "allow";
    r.effect = "read";
    r.fast_path = (method[0] == 'r');   /* rule* 走快路径；choice-llm 不算 */
    r.probability = -1;
    r.duration_us = 0;
    r.result = status;
    r.reason = method;
    r.input = input;
    r.corr = "";
    (void)nl_audit_emit(&r);

    nl_cap_free(reg);
    return strcmp(status, "ok") == 0 ? 0 : 3;
}

int main(int argc, char **argv) {
    int dry_run=0,verbose=0,no_model=0;
    int cmdgen=-1;   /* -1 = 跟随默认（有专用模型就开）；--cmdgen/--no-cmdgen 覆盖 */
    int do_scan=0, do_selftest=0, do_audit=0, do_undo=0;
    int undo_n=1;
    long audit_since = 0;
    int audit_limit = 20;
    const char *registry_path=NULL;
    char *cmd=NULL;
    const char *decide_input=NULL;
    const char *eval_path=NULL;
    int do_serve=0, serve_idle=300, serve_max_idle=3600;
    const char *serve_socket=NULL;
    for(int i=1;i<argc;i++) { if(strcmp(argv[i],"--dry-run")==0)dry_run=1; else if(strcmp(argv[i],"--yes")==0)setenv("NL_ASSUME_YES","1",1); else if(strcmp(argv[i],"--verbose")==0)verbose=1; else if(strcmp(argv[i],"--no-model")==0)no_model=1; else if(strcmp(argv[i],"--cmdgen")==0)cmdgen=1; else if(strcmp(argv[i],"--no-cmdgen")==0)cmdgen=0; else if(strcmp(argv[i],"-c")==0&&i+1<argc)cmd=argv[++i];
        else if(strcmp(argv[i],"--registry")==0&&i+1<argc)registry_path=argv[++i];
        else if(strcmp(argv[i],"--scan")==0)do_scan=1;
        else if(strcmp(argv[i],"--self-test")==0)do_selftest=1;
        else if(strcmp(argv[i],"--audit")==0)do_audit=1;
        else if(strcmp(argv[i],"--undo")==0){
            do_undo=1;
            if(i+1<argc && argv[i+1][0]>='1' && argv[i+1][0]<='9') undo_n=atoi(argv[++i]);
        }
        else if(strcmp(argv[i],"--since")==0&&i+1<argc)audit_since=atol(argv[++i]);
        else if(strcmp(argv[i],"--limit")==0&&i+1<argc)audit_limit=atoi(argv[++i]);
        else if(strcmp(argv[i],"--decide")==0&&i+1<argc)decide_input=argv[++i];
        else if(strcmp(argv[i],"--eval")==0&&i+1<argc)eval_path=argv[++i];
        else if(strcmp(argv[i],"--serve")==0)do_serve=1;
        else if(strcmp(argv[i],"--socket")==0&&i+1<argc)serve_socket=argv[++i];
        else if(strcmp(argv[i],"--idle")==0&&i+1<argc)serve_idle=atoi(argv[++i]);
        else if(strcmp(argv[i],"--max-idle")==0&&i+1<argc)serve_max_idle=atoi(argv[++i]); else if(strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0){print_usage();return 0;} }
    if (do_serve) {
        char sp[512];
        const char *path = serve_socket;
        if (!path) {
            if (nl_inferd_socket_path(sp, sizeof(sp)) != 0) {
                fprintf(stderr, "nlsh: socket 路径过长\n");
                return 1;
            }
            path = sp;
        }
        return nl_serve_run(path, serve_idle, serve_max_idle) == 0 ? 0 : 1;
    }

    signal(SIGINT,sigint_handler);
    nl_history_init(getenv("HOME")?strcat(strcpy(malloc(256),getenv("HOME")),"/.nlsh_history"):NULL);

    const char *reg_path = registry_path ? registry_path : nl_cap_default_path();

    if (do_undo) {
        char err[256] = "";
        if (nl_snapshot_undo(undo_n, err, sizeof(err)) != 0) {
            fprintf(stderr, "nlsh: 撤销失败：%s\n", err[0] ? err : "未知原因");
            return 1;
        }
        printf("✅ 已撤销第 %d 次快照\n", undo_n);
        return 0;
    }

    if (do_audit) {
        static char buf[16384];
        int n = nl_audit_query(audit_since, audit_limit, buf, sizeof(buf));
        if (n < 0) { fprintf(stderr, "nlsh: 审计读取失败\n"); return 1; }
        printf("%s\n", buf);
        return 0;
    }

    if (do_scan || do_selftest) {
        NlCapRegistry *reg = nl_cap_load(reg_path, NULL);
        if (!reg) reg = nl_cap_create();
        if (!reg) { fprintf(stderr, "nlsh: 无法创建注册表\n"); return 1; }

        if (do_scan) {
            int ndirs = 0;
            const char *const *dirs = nl_cap_default_dirs(&ndirs);
            int added = nl_cap_scan_dirs(dirs, ndirs, reg);
            if (added < 0) added = 0;
            if (nl_cap_save_atomic(reg, reg_path) != 0) {
                fprintf(stderr, "nlsh: 注册表写回失败 (%s)\n", reg_path);
                nl_cap_free(reg);
                return 1;
            }
            printf("扫描完成：新增 %d 条，总计 %d 条 → %s\n",
                   added, nl_cap_count(reg), reg_path);
        }
        if (do_selftest) {
            NlSelfTestReport rep;
            if (nl_cap_selftest(reg, &rep) < 0) {
                fprintf(stderr, "nlsh: 自检失败\n");
                nl_cap_free(reg);
                return 1;
            }
            printf("自检完成：usable=%d needs-privilege=%d missing=%d total=%d\n",
                   rep.usable, rep.needs_privilege, rep.missing, rep.total);
        }
        nl_cap_free(reg);
        return 0;
    }


    if (eval_path) return nl_eval_run(reg_path, eval_path);
    if (decide_input) return run_decide(reg_path, decide_input, no_model);

    /* --no-model：既不加载模型，也不去问常驻进程（否则等于偷偷用了模型） */
    nl_dispatch_set_remote(!no_model);
    /* 专用 NL2Bash 小模型（ADR-025）：--no-model 一并关掉（它也是模型）；
     * 否则按显式开关，未指定时"有模型就开"。 */
    nl_dispatch_set_cmdgen(no_model ? 0 : cmdgen);
    nl_dispatch_set_verbose(verbose);
    if(!no_model) {
        int tier = nl_resolve_tier();
        NlBackend be = nl_backend_for_tier(tier);
        if (be == NL_BACKEND_EMBEDDING) {
            fprintf(stderr,"ℹ️ 档位 %d：decoder 未启用（仅规则/向量粗筛）\n", tier);
        } else {
            const char *mp=nl_model_default_path(); if(!mp)mp="/usr/share/nl-os/model.gguf";
            /* 惰性：规则层能决定的句子不加载模型（省 2s CPU / 780MB） */
            if(nl_init_lazy(mp)!=0) fprintf(stderr,"⚠️ 模型加载失败，降档到规则引擎\n");
        }
    }

    /* 传统命令走快路径时根本不需要注册表（只有自然语言路径要查"本机有没有这个能力"）。
     * 满表 1891 条约 356KB，全量解析占掉一次调用 30ms 里的大头——这里省掉它。 */
    NlCapRegistry *reg = NULL;
    if (!(cmd && nl_is_traditional_cmd(cmd))) reg = nl_cap_load(reg_path, NULL);

    /* 首次运行（注册表文件还不存在 → 空表）：扫一次 PATH 并落盘。
     * 没有这一步，决策层在用户机器上等于不存在 —— 它靠注册表找候选，空表就是
     * 零候选，然后所有没写规则的说法一律"无法理解"。ADR-024 记了这个坑：
     * 通用层必须开箱可用，不能依赖用户先想到 `nlsh --scan`。
     * 只在空表时扫（一次约 0.7s），之后都读已落盘的表。 */
    if (reg && nl_cap_count(reg) == 0) {
        int ndirs = 0;
        const char *const *dirs = nl_cap_default_dirs(&ndirs);
        if (nl_cap_scan_dirs(dirs, ndirs, reg) > 0)
            (void)nl_cap_save_atomic(reg, reg_path);
    }
    nl_dispatch_set_registry(reg);
    nl_set_registry(reg);          /* 分层解析的第三层要用它做受约束选择（ADR-024） */

    if(cmd) {
        char out[4096];
        /* dispatch 自己负责打印（执行路径需要与子进程输出交错） */
        int rc = nl_dispatch(cmd,out,sizeof(out),dry_run);
        if (reg) nl_cap_free(reg);
        /* 退出码统一（ADR-019 P-4）：0 成功 / 1 失败或拒绝 / 3 用户取消 */
        return rc == 0 ? 0 : (rc == NL_CONFIRM_EXIT_CANCELLED ? NL_CONFIRM_EXIT_CANCELLED : 1);
    } else {
        printf("NL-Shell v0.1 · 输入自然语言或传统命令 · Ctrl+C 中断 · Ctrl+D 退出\n");
        fflush(stdout);
        while(g_running) {
            char *line=nl_readline("nl> ");
            if(!line) { printf("\n"); break; }
            if(strlen(line)==0) { free(line); continue; }
            nl_history_add(line);
            { char out[4096]; (void)nl_dispatch(line,out,sizeof(out),dry_run); }
            free(line);
        }
    }
    if (reg) nl_cap_free(reg);
    nl_history_save(); nl_free(); return 0;
}
