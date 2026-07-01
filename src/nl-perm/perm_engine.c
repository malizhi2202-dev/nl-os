#include "perm_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *L4_PREFIX[] = {"/etc","/boot","/sys","/proc","/dev/sd","/dev/nvme","/dev/mmcblk","/dev/dm-","/dev/loop","/dev/vd","/lib","/lib64","/usr/lib","/usr/lib64","/root","/run/systemd",NULL};
static const char *L4_EXACT[] = {"/","/etc/passwd","/etc/shadow","/etc/sudoers",NULL};
static const char *WHITE[] = {"/home","/tmp","/var/","/opt","/mnt",NULL};
static char **cfg_l4 = NULL; static int cfg_n = 0;

static int pref(const char *p, const char **l) {
    for (int i=0;l[i];i++) if (strncmp(p,l[i],strlen(l[i]))==0) return 1;
    for (int i=0;i<cfg_n;i++) if (strncmp(p,cfg_l4[i],strlen(cfg_l4[i]))==0) return 1;
    return 0;
}
static int exact(const char *p, const char **l) {
    for (int i=0;l[i];i++) if (strcmp(p,l[i])==0) return 1;
    return 0;
}
static int rd(const char *t) {
    return t && (strcmp(t,"read")==0||strcmp(t,"list")==0||strcmp(t,"view")==0||strcmp(t,"find")==0||strcmp(t,"search")==0);
}
static int dl(const char *t) { return t && strcmp(t,"delete")==0; }

int nl_perm_check(const NlPermRequest *req, NlPermResponse *resp) {
    if (!req||!resp) return -1;
    memset(resp,0,sizeof(NlPermResponse));
    snprintf(resp->audit_id,sizeof(resp->audit_id),"nl-%d-%ld",getpid(),time(NULL));

    if (rd(req->command_type)) {
        resp->decision=0; resp->level=NL_PERM_L1;
        snprintf(resp->reason,sizeof(resp->reason),"只读操作"); return 0;
    }
    for (int i=0;i<req->path_count;i++) {
        char *n=realpath(req->paths[i],NULL); const char *c=n?n:req->paths[i];
        if (exact(c,L4_EXACT)||pref(c,L4_PREFIX)) {
            resp->decision=2; resp->level=NL_PERM_L4;
            snprintf(resp->reason,sizeof(resp->reason),"路径在L4保护名单"); free(n); return 0;
        }
        if (strncmp(c,"/dev/",5)==0 && !rd(req->command_type)) {
            resp->decision=2; resp->level=NL_PERM_L4;
            snprintf(resp->reason,sizeof(resp->reason),"/dev仅允许读"); free(n); return 0;
        }
        free(n);
    }
    if (dl(req->command_type)) {
        int sf=1;
        for (int i=0;i<req->path_count;i++) {
            int w=0;
            for (int j=0;WHITE[j];j++)
                if (strncmp(req->paths[i],WHITE[j],strlen(WHITE[j]))==0) { w=1; break; }
            if (!w) { sf=0; break; }
        }
        resp->decision=sf?1:2; resp->level=sf?NL_PERM_L3:NL_PERM_L4;
        snprintf(resp->reason,sizeof(resp->reason),sf?"删除需输入yes确认":"目标不在安全白名单"); return 0;
    }
    resp->decision=1; resp->level=NL_PERM_L2;
    snprintf(resp->reason,sizeof(resp->reason),"修改操作按Enter确认"); return 0;
}

int nl_perm_load_config(const char *path) {
    if (!path) return -1;
    FILE *f=fopen(path,"r"); if (!f) return -1;
    char line[512];
    while (fgets(line,sizeof(line),f)) {
        char *s=line;
        while (*s==' '||*s=='\t') s++;
        if (*s=='#'||*s=='\n'||*s==0) continue;
        size_t l=strlen(s);
        while (l>0&&(s[l-1]=='\n'||s[l-1]=='\r')) s[--l]=0;
        if (l==0) continue;
        cfg_l4=realloc(cfg_l4,(cfg_n+1)*sizeof(char*));
        cfg_l4[cfg_n++]=strdup(s);
    }
    fclose(f); return 0;
}

int nl_perm_reload_config(void) {
    for (int i=0;i<cfg_n;i++) free(cfg_l4[i]);
    free(cfg_l4); cfg_l4=NULL; cfg_n=0;
    return nl_perm_load_config("/etc/nl-os/perm_rules.conf");
}
