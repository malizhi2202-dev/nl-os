/*
 * path_safety.h — 安全网关 Layer 1
 *
 * 在所有 NL 处理之前运行。硬编码规则，不依赖 ML 模型。
 * L4 黑名单命中 → 直接拒绝，不生成命令。
 */

#ifndef NL_PATH_SAFETY_H
#define NL_PATH_SAFETY_H

#include <stddef.h>

/* 路径安全检查结果 */
typedef enum {
    NL_PATH_PASS = 0,
    NL_PATH_REJECT_L4 = 1,
    NL_PATH_REJECT_WHITE = 2,
    NL_PATH_ERROR = -1
} NlPathStatus;

typedef struct {
    NlPathStatus status;
    char *normalized_path;
    char  reason[256];
} NlPathResult;

typedef struct {
    char *cleaned_input;
    int   chars_stripped;
    char  warning[256];
} NlSanitizeResult;

/* 路径规范化 + 白名单 + 黑名单 */
NlPathResult nl_path_check(const char *path, int allow_write);

/* 输入净化：剥离零宽字符/bidi/ANSI/控制字符 */
NlSanitizeResult nl_input_sanitize(const char *input);

/* 从 NL 输入提取路径关键词（返回 NULL 结尾的数组） */
char **nl_extract_paths(const char *nl_input);
void   nl_free_paths(char **paths);
void   nl_path_result_free(NlPathResult *r);
void   nl_sanitize_result_free(NlSanitizeResult *r);

#endif
