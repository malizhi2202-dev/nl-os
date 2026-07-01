/*
 * json_helper.c — cJSON 封装实现
 *
 * 封装 cJSON 的底层 API，提供统一的错误处理和类型安全访问。
 */

#include "json_helper.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================
 * 解析
 * ============================================================ */

int nl_json_parse(const char *json_str, NlJsonDoc *doc)
{
    if (!json_str || !doc) return -1;

    memset(doc, 0, sizeof(NlJsonDoc));
    doc->root = cJSON_Parse(json_str);
    if (!doc->root) {
        const char *err = cJSON_GetErrorPtr();
        if (err) {
            /* 计算错误位置所在行号 */
            doc->error_line = 1;
            for (const char *p = json_str; p < err; p++) {
                if (*p == '\n') doc->error_line++;
            }
            snprintf(doc->error_msg, sizeof(doc->error_msg),
                     "JSON parse error before: %.32s", err);
        } else {
            doc->error_line = 0;
            snprintf(doc->error_msg, sizeof(doc->error_msg),
                     "JSON parse error (unknown location)");
        }
        return -1;
    }
    return 0;
}

/* ============================================================
 * 类型安全访问
 * ============================================================ */

int nl_json_get_string(const cJSON *obj, const char *key, const char **out)
{
    if (!obj || !key || !out) return -1;
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsString(item)) return -1;
    *out = cJSON_GetStringValue(item);
    return 0;
}

int nl_json_get_int(const cJSON *obj, const char *key, int *out)
{
    if (!obj || !key || !out) return -1;
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsNumber(item)) return -1;
    *out = (int)cJSON_GetNumberValue(item);
    return 0;
}

int nl_json_get_float(const cJSON *obj, const char *key, double *out)
{
    if (!obj || !key || !out) return -1;
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsNumber(item)) return -1;
    *out = cJSON_GetNumberValue(item);
    return 0;
}

int nl_json_get_bool(const cJSON *obj, const char *key, int *out)
{
    if (!obj || !key || !out) return -1;
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item) return -1;
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item) ? 1 : 0;
        return 0;
    }
    return -1;
}

int nl_json_array_size(const cJSON *arr)
{
    if (!arr || !cJSON_IsArray(arr)) return 0;
    return cJSON_GetArraySize(arr);
}

cJSON *nl_json_array_get(const cJSON *arr, int index)
{
    if (!arr || !cJSON_IsArray(arr) || index < 0) return NULL;
    return cJSON_GetArrayItem(arr, index);
}

/* ============================================================
 * 构建
 * ============================================================ */

cJSON *nl_json_new_object(void)  { return cJSON_CreateObject(); }
cJSON *nl_json_new_array(void)   { return cJSON_CreateArray(); }

void nl_json_add_string(cJSON *obj, const char *key, const char *value)
{
    if (obj && key && value) cJSON_AddStringToObject(obj, key, value);
}

void nl_json_add_int(cJSON *obj, const char *key, int value)
{
    if (obj && key) cJSON_AddNumberToObject(obj, key, value);
}

void nl_json_add_float(cJSON *obj, const char *key, double value)
{
    if (obj && key) cJSON_AddNumberToObject(obj, key, value);
}

void nl_json_add_bool(cJSON *obj, const char *key, int value)
{
    if (obj && key) cJSON_AddBoolToObject(obj, key, value);
}

void nl_json_add_item(cJSON *obj, const char *key, cJSON *item)
{
    if (obj && key && item) cJSON_AddItemToObject(obj, key, item);
}

char *nl_json_print(const cJSON *obj)
{
    if (!obj) return NULL;
    return cJSON_PrintUnformatted(obj);
}

void nl_json_free(NlJsonDoc *doc)
{
    if (doc) {
        cJSON_Delete(doc->root);
        free(doc->raw);
        memset(doc, 0, sizeof(NlJsonDoc));
    }
}
