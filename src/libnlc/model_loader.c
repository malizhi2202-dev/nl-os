/*
 * model_loader.c — GGUF 模型加载器实现
 *
 * MVP stub 版本（BUILD_STUB_MODEL=ON）:
 *   - 不链接 ggml，返回预设的元信息和固定推理结果
 *   - 供 T01-T05 期间验证构建系统和 API 设计
 *
 * T06 将接入真实 ggml 推理（BUILD_STUB_MODEL=OFF）。
 */

#include "model_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 模型句柄 */
struct NlModel {
    NlModelInfo info;
    int         is_stub;  /* 1 = stub 模式, 0 = 真实 ggml */
    FILE       *file;     /* 模型文件句柄（stub 模式仅检查文件存在） */
};

/* ============================================================
 * Stub 实现
 * ============================================================ */

NlModel *nl_model_init(const char *model_path) {
    if (!model_path) {
        fprintf(stderr, "nl_model_init: model_path is NULL\n");
        return NULL;
    }

    NlModel *model = calloc(1, sizeof(NlModel));
    if (!model) return NULL;
    model->is_stub = 1;

    /* 检查文件是否存在 */
    model->file = fopen(model_path, "rb");
    if (!model->file) {
        fprintf(stderr, "nl_model_init: cannot open '%s'\n", model_path);
        free(model);
        return NULL;
    }

    /* 读取 GGUF 头验证魔数 */
    char magic[4];
    if (fread(magic, 1, 4, model->file) == 4) {
        if (memcmp(magic, "GGUF", 4) != 0 && memcmp(magic, "gguf", 4) != 0) {
            fprintf(stderr, "nl_model_init: invalid GGUF magic\n");
            fclose(model->file);
            free(model);
            return NULL;
        }
    }

    /* 获取文件大小 */
    fseek(model->file, 0, SEEK_END);
    model->info.model_size = ftell(model->file);
    rewind(model->file);

    /* 填充预设元信息（stub 固定值，T06 从 GGUF metadata 读取） */
    snprintf(model->info.arch, sizeof(model->info.arch), "stub");
    model->info.vocab_size = 32000;
    model->info.hidden_size = 2048;
    model->info.num_layers = 24;
    model->info.num_heads = 16;
    snprintf(model->info.gguf_version, sizeof(model->info.gguf_version), "v3");

    return model;
}

const NlModelInfo *nl_model_get_info(const NlModel *model) {
    if (!model) return NULL;
    return &model->info;
}

int nl_model_infer(NlModel *model, const char *prompt, char *output, size_t max_len) {
    if (!model || !output || max_len == 0) return -1;

    /* Stub: 返回固定 JSON（模拟意图分类结果，置信度足够高以通过校验） */
    const char *stub_response =
        "{\"intent\":\"create_dir\","
        "\"entities\":{\"target\":\"test_dir\",\"location\":\".\"},"
        "\"confidence\":0.85}";

    size_t len = strlen(stub_response);
    if (len >= max_len) len = max_len - 1;
    memcpy(output, stub_response, len);
    output[len] = '\0';

    /* 抑制 unused 警告 */
    (void)prompt;
    return (int)len;
}

void nl_model_free(NlModel *model) {
    if (!model) return;
    if (model->file) fclose(model->file);
    free(model);
}
