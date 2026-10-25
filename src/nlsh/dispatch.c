#include "dispatch.h"
#include "nlc_api.h"
#include "ipc_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "audit.h"
#include "budget.h"
#include "capability_registry.h"
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

static NlCapRegistry *g_reg;

void nl_dispatch_set_registry(NlCapRegistry *r) { g_reg = r; }
NlCapRegistry *nl_dispatch_registry(void) { return g_reg; }

static long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

static void audit_emit(const char *input, const char *cap_id, const char *verdict,
                       const char *result, const char *reason, int fast_path, long dur)
{
    NlAuditRec r;
    memset(&r, 0, sizeof(r));
    r.capability_id = cap_id;
    r.perm_level = 0;
    r.verdict = verdict;
    r.effect = "read";
    r.fast_path = fast_path;
    r.probability = -1;
    r.duration_us = dur;
    r.result = result;
    r.reason = reason;
    r.input = input;
    r.corr = "";
    (void)nl_audit_emit(&r);
}

/* 取命令的 argv0（去路径、去 sudo/env 前缀） */
static void command_argv0(const char *cmd, char *out, size_t cap)
{
    const char *p = cmd;
    while (*p == ' ') p++;
    /* 跳过常见包装器，最多两层 */
    for (int round = 0; round < 2; round++) {
        const char *space = strchr(p, ' ');
        if (!space) break;
        size_t n = (size_t)(space - p);
        if (!((n == 4 && strncmp(p, "sudo", 4) == 0) ||
              (n == 3 && strncmp(p, "env", 3) == 0))) break;
        p = space + 1;
        while (*p == ' ') p++;
    }
    const char *end = p;
    while (*end && *end != ' ') end++;
    const char *base = p;
    for (const char *q = p; q < end; q++) if (*q == '/') base = q + 1;
    size_t n = (size_t)(end - base);
    if (n >= cap) n = cap - 1;
    memcpy(out, base, n);
    out[n] = '\0';
}

/* 候选能力：按公共前缀 + 子串命中粗排，取前 3（避免自引用拷贝，用独立数组） */
typedef struct { int score; char id[NL_CAP_MAX_ID_LEN]; } NlCand;

static int suggest_candidates(const char *missing, char out[3][NL_CAP_MAX_ID_LEN])
{
    if (!g_reg) return 0;

    NlCand best[3];
    int n = 0;
    memset(best, 0, sizeof(best));

    for (int i = 0; i < nl_cap_count(g_reg); i++) {
        const NlCapability *c = nl_cap_at(g_reg, i);
        if (!c) continue;
        int score = strstr(c->id, missing) ? 100 : 0;
        for (int k = 0; c->id[k] && missing[k] && c->id[k] == missing[k]; k++) score++;

        if (n < 3) {
            best[n].score = score;
            snprintf(best[n].id, NL_CAP_MAX_ID_LEN, "%s", c->id);
            n++;
        } else {
            int min = 0;
            for (int s = 1; s < 3; s++) if (best[s].score < best[min].score) min = s;
            if (score > best[min].score) {
                best[min].score = score;
                snprintf(best[min].id, NL_CAP_MAX_ID_LEN, "%s", c->id);
            }
        }
    }

    /* n <= 3，直接按分数降序冒泡 */
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (best[b].score > best[a].score) { NlCand t = best[a]; best[a] = best[b]; best[b] = t; }

    for (int i = 0; i < n; i++)
        snprintf(out[i], NL_CAP_MAX_ID_LEN, "%s", best[i].id);
    return n;
}

int nl_dispatch(const char *input, char *output, size_t out_sz) {
    if(!input||!output) return -1;

    /* ── 快路径：传统命令直接执行，不经模型 ── */
    if(nl_is_traditional_cmd(input)) {
        long t0 = now_us();
        FILE *fp=popen(input,"r");
        if(!fp){snprintf(output,out_sz,"(error)"); audit_emit(input,"fastpath","allow","error","popen-failed",1,now_us()-t0);return -1;}
        size_t n=fread(output,1,out_sz-1,fp);
        output[n]='\0';
        int rc = pclose(fp);
        audit_emit(input,"fastpath","allow",(rc==0)?"ok":"error","fast-path",1,now_us()-t0);
        return 0;
    }

    /* ── 复杂度预算：超范围直接拒绝，并给可执行替代 ── */
    {
        NlBudgetDetail bd;
        if (nl_budget_check(input, g_reg, &bd) == NL_BUDGET_REFUSE) {
            size_t used = (size_t)snprintf(output, out_sz, "解析不了（超出能力边界）：");
            for (int i = 0; i < bd.dim_count && used < out_sz; i++)
                used += (size_t)snprintf(output + used, out_sz - used, "%s%s",
                                         (i ? "、" : ""), nl_budget_dim_str(bd.dims[i]));
            if (used < out_sz)
                used += (size_t)snprintf(output + used, out_sz - used, "\n替代步骤：");
            for (int i = 0; i < bd.alternative_count && used < out_sz; i++)
                used += (size_t)snprintf(output + used, out_sz - used, "%s%s",
                                         (i ? "、" : ""), bd.alternative_ids[i]);
            audit_emit(input,"","refused","refused","complexity-budget",0,0);
            return -1;
        }
    }

    NlParseResult r;
    if(nl_parse(input,&r)!=0||!r.command) { snprintf(output,out_sz,"无法理解"); audit_emit(input,"","refused","refused","unparsed",0,0); return -1; }

    /* ── 能力存在性：解析出能力但本机没有 → 明确拒绝并给候选 ── */
    char argv0[64], cap_id[NL_CAP_MAX_ID_LEN];
    command_argv0(r.command, argv0, sizeof(argv0));
    snprintf(cap_id, sizeof(cap_id), "cli:%s", argv0);
    if (g_reg && nl_cap_find(g_reg, cap_id) == NULL) {
        char cands[3][NL_CAP_MAX_ID_LEN];
        int n = suggest_candidates(argv0, cands);
        size_t used = (size_t)snprintf(output, out_sz,
            "本机没有该能力：%s\n候选：", cap_id);
        for (int i = 0; i < n && used < out_sz; i++)
            used += (size_t)snprintf(output + used, out_sz - used, "%s%s",
                                     (i ? "、" : ""), cands[i]);
        if (n == 0 && used < out_sz) snprintf(output + used, out_sz - used, "(无)");
        audit_emit(input, cap_id, "refused", "refused", "capability-missing", 0, 0);
        nl_parse_result_free(&r);
        return -1;
    }

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
    audit_emit(input, cap_id, r.needs_confirm?"confirm":"allow", "ok",
               r.needs_confirm?"needs-confirm":"read-only", 0, 0);
    nl_parse_result_free(&r); return 0;
}
