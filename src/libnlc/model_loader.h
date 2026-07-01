/*
 * model_loader.h — GGUF 模型加载器
 *
 * 负责加载 GGUF v3 格式的量化模型文件，管理 mmap 内存映射。
 * MVP stub 版本返回固定数据；T06 接入真实 ggml 推理。
 */

#ifndef NL_MODEL_LOADER_H
#define NL_MODEL_LOADER_H

#include <stddef.h>

/* 模型元信息 */
typedef struct {
    char   arch[64];       /* 模型架构 (如 "qwen2", "gemma2") */
    int    vocab_size;     /* 词表大小 */
    int    hidden_size;    /* 隐藏层维度 */
    int    num_layers;     /* 层数 */
    int    num_heads;      /* 注意力头数 */
    size_t model_size;     /* 模型文件大小（字节） */
    char   gguf_version[16]; /* GGUF 版本 */
} NlModelInfo;

/* 模型句柄（opaque） */
typedef struct NlModel NlModel;

/*
 * 初始化模型
 * 参数:
 *   model_path - GGUF 模型文件路径
 * 返回: 模型句柄，失败返回 NULL
 */
NlModel *nl_model_init(const char *model_path);

/*
 * 获取模型元信息
 * 返回: 模型信息（调用者不需释放）
 */
const NlModelInfo *nl_model_get_info(const NlModel *model);

/*
 * 模型推理
 * 参数:
 *   model  - 模型句柄
 *   prompt - 输入 prompt 字符串
 *   output - 输出缓冲区
 *   max_len- 输出缓冲区最大长度
 * 返回: 实际输出的 token 数，失败返回 -1
 */
int nl_model_infer(NlModel *model, const char *prompt, char *output, size_t max_len);

/*
 * 释放模型资源
 */
void nl_model_free(NlModel *model);

#endif /* NL_MODEL_LOADER_H */
