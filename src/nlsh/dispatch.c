#include "dispatch.h"
#include "nlc_api.h"
#include "ipc_client.h"
#include "exec.h"
#include "audit.h"
#include "budget.h"
#include "snapshot.h"
#include "capability_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

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
    if (!g_reg || !missing) return 0;

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

/*
 * 解析失败时的候选提示：规则层没有覆盖的说法（"重启 nginx"），按关键词映射到
 * 注册表里可能对应的能力。只提示、不执行——猜错一次也比默默说"无法理解"好。
 */
static const struct { const char *kw; const char *argv0; } UNKNOWN_HINTS[] = {
    { "重启",     "systemctl" },
    { "启动",     "systemctl" },
    { "停止",     "systemctl" },
    { "服务",     "systemctl" },
    { "安装",     "apt"       },
    { "卸载",     "apt"       },
    { "端口",     "ss"        },
    { "进程",     "ps"        },
    { "内存",     "free"      },
    { "磁盘",     "df"        },
    { "网络",     "ip"        },
    { "防火墙",   "ufw"       },
    { "定时任务", "crontab"   },
    { "关机",     "shutdown"  },
    { NULL, NULL }
};

static int suggest_from_keywords(const char *input, char out[3][NL_CAP_MAX_ID_LEN])
{
    int n = 0;
    for (int i = 0; UNKNOWN_HINTS[i].kw && n < 3; i++) {
        if (strstr(input, UNKNOWN_HINTS[i].kw) == NULL) continue;
        char id[NL_CAP_MAX_ID_LEN];
        snprintf(id, sizeof(id), "cli:%s", UNKNOWN_HINTS[i].argv0);
        if (!g_reg || nl_cap_find(g_reg, id) == NULL) continue;
        int dup = 0;
        for (int k = 0; k < n; k++) if (strcmp(out[k], id) == 0) dup = 1;
        if (!dup) snprintf(out[n++], NL_CAP_MAX_ID_LEN, "%s", id);
    }
    return n;
}

/* ── 确认门 ── */

static int assume_yes(void)
{
    const char *v = getenv("NL_ASSUME_YES");
    return v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y');
}

static int is_affirmative(const char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    return strncmp(line, "y", 1) == 0 || strncmp(line, "Y", 1) == 0 ||
           strncmp(line, "是", strlen("是")) == 0;
}

/* 确认：返回 1 = 继续执行，0 = 用户取消（含 EOF：fail-closed） */
static int confirm(const char *cmd, int perm_level)
{
    if (assume_yes()) return 1;
    printf("⚠️ 需要确认 (L%d): %s\n执行? [y/N] ", perm_level, cmd);
    fflush(stdout);
    char yn[32];
    if (!fgets(yn, sizeof(yn), stdin)) { printf("\n已取消（无输入）\n"); return 0; }
    if (!is_affirmative(yn)) { printf("已取消（未执行）\n"); return 0; }
    return 1;
}

/* 统一打印出口：dispatch 自己负责输出（执行路径要与子进程输出交错） */
static void say(const char *text)
{
    if (text && text[0]) { fputs(text, stdout); fputc('\n', stdout); fflush(stdout); }
}

