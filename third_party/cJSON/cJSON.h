/*
 * cJSON.h — 兼容层头文件
 *
 * NL-OS 使用内置的 mini-json 实现（见 cJSON.c），
 * 保持与标准 cJSON API 兼容，以便未来替换为完整版本。
 *
 * 支持的 API 子集：
 *   - cJSON_Parse / cJSON_Delete
 *   - cJSON_GetErrorPtr
 *   - cJSON_GetObjectItem
 *   - cJSON_IsString / IsNumber / IsBool / IsTrue / IsFalse / IsArray
 *   - cJSON_GetStringValue / GetNumberValue
 *   - cJSON_CreateObject / CreateArray / CreateString / CreateNumber
 *   - cJSON_AddStringToObject / AddNumberToObject / AddBoolToObject
 *   - cJSON_AddItemToObject / AddItemToArray
 *   - cJSON_PrintUnformatted
 *   - cJSON_GetArraySize / GetArrayItem
 */
#ifndef CJSON_H
#define CJSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* cJSON 类型 */
#define cJSON_Invalid 0
#define cJSON_False   1
#define cJSON_True    2
#define cJSON_NULL    3
#define cJSON_Number  4
#define cJSON_String  5
#define cJSON_Array   6
#define cJSON_Object  7

typedef struct cJSON {
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;
    int           type;
    char         *valuestring;
    int           valueint;
    double        valuedouble;
    char         *string;
} cJSON;

/* 解析 */
cJSON  *cJSON_Parse(const char *value);
const char *cJSON_GetErrorPtr(void);
void   cJSON_Delete(cJSON *c);

/* 访问 */
cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string);
int    cJSON_GetArraySize(const cJSON *array);
cJSON *cJSON_GetArrayItem(const cJSON *array, int index);

/* 类型检查 */
#define cJSON_IsString(item) ((item) && (item)->type == cJSON_String)
#define cJSON_IsNumber(item) ((item) && (item)->type == cJSON_Number)
#define cJSON_IsBool(item)   ((item) && ((item)->type == cJSON_True || (item)->type == cJSON_False))
#define cJSON_IsTrue(item)   ((item) && (item)->type == cJSON_True)
#define cJSON_IsFalse(item)  ((item) && (item)->type == cJSON_False)
#define cJSON_IsArray(item)  ((item) && (item)->type == cJSON_Array)
#define cJSON_IsObject(item) ((item) && (item)->type == cJSON_Object)

/* 取值 */
char   *cJSON_GetStringValue(const cJSON *item);
double  cJSON_GetNumberValue(const cJSON *item);

/* 创建 */
cJSON *cJSON_CreateObject(void);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateString(const char *string);
cJSON *cJSON_CreateNumber(double num);
cJSON *cJSON_CreateNull(void);

/* 添加（到对象） */
cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *string);
cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double number);
cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int b);
cJSON *cJSON_AddNullToObject(cJSON *object, const char *name);
void   cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);

/* 添加（到数组） */
void cJSON_AddItemToArray(cJSON *array, cJSON *item);

/* 序列化 */
char *cJSON_PrintUnformatted(const cJSON *item);

#ifdef __cplusplus
}
#endif

#endif /* CJSON_H */
