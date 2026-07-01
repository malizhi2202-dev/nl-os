#include "dispatch.h"
#include "nlc_api.h"
#include "ipc_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static const char *CMD_PREFIX[] = {"ls","cd","ps","echo","find","grep","cat","less","head","tail","rm","cp","mv","mkdir","touch","chmod","chown","tar","gzip","gunzip","zip","unzip","wget","curl","git","make","gcc","clang","python","node","npm","docker","systemctl","sudo","su","ssh","scp","rsync","df","du","top","htop","kill","pkill","ping","ifconfig","ip","netstat","ss","awk","sed","sort","uniq","wc","diff","patch","file","stat","ln","export","source",".","alias","unalias","type","which","whoami","id","groups","passwd","mount","umount","shutdown","reboot",NULL};
int nl_is_traditional_cmd(const char *input) {
    if (!input||!*input) return 0;
    const char *s=input; while(*s==' ')s++;
    for(int i=0;CMD_PREFIX[i];i++) {
        size_t l=strlen(CMD_PREFIX[i]);
        if(strncmp(s,CMD_PREFIX[i],l)==0&&(!s[l]||s[l]==' '||s[l]=='|'||s[l]==';'||s[l]=='&'||s[l]=='>'||s[l]=='<'))
            return 1;
    }
    if(*s=='/'||*s=='.'||*s=='~'||strchr(s,'|')||strchr(s,'>')||strchr(s,'<'))
        return 1;
    for(const char *p=s;*p;p++) if(!isascii(*p)&&!isprint(*p)) return 0;
    return 0;
}
int nl_dispatch(const char *input, char *output, size_t out_sz) {
    if(!input||!output) return -1;
    if(nl_is_traditional_cmd(input)) {
        FILE *fp=popen(input,"r");
        if(!fp){snprintf(output,out_sz,"(error)");return -1;}
        size_t n=fread(output,1,out_sz-1,fp);
        output[n]='\0'; pclose(fp); return 0;
    }
    NlParseResult r;
    if(nl_parse(input,&r)!=0||!r.command) { snprintf(output,out_sz,"无法理解"); return -1; }
    snprintf(output,out_sz,"[%s] %s\n需确认: %s",nl_intent_name(r.intent),r.command,r.needs_confirm?"是":"否");
    int fd=nl_ipc_connect("/run/nl-os/perm.sock");
    if(fd>=0) {
        char req[1024],resp[512];
        snprintf(req,sizeof(req),"{\"command_type\":\"%s\",\"nl_input\":\"%s\",\"generated_cmd\":\"%s\",\"paths\":[]}",
            nl_intent_name(r.intent),input,r.command);
        if(nl_ipc_request(fd,req,resp,sizeof(resp))==0)
            snprintf(output,out_sz,"%s\n[权限: %s]",r.command,resp);
        nl_ipc_close(fd);
    }
    nl_parse_result_free(&r); return 0;
}
