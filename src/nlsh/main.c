#include "dispatch.h"
#include "history.h"
#include "line_edit.h"
#include "nlc_api.h"
#include "capability_registry.h"
#include "capability_scan.h"
#include "audit.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
static volatile int g_running=1;
static void sigint_handler(int sig) { (void)sig; g_running=0; }
static void print_usage(void) {
    printf("nlsh - NL-Shell\n");
    printf("用法: nlsh [--dry-run] [--verbose] [--no-model] [--registry PATH]\n");
    printf("           [--scan] [--self-test] [--audit] [--undo [N]] [-c cmd]\n");
}
int main(int argc, char **argv) {
    int dry_run=0,verbose __attribute__((unused)) =0,no_model=0;
    int do_scan=0, do_selftest=0, do_audit=0, do_undo=0;
    int undo_n=1;
    long audit_since = 0;
    int audit_limit = 20;
    const char *registry_path=NULL;
    char *cmd=NULL;
    for(int i=1;i<argc;i++) { if(strcmp(argv[i],"--dry-run")==0)dry_run=1; else if(strcmp(argv[i],"--verbose")==0)verbose=1; else if(strcmp(argv[i],"--no-model")==0)no_model=1; else if(strcmp(argv[i],"-c")==0&&i+1<argc)cmd=argv[++i];
        else if(strcmp(argv[i],"--registry")==0&&i+1<argc)registry_path=argv[++i];
        else if(strcmp(argv[i],"--scan")==0)do_scan=1;
        else if(strcmp(argv[i],"--self-test")==0)do_selftest=1;
        else if(strcmp(argv[i],"--audit")==0)do_audit=1;
        else if(strcmp(argv[i],"--undo")==0){
            do_undo=1;
            if(i+1<argc && argv[i+1][0]>='1' && argv[i+1][0]<='9') undo_n=atoi(argv[++i]);
        }
        else if(strcmp(argv[i],"--since")==0&&i+1<argc)audit_since=atol(argv[++i]);
        else if(strcmp(argv[i],"--limit")==0&&i+1<argc)audit_limit=atoi(argv[++i]); else if(strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0){print_usage();return 0;} }
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

    if(!no_model) {
        const char *mp=getenv("NL_MODEL_PATH"); if(!mp)mp="/usr/share/nl-os/model.gguf";
        if(nl_init(mp)!=0&&!no_model) fprintf(stderr,"⚠️ 模型加载失败，仅规则引擎可用\n");
    }

    NlCapRegistry *reg = nl_cap_load(reg_path, NULL);
    nl_dispatch_set_registry(reg);

    if(cmd) {
        char out[4096];
        int rc = nl_dispatch(cmd,out,sizeof(out));
        printf("%s\n",out);
        if (reg) nl_cap_free(reg);
        return rc == 0 ? 0 : 1;
    } else {
        printf("NL-Shell v0.1 · 输入自然语言或传统命令 · Ctrl+C 中断 · Ctrl+D 退出\n");
        while(g_running) {
            char *line=nl_readline("nl> ");
            if(!line) { printf("\n"); break; }
            if(strlen(line)==0) { free(line); continue; }
            nl_history_add(line);
            if(dry_run) { NlParseResult r; if(nl_parse(line,&r)==0) printf("[dry-run] %s\n",r.command?r.command:"(none)"); nl_parse_result_free(&r); }
            else { char out[4096]; nl_dispatch(line,out,sizeof(out)); printf("%s\n",out); }
            free(line);
        }
    }
    if (reg) nl_cap_free(reg);
    nl_history_save(); nl_free(); return 0;
}
