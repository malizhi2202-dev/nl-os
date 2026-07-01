/*
 * intent_extract.h — 模型推理 Layer 3
 *
 * 构建 prompt → 调用模型推理 → 解析 JSON → Schema 校验 → 返回意图+实体。
 */

#ifndef NL_INTENT_EXTRACT_H
#define NL_INTENT_EXTRACT_H

#include "rule_engine.h"
#include "model_loader.h"
#include "cJSON.h"

/* 推理结果状态 */
typedef enum {
    NL_INFER_OK = 0,
    NL_INFER_LOW_CONFIDENCE = 1,
    NL_INFER_INVALID_JSON = 2,
    NL_INFER_MODEL_ERROR = -1
} NlInferStatus;

/* 意图提取结果 */
typedef struct {
    NlInferStatus  status;
    NlIntentType   intent;
    NlSlot         slots[NL_MAX_SLOTS];
    int            slot_count;
    float          confidence;
    char           raw_output[1024];  /* 模型原始输出（调试用） */
} NlIntentResult;

/*
 * 意图提取
 * 参数:
 *   model  - 模型句柄
 *   input  - 用户 NL 输入
 *   result - 输出结果
 * 返回: 0=成功, -1=失败
 */
int nl_intent_extract(NlModel *model, const char *input, NlIntentResult *result);

/*
 * 释放结果
 */
void nl_intent_result_free(NlIntentResult *result);

/*
 * Schema 校验（公开用于测试）
 * 返回: 0=通过, -1=失败（error 填充错误信息）
 */
int validate_schema(const struct cJSON *root, char *error, size_t error_sz);

#endif /* NL_INTENT_EXTRACT_H */
