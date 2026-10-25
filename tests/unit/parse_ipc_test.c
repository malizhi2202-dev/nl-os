/*
 * parse_ipc_test.c — 常驻解析进程的编解码与"规则层判定一致"
 *
 * 守护进程与客户端共用同一份编解码，这里固定两件事：
 *   1. NlParseResult → JSON → NlParseResult 无损（命令/权限/确认/槽位/路径都要回来）
 *   2. nl_parse_rules_only 的判定与 nl_parse 一致（规则命中 ⟺ source == RULE），
 *      否则客户端会在"其实规则能决定"时白白去问守护进程。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

#include "nlc_api.h"
#include "parse_ipc.h"

static void test_roundtrip(void)
{
    NlParseResult r;
    memset(&r, 0, sizeof(r));
    r.intent = NL_INTENT_CREATE_DIR;
    r.command = strdup("mkdir -p demo");
    r.perm_level = NL_PERM_L2;
    r.source = NL_SOURCE_MODEL;
    r.confidence = 0.87f;
    r.needs_confirm = 1;
    r.slot_count = 1;
    r.slots[0].name = strdup("location");
    r.slots[0].value = strdup("demo");
    r.normalized_paths[0] = strdup("demo");
    r.path_count = 1;
    snprintf(r.debug_info, sizeof(r.debug_info), "[模型推理] intent=create_dir");

    char *json = nl_parse_result_to_json(&r);
    ASSERT(json != NULL, "序列化成功");
    ASSERT(strstr(json, "\"ok\":true") != NULL, "ok=true");
    ASSERT(strstr(json, "mkdir -p demo") != NULL, "命令在 JSON 里");

    NlParseResult back;
    char err[256];
    ASSERT(nl_parse_result_from_json(json, &back, err, sizeof(err)) == 0, "反序列化成功");
    ASSERT(back.command && strcmp(back.command, "mkdir -p demo") == 0, "命令还原");
    ASSERT(back.perm_level == NL_PERM_L2, "权限还原");
    ASSERT(back.needs_confirm == 1, "确认标记还原");
    ASSERT(back.path_count == 1 && strcmp(back.normalized_paths[0], "demo") == 0, "路径还原");
    ASSERT(back.slot_count == 1 && strcmp(back.slots[0].value, "demo") == 0, "槽位还原");

    nl_parse_result_free(&back);
    nl_parse_result_free(&r);
    free(json);
}

static void test_error_response(void)
{
    NlParseResult r;
    char err[256];
    ASSERT(nl_parse_result_from_json("{\"ok\":false,\"error\":\"无法解析\"}", &r, err, sizeof(err)) != 0,
           "ok=false 视为失败");
    ASSERT(strstr(err, "无法解析") != NULL, "错误原因透传");
    ASSERT(nl_parse_result_from_json("not json", &r, err, sizeof(err)) != 0, "非 JSON 失败");
}

static void test_rules_agreement(void)
{
    /* 模型不可用：规则未命中的句子 nl_parse 会失败，正好用来判定"是否规则命中" */
    setenv("NL_MODEL_PATH", "/no/such/model.gguf", 1);
    const char *hits[] = { "列出当前目录", "创建一个叫 demo 的文件夹", "查看当前目录", NULL };
    const char *misses[] = { "显示当前工作目录", "把日志按大小排序后发到远程", NULL };

    for (int i = 0; hits[i]; i++) {
        NlParseResult r;
        int rc = nl_parse_rules_only(hits[i], &r);
        ASSERT(rc == 1, "规则应命中");
        ASSERT(r.command != NULL && r.source == NL_SOURCE_RULE, "命中时 source=RULE 且有命令");
        nl_parse_result_free(&r);
    }
    for (int i = 0; misses[i]; i++) {
        NlParseResult r;
        memset(&r, 0, sizeof(r));
        int rc = nl_parse_rules_only(misses[i], &r);
        ASSERT(rc == 0, "规则不应命中");
        ASSERT(r.command == NULL, "未命中时不给命令");
        nl_parse_result_free(&r);
    }
}

int main(void)
{
    test_roundtrip();
    test_error_response();
    test_rules_agreement();
    if (failures == 0) printf("parse_ipc: 全部通过\n");
    else fprintf(stderr, "parse_ipc: %d 项失败\n", failures);
    return failures == 0 ? 0 : 1;
}
