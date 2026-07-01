#include "script_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
NlScript *nl_script_parse(const char *fp){
    FILE *f=fopen(fp,"r");if(!f)return NULL;
    NlScript *s=calloc(1,sizeof(NlScript));if(!s){fclose(f);return NULL;}
    int cap=32;s->lines=calloc(cap,sizeof(char*));
    char buf[4096];
    while(fgets(buf,sizeof(buf),f)){
        char *l=buf;while(*l==' '||*l=='\t')l++;
        if(*l=='#'){
            if(strncmp(l,"# @perm:",8)==0&&strstr(l,"allow-l2"))s->allow_l2=1;
            continue;
        }
        size_t len=strlen(l);while(len>0&&(l[len-1]=='\n'||l[len-1]=='\r'))l[--len]=0;
        if(len==0)continue;
        if(s->count>=cap){cap*=2;s->lines=realloc(s->lines,cap*sizeof(char*));}
        s->lines[s->count++]=strdup(l);
    }
    fclose(f);return s;
}
void nl_script_free(NlScript *s){if(!s)return;for(int i=0;i<s->count;i++)free(s->lines[i]);free(s->lines);free(s);}
