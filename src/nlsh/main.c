#include "dispatch.h"
#include "history.h"
#include "line_edit.h"
#include "nlc_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
static volatile int g_running=1;
static void sigint_handler(int sig) { (void)sig; g_running=0; }
static void print_usage(void) { printf("nlsh - NL-Shell\n用法: nlsh [--dry-run] [--verbose] [--no-model] [-c cmd]\n"); }
int main(int argc, char **argv) {
    int dry_run=0,verbose __attribute__((unused)) =0,no_model=0; char *cmd=NULL;
    for(int i=1;i<argc;i++) { if(strcmp(argv[i],"--dry-run")==0)dry_run=1; else if(strcmp(argv[i],"--verbose")==0)verbose=1; else if(strcmp(argv[i],"--no-model")==0)no_model=1; else if(strcmp(argv[i],"-c")==0&&i+1<argc)cmd=argv[++i]; else if(strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0){print_usage();return 0;} }
    signal(SIGINT,sigint_handler);
    nl_history_init(getenv("HOME")?strcat(strcpy(malloc(256),getenv("HOME")),"/.nlsh_history"):NULL);

    if(!no_model) {
        const char *mp=getenv("NL_MODEL_PATH"); if(!mp)mp="/usr/share/nl-os/model.gguf";
        if(nl_init(mp)!=0&&!no_model) fprintf(stderr,"⚠️ 模型加载失败，仅规则引擎可用\n");
    }

    if(cmd) {
        char out[4096]; nl_dispatch(cmd,out,sizeof(out)); printf("%s\n",out);
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
    nl_history_save(); nl_free(); return 0;
}
