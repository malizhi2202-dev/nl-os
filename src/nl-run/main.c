#include "script_parser.h"
#include "nlc_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char**argv){
    if(argc<2){fprintf(stderr,"用法: nl-run <file.tools>\n");return -1;}
    int dry_run=0;for(int i=1;i<argc;i++){if(strcmp(argv[i],"--dry-run")==0)dry_run=1;}
    const char *fp=argv[argc-1];
    NlScript *s=nl_script_parse(fp);
    if(!s||s->count==0){fprintf(stderr,"无法解析 %s\n",fp);nl_script_free(s);return -1;}
    if(!dry_run){
        const char *mp=getenv("NL_MODEL_PATH");if(!mp)mp="/usr/share/nl-os/model.gguf";
        if(nl_init(mp)!=0){fprintf(stderr,"模型加载失败\n");nl_script_free(s);return -1;}
    }
    printf("执行 %s (%d 步):\n",fp,s->count);
    for(int i=0;i<s->count;i++){
        printf("[%d/%d] %s\n",i+1,s->count,s->lines[i]);
        if(dry_run)continue;
        NlParseResult r;
        if(nl_parse(s->lines[i],&r)!=0||!r.command){fprintf(stderr,"❌ 步骤 %d 失败: 无法解析\n",i+1);nl_script_free(s);nl_free();return i+1;}
        if(r.needs_confirm&&!s->allow_l2){printf("⚠️ 需要确认 (L%d): %s\n输入 yes 继续: ",r.perm_level,r.command);char yn[8];if(!fgets(yn,sizeof(yn),stdin)||strncmp(yn,"yes",3)!=0){printf("已取消\n");nl_parse_result_free(&r);nl_script_free(s);nl_free();return i+1;}}
        printf("  → %s\n",r.command);
        nl_parse_result_free(&r);
    }
    printf("✅ 全部完成\n");
    nl_script_free(s);nl_free();return 0;
}
