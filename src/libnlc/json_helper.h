/*
 * json_helper.h — cJSON 封装层
 *
 * 提供 NL-OS 内部使用的 JSON 解析/构建辅助函数。
 * 封装 cJSON 的底层 API，统一错误处理。
 */

#ifndef NL_JSON_HELPER_H
#define NL_JSON_HELPER_H

#include <stddef.h>

/* 前向声明 cJSON 类型（避免头文件污染） */
typedef struct cJSON cJSON;

/* JSON 解析结果 */
typedef struct {
    cJSON *root;        /* 根节点（调用者负责 nl_json_free） */
    char  *raw;         /* 原始字符串副本 */
    int    error_line;
    char   error_msg[256];
} NlJsonDoc;

/*
 * 解析 JSON 字符串
 * 成功返回 0，失败返回 -1 并填充 error_msg
 */
int nl_json_parse(const char *json_str, NlJsonDoc *doc);

/*
 * 从 JSON 对象中安全读取字段
 * 返回 0 成功，-1 字段不存在或类型不匹配
 */
int nl_json_get_string(const cJSON *obj, const char *key, const char **out);
int nl_json_get_int(const cJSON *obj, const char *key, int *out);
int nl_json_get_float(const cJSON *obj, const char *key, double *out);
int nl_json_get_bool(const cJSON *obj, const char *key, int *out);

/*
 * 获取数组长度 / 数组元素
 */
int nl_json_array_size(const cJSON *arr);
cJSON *nl_json_array_get(const cJSON *arr, int index);

/*
 * 构建 JSON（用于构造 IPC 请求/响应）
 */
cJSON *nl_json_new_object(void);
cJSON *nl_json_new_array(void);
void nl_json_add_string(cJSON *obj, const char *key, const char *value);
void nl_json_add_int(cJSON *obj, const char *key, int value);
void nl_json_add_float(cJSON *obj, const char *key, double value);
void nl_json_add_bool(cJSON *obj, const char *key, int value);
void nl_json_add_item(cJSON *obj, const char *key, cJSON *item);

/*
 * 序列化 JSON → 字符串
 * 返回 malloc 的字符串，调用者负责 free
 */
char *nl_json_print(const cJSON *obj);

/*
 * 释放 JSON 文档
 */
void nl_json_free(NlJsonDoc *doc);

#endif /* NL_JSON_HELPER_H */