int nl_dispatch(const char *input, char *output, size_t out_sz, int dry_run)
{
    if (!input || !output) return -1;
    output[0] = '\0';

    /* ── 快路径：传统命令直接执行，不经模型 ── */
    if (nl_is_traditional_cmd(input)) {
        if (dry_run) {
            snprintf(output, out_sz, "[dry-run] %s", input);
            say(output);
            audit_emit(input, "fastpath", "allow", "dry-run", "dry-run", 1, 0);
            return 0;
        }
        long t0 = now_us();
        FILE *fp = popen(input, "r");
        if (!fp) {
            snprintf(output, out_sz, "(error)");
            say(output);
            audit_emit(input, "fastpath", "allow", "error", "popen-failed", 1, now_us() - t0);
            return -1;
        }
        size_t n = fread(output, 1, out_sz - 1, fp);
        output[n] = '\0';
        int rc = pclose(fp);
        say(output);
        audit_emit(input, "fastpath", "allow", (rc == 0) ? "ok" : "error", "fast-path", 1, now_us() - t0);
        return 0;
    }

    /* ── 输入级闸门：自然语言句子里混入 shell 语法 → 直接拒绝。
     * 否则「创建一个叫 x; touch hacked」会被规则层解析成子命令 `touch hacked`，
     * 而生成命令本身是干净的（不含元字符），只有输入层能拦住。 ── */
    {
        const char *bad = strpbrk(input, ";|&$`><");
        if (bad != NULL) {
            snprintf(output, out_sz,
                     "拒绝执行：输入含 shell 元字符 '%c'，自然语言模式不支持 shell 语法", *bad);
            say(output);
            audit_emit(input, "", "refused", "refused", "input-metachar", 0, 0);
            return -1;
        }
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
            say(output);
            audit_emit(input, "", "refused", "refused", "complexity-budget", 0, 0);
            return -1;
        }
    }

    NlParseResult r;
    if (nl_parse(input, &r) != 0 || !r.command) {
        /* 解析失败也要说清原因：能猜的给候选，猜不到的直说 */
        char cands[3][NL_CAP_MAX_ID_LEN];
        int n = suggest_from_keywords(input, cands);
        size_t used;
        if (n > 0) {
            used = (size_t)snprintf(output, out_sz, "无法理解这句话；本机没有该能力或未支持该说法。\n相近能力：");
            for (int i = 0; i < n && used < out_sz; i++)
                used += (size_t)snprintf(output + used, out_sz - used, "%s%s",
                                         (i ? "、" : ""), cands[i]);
        } else {
            snprintf(output, out_sz,
                     "无法理解这句话。可用 `nlsh --self-test` 查看本机现有能力。");
        }
        say(output);
        audit_emit(input, "", "refused", "refused", "unparsed", 0, 0);
        return -1;
    }

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
        say(output);
        audit_emit(input, cap_id, "refused", "refused", "capability-missing", 0, 0);
        nl_parse_result_free(&r);
        return -1;
    }

    if (nl_exec_has_metachars(r.command)) {
        snprintf(output, out_sz, "拒绝执行：生成的命令含 shell 元字符：%s", r.command);
        say(output);
        audit_emit(input, cap_id, "refused", "refused", "metachar", 0, 0);
        nl_parse_result_free(&r);
        return -1;
    }

    /* ── 预演：只解析不执行（AC-7） ── */
    if (dry_run) {
        snprintf(output, out_sz, "[dry-run] %s", r.command);
        say(output);
        audit_emit(input, cap_id, "allow", "dry-run", (r.perm_level >= 2) ? "write" : "read", 0, 0);
        nl_parse_result_free(&r);
        return 0;
    }

    /* ── 确认门：写操作 / L2+ 才问（AC-3/AC-4） ── */
    int need_confirm = r.needs_confirm || r.perm_level >= 2;
    if (need_confirm && !confirm(r.command, r.perm_level)) {
        snprintf(output, out_sz, "已取消：%s", r.command);
        audit_emit(input, cap_id, "confirm", "cancelled", "user-declined", 0, 0);
        nl_parse_result_free(&r);
        return -1;
    }

    /* ── 权限裁决（perm.sock 存在时问一次；不存在则跳过，不阻塞） ── */
    int fd = nl_ipc_connect("/run/nl-os/perm.sock");
    if (fd >= 0) {
        char req[1024], resp[512];
        snprintf(req, sizeof(req),
                 "{\"command_type\":\"%s\",\"nl_input\":\"%s\",\"generated_cmd\":\"%s\",\"paths\":[]}",
                 nl_intent_name(r.intent), input, r.command);
        if (nl_ipc_request(fd, req, resp, sizeof(resp)) == 0) {
            printf("[权限] %s\n", resp);
            fflush(stdout);
        }
        nl_ipc_close(fd);
    }

    /* ── 快照：写操作执行前留底，供 --undo（AC-5） ── */
    if (need_confirm || r.perm_level >= 2) {
        char ps[8][NL_EXEC_PATH_MAX];
        const char *arr[8];
        int np = nl_exec_collect_paths(r.command, ps, 8);
        for (int k = 0; k < np; k++) arr[k] = ps[k];
        if (np > 0) {
            char sdir[1024];
            /* 只对删除/移动这类高危操作提示：创建不存在的路径本来就没东西可存 */
            if (nl_snapshot_take(arr, np, sdir, sizeof(sdir)) == NL_SNAP_SKIPPED &&
                r.perm_level >= 3)
                fprintf(stderr, "⚠️ 未留快照（超限或含敏感路径）\n");
        }
    }

    /* ── 执行（不经 shell；子进程继承 stdout，fork 前必须 flush） ── */
    printf("→ %s\n", r.command);
    fflush(stdout);
    fflush(stderr);
    long t0 = now_us();
    int rc = nl_exec_execute(r.command, 5000);
    long dur = now_us() - t0;

    if (rc == 0) {
        printf("✅ 完成（%ld ms）\n", dur / 1000);
        snprintf(output, out_sz, "→ %s\n✅ 完成", r.command);
        audit_emit(input, cap_id, need_confirm ? "confirm" : "allow", "ok",
                   "executed", 0, dur);
    } else {
        printf("❌ 执行失败（退出码 %d）\n", rc);
        snprintf(output, out_sz, "→ %s\n❌ 执行失败", r.command);
        audit_emit(input, cap_id, need_confirm ? "confirm" : "allow", "error",
                   "exec-failed", 0, dur);
    }
    fflush(stdout);
    nl_parse_result_free(&r);
    return rc == 0 ? 0 : -1;
}
