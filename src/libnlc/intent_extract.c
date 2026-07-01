/*
 * intent_extract.c — 模型推理 Layer 3 实现
 *
 * 构建 prompt → 调用模型 → 解析 JSON → Schema 校验 → 提取意图+实体。
 */

#include "intent_extract.h"
#include "few_shot.h"
#include "json_helper.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RETRIES 3
#define LOW_CONFIDENCE_THRESHOLD 0.6f

/* intent 名称 → 枚举映射 */
static const struct {
    const char *name;
    NlIntentType type;
} INTENT_MAP[] = {
    {"create_file",   NL_INTENT_CREATE_FILE},
    {"create_dir",    NL_INTENT_CREATE_DIR},
    {"delete",        NL_INTENT_DELETE},
    {"move",          NL_INTENT_MOVE},
    {"copy",          NL_INTENT_COPY},
    {"list_dir",      NL_INTENT_LIST_DIR},
    {"view_file",     NL_INTENT_VIEW_FILE},
    {"find_files",    NL_INTENT_FIND_FILES},
    {"change_dir",    NL_INTENT_CHANGE_DIR},
    {"show_process",  NL_INTENT_SHOW_PROCESS},
    {"search_content",NL_INTENT_SEARCH_CONTENT},
    {"run_cmd",       NL_INTENT_RUN_CMD},
    {"other",         NL_INTENT_OTHER},
    {NULL, 0}
};

static NlIntentType parse_intent(const char *name) {
    if (!name) return NL_INTENT_OTHER;
    for (int i = 0; INTENT_MAP[i].name; i++)
        if (strcmp(name, INTENT_MAP[i].name) == 0)
            return INTENT_MAP[i].type;
    return NL_INTENT_OTHER;
}

/* Schema 校验：检查必需字段 */
int validate_schema(const cJSON *root, char *error, size_t error_sz) {
    if (!root || !cJSON_IsObject(root)) {
        snprintf(error, error_sz, "root is not an object");
        return -1;
    }

    /* intent 必需且为字符串 */
    const char *intent_str = NULL;
    if (nl_json_get_string(root, "intent", &intent_str) != 0) {
        snprintf(error, error_sz, "missing or invalid 'intent' field");
        return -1;
    }

    /* 验证 intent 值 */
    int valid = 0;
    for (int i = 0; INTENT_MAP[i].name; i++) {
        if (strcmp(intent_str, INTENT_MAP[i].name) == 0) { valid = 1; break; }
    }
    if (!valid) {
        snprintf(error, error_sz, "unknown intent '%s'", intent_str);
        return -1;
    }

    /* confidence 必需且为数字 */
    const cJSON *conf = cJSON_GetObjectItem(root, "confidence");
    if (!conf || !cJSON_IsNumber(conf)) {
        snprintf(error, error_sz, "missing or invalid 'confidence' field");
        return -1;
    }

    return 0;
}

/* 从 JSON 中提取实体 */
static int extract_entities(const cJSON *entities_obj, NlIntentResult *result) {
    if (!entities_obj || !cJSON_IsObject(entities_obj)) return 0;

    static const char *entity_keys[] = {"target", "location", "condition",
                                         "filter_type", "filter_value", NULL};
    for (int i = 0; entity_keys[i]; i++) {
        const char *val = NULL;
        if (nl_json_get_string(entities_obj, entity_keys[i], &val) == 0 && val[0]) {
            if (result->slot_count < NL_MAX_SLOTS) {
                result->slots[result->slot_count].name = strdup(entity_keys[i]);
                result->slots[result->slot_count].value = strdup(val);
                result->slot_count++;
            }
        }
    }
    return 0;
}

/* ============================================================
 * 公共 API
 * ============================================================ */

int nl_intent_extract(NlModel *model, const char *input, NlIntentResult *result) {
    if (!model || !input || !result) return -1;
    memset(result, 0, sizeof(NlIntentResult));

    /* 构建完整 prompt */
    const char *system = nl_system_prompt();
    const char *fewshot = nl_few_shot_examples();

    size_t prompt_len = strlen(system) + strlen(fewshot) + strlen(input) + 128;
    char *prompt = malloc(prompt_len);
    if (!prompt) return -1;
    snprintf(prompt, prompt_len,
             "%s\n%s\n用户: %s\n输出: ", system, fewshot, input);

    /* 推理 + 重试 */
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        char output[2048];
        int len = nl_model_infer(model, prompt, output, sizeof(output));
        if (len <= 0) {
            result->status = NL_INFER_MODEL_ERROR;
            free(prompt);
            return -1;
        }

        /* 保存原始输出（截断到缓冲区大小） */
        size_t cp = (size_t)len < sizeof(result->raw_output) - 1
                    ? (size_t)len : sizeof(result->raw_output) - 1;
        memcpy(result->raw_output, output, cp);
        result->raw_output[cp] = '\0';

        /* 解析 JSON */
        NlJsonDoc doc;
        if (nl_json_parse(output, &doc) != 0) {
            if (retry + 1 < MAX_RETRIES) continue;
            result->status = NL_INFER_INVALID_JSON;
            free(prompt);
            return -1;
        }

        /* Schema 校验 */
        char schema_error[256];
        if (validate_schema(doc.root, schema_error, sizeof(schema_error)) != 0) {
            nl_json_free(&doc);
            if (retry + 1 < MAX_RETRIES) continue;
            result->status = NL_INFER_INVALID_JSON;
            free(prompt);
            return -1;
        }

        /* 提取 intent */
        const char *intent_str = NULL;
        nl_json_get_string(doc.root, "intent", &intent_str);
        result->intent = parse_intent(intent_str);

        /* 提取 confidence */
        const cJSON *conf = cJSON_GetObjectItem(doc.root, "confidence");
        result->confidence = (float)cJSON_GetNumberValue(conf);

        /* 提取 entities */
        const cJSON *entities = cJSON_GetObjectItem(doc.root, "entities");
        extract_entities(entities, result);

        nl_json_free(&doc);

        /* 低置信度检查 */
        if (result->confidence < LOW_CONFIDENCE_THRESHOLD) {
            result->status = NL_INFER_LOW_CONFIDENCE;
            free(prompt);
            return -1;
        }

        result->status = NL_INFER_OK;
        free(prompt);
        return 0;
    }

    /* 不应到达这里 */
    result->status = NL_INFER_INVALID_JSON;
    free(prompt);
    return -1;
}

void nl_intent_result_free(NlIntentResult *result) {
    if (!result) return;
    for (int i = 0; i < result->slot_count; i++) {
        free(result->slots[i].name);
        free(result->slots[i].value);
    }
    memset(result, 0, sizeof(NlIntentResult));
}
