#include "script_parser.h"
#include "nlc_api.h"
#include "freeze.h"
#include "snapshot.h"
#include "exec.h"
#include "budget.h"
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char**argv){
    if(argc<2){fprintf(stderr,"用法: nl-run <file.tools>\n");return -1;}
    int dry_run=0, do_freeze=0;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--dry-run")==0)dry_run=1;
        else if(strcmp(argv[i],"--freeze")==0)do_freeze=1;
    }
    const char *fp=argv[argc-1];
    NlScript *s=nl_script_parse(fp);
    if(!s||s->count==0){fprintf(stderr,"无法解析 %s\n",fp);nl_script_free(s);return -1;}
    /* 固化：把当前脚本写成带校验和的 v2 格式 */
    if(do_freeze){
        if(nl_freeze_write(fp, s->lines, s->count)!=0){
            fprintf(stderr,"固化失败: %s\n",fp);nl_script_free(s);return -1;
        }
        printf("✅ 已固化 %s（%d 步，含 sha256 校验和）\n",fp,s->count);
        printf("   # @checksum: sha256:… （篡改后将被拒绝执行）\n");
        nl_script_free(s);return 0;
    }

    /* 执行前校验：固化文件被改动 → 拒绝执行任何步骤 */
    NlFreezeStatus fs = nl_freeze_verify(fp);
    if(fs==NL_FREEZE_TAMPERED){
        fprintf(stderr,"❌ 校验和不匹配：%s 已被修改，拒绝执行任何步骤\n",fp);
        nl_script_free(s);return -1;
    }
    if(fs==NL_FREEZE_PLAIN)
        fprintf(stderr,"⚠️ 未固化脚本（将经模型解析）\n");

    if(!dry_run){
        if(fs==NL_FREEZE_OK){
            /* 固化脚本自带确定性命令，不需要模型 */
            fprintf(stderr,"⚠️ 使用固化脚本（不经模型）\n");
        }else{
            const char *mp=getenv("NL_MODEL_PATH");if(!mp)mp="/usr/share/nl-os/model.gguf";
            if(nl_init(mp)!=0){fprintf(stderr,"模型加载失败\n");nl_script_free(s);return -1;}
        }
    }
    printf("执行 %s (%d 步):\n",fp,s->count);
    for(int i=0;i<s->count;i++){
        printf("[%d/%d] %s\n",i+1,s->count,s->lines[i]);
        if(dry_run)continue;
        /* 与 nlsh 同一道复杂度闸门：多动作/无对应能力 → 整步拒绝，不降级成单条命令 */
        {
            NlBudgetDetail bd;
            if(nl_budget_check(s->lines[i], NULL, &bd)==NL_BUDGET_REFUSE){
                fprintf(stderr,"❌ 步骤 %d 拒绝执行：超出能力边界（",i+1);
                for(int k=0;k<bd.dim_count;k++) fprintf(stderr,"%s%s",k?"、":"",nl_budget_dim_str(bd.dims[k]));
                fprintf(stderr,"）\n");
                nl_script_free(s);nl_free();return i+1;
            }
        }
        NlParseResult r;
        if(nl_parse(s->lines[i],&r)!=0||!r.command){fprintf(stderr,"❌ 步骤 %d 失败: 无法解析\n",i+1);nl_script_free(s);nl_free();return i+1;}
        if(r.needs_confirm&&!s->allow_l2){printf("⚠️ 需要确认 (L%d): %s\n输入 yes 继续: ",r.perm_level,r.command);char yn[8];if(!fgets(yn,sizeof(yn),stdin)||strncmp(yn,"yes",3)!=0){printf("已取消\n");nl_parse_result_free(&r);nl_script_free(s);nl_free();return i+1;}}
        printf("  → %s\n",r.command);
        if(!dry_run && (r.needs_confirm || r.perm_level >= 2)){
            char ps[8][512];
            const char *arr[8];
            int np = nl_exec_collect_paths(r.command, ps, 8);
            for(int k=0;k<np;k++) arr[k]=ps[k];
            if(np > 0){
                char sdir[1024];
                NlSnapStatus st = nl_snapshot_take(arr, np, sdir, sizeof(sdir));
                if(st==NL_SNAP_SKIPPED)
                    fprintf(stderr,"⚠️ 步骤 %d 未留快照（超限或含敏感路径）\n", i+1);
            }
        }
        if(!dry_run){
            int rc = nl_exec_execute(r.command, 5000);
            if(rc!=0){
                fprintf(stderr,"❌ 步骤 %d 失败: 退出码 %d\n",i+1,rc);
                nl_parse_result_free(&r);nl_script_free(s);nl_free();return i+1;
            }
        }
        nl_parse_result_free(&r);
    }
    printf("✅ 全部完成\n");
    nl_script_free(s);nl_free();return 0;
}
