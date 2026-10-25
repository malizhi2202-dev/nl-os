/*
 * cmdgen.c — 自然语言 → 命令 专用小模型（ADR-025）
 *
 * 与经典"指令模型"路径的区别：
 *   指令模型：自由生成 JSON（intent + entities），要模型同时理解语义、抽参数、守格式；
 *    0.5B 上这三件事都不稳，于是常常输出 other/invalid。
 *   本路径：专用微调模型只做一件事 —— 直接吐出命令。提示词、system、停用符都固定
 *    （模型卡里给的就是 ChatML + 固定的 system），解码用贪心（temperature=0）。
 *
 * 本文件只负责"拿到一条候选命令"，安全判断全在调用方（dispatch）：
 * 元字符检查、能力是否登记、权限等级、确认门、快照、审计 —— 与规则层同一条链路。
 */

#include "cmdgen.h"
#include "model_loader.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 模型卡里的 system（写成定值，避免每个用户机器上表现不同） */
static const char *const CMDGEN_SYSTEM =
    "You are a Linux and Ubuntu terminal assistant. "
    "Return only the exact, executable Bash command matching the user request "
    "with no markdown formatting or conversational filler.";

static NlModel *g_model = NULL;
static int      g_failed = 0;

int nl_cmdgen_available(void)
{
    if (g_failed) return 0;
    const char *p = nl_cmdgen_model_path();
    return (p && p[0] && access(p, R_OK) == 0) ? 1 : 0;
}

int nl_cmdgen_loaded(void) { return g_model ? 1 : 0; }

void nl_cmdgen_unload(void)
{
    if (g_model) { nl_model_free(g_model); g_model = NULL; }
}

static int ensure_model(void)
{
    if (g_model) return 0;
    if (g_failed) return -1;
    const char *path = nl_cmdgen_model_path();
    if (!path || !path[0]) { g_failed = 1; return -1; }
    g_model = nl_model_init(path);
    if (!g_model) { g_failed = 1; return -1; }
    return 0;
}

/* 去掉模型可能带出来的客套话/围栏/提示符，只留第一行命令 */
static void clean_output(char *s)
{
    /* 特殊 token 会被当作普通文本吐出来（`<|im_end|>`）：
     * 不切掉它，命令里就多了 `<` `|`，安全闸直接判成元字符。
     * 模型卡里的 stop 序列在这里没有"自动生效"，必须自己截。 */
    char *mark = strstr(s, "<|");
    if (mark) *mark = '\0';

    /* 去掉 ```bash ... ``` 围栏 */
    char *fence = strstr(s, "```");
    while (fence) {
        char *nl = strchr(fence, '\n');
        if (!nl) { *fence = '\0'; break; }
        memmove(fence, nl + 1, strlen(nl + 1) + 1);
        fence = strstr(s, "```");
    }

    /* 去掉常见前缀：「命令：」「Command:」「$ 」 */
    static const char *const PREFIX[] = {
        "命令：", "命令:", "Command:", "command:", "命令是", "$ ", "> ", "回答：", "回答:"
    };
    for (int round = 0; round < 3; round++) {
        int stripped = 0;
        for (size_t i = 0; i < sizeof(PREFIX) / sizeof(PREFIX[0]); i++) {
            size_t pl = strlen(PREFIX[i]);
            if (strncmp(s, PREFIX[i], pl) == 0) {
                memmove(s, s + pl, strlen(s + pl) + 1);
                stripped = 1;
                break;
            }
        }
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') memmove(s, s + 1, strlen(s));
        if (!stripped) break;
    }

    /* 只保留第一行（模型偶尔会补一句解释） */
    char *nl = strchr(s, '\n');
    if (nl) *nl = '\0';

    /* 去掉包裹的反引号/引号 */
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '`' || s[len-1] == '"' || s[len-1] == '\'' ||
                       s[len-1] == ' ' || s[len-1] == '.'))
        s[--len] = '\0';
    while (*s == '`' || *s == '"' || *s == '\'') memmove(s, s + 1, strlen(s));
}

int nl_cmdgen_generate(const char *input, char *out, size_t cap)
{
    if (!input || !*input || !out || cap == 0) return -1;
    if (ensure_model() != 0) return -1;

    char prompt[2048];
    if (nl_model_format_chat(g_model, CMDGEN_SYSTEM, input, prompt, sizeof(prompt)) < 0)
        return -1;

    char raw[1024];
    int n = nl_model_infer(g_model, prompt, raw, sizeof(raw));
    if (getenv("NL_CMDGEN_DEBUG"))
        fprintf(stderr, "[cmdgen] n=%d prompt_len=%zu raw=%s\n", n, strlen(prompt), n > 0 ? raw : "(none)");
    if (n <= 0) return -1;

    clean_output(raw);
    if (!raw[0]) return -1;
    snprintf(out, cap, "%s", raw);
    return 0;
}
