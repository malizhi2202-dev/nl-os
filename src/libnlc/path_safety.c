/*
 * path_safety.c — 安全网关 Layer 1 实现
 *
 * 职责：在所有 NL 管线之前运行，拦截危险路径。
 * 原则：硬编码规则，不依赖 ML，不可绕过。
 */

#include "path_safety.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <ctype.h>

/* ============================================================
 * 白名单：仅这些目录允许写操作
 * ============================================================ */
static const char *WHITELIST[] = {
    "/home/",
    "/tmp/",
    "/var/tmp/",
    "/opt/",
    "/mnt/",
    NULL
};

/* ============================================================
 * 黑名单：L4 硬拦截，不可绕过
 * ============================================================ */
static const char *BLACKLIST_PREFIX[] = {
    "/etc",
    "/boot",
    "/sys",
    "/proc",
    "/dev/sd",
    "/dev/nvme",
    "/dev/mmcblk",
    "/dev/dm-",
    "/dev/loop",
    "/dev/vd",
    "/lib",
    "/lib64",
    "/usr/lib",
    "/usr/lib64",
    "/root",
    "/run/systemd",
    "/snap",
    NULL
};

/* 精确黑名单文件 */
static const char *BLACKLIST_EXACT[] = {
    "/",
    "/etc/passwd",
    "/etc/shadow",
    "/etc/sudoers",
    NULL
};

/* ============================================================
 * 路径规范化
 * ============================================================ */
static char *resolve_path(const char *path) {
    if (!path) return NULL;

    /* 展开 ~ */
    char expanded[PATH_MAX];
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = "/root";
        snprintf(expanded, sizeof(expanded), "%s%s", home, path + 1);
    } else {
        strncpy(expanded, path, sizeof(expanded) - 1);
        expanded[sizeof(expanded) - 1] = '\0';
    }

    /* realpath 解析符号链接 + ../ + // */
    char *resolved = realpath(expanded, NULL);
    if (!resolved) {
        /* realpath 失败（路径不存在）→ 手动规范化 ../ 和 ./ */
        /* 使用分段栈：按 / 分割，.. 出栈，. 跳过 */
        char *segments[PATH_MAX / 2];
        int seg_count = 0;
        char *work = strdup(expanded);
        if (!work) return NULL;

        char *token = strtok(work, "/");
        while (token) {
            if (strcmp(token, "..") == 0) {
                if (seg_count > 0) seg_count--;
            } else if (strcmp(token, ".") != 0 && strlen(token) > 0) {
                segments[seg_count++] = token;
            }
            token = strtok(NULL, "/");
        }

        /* 重建路径（在 free(work) 之前完成，因为 segments 指向 work 内部）*/
        if (seg_count == 0) {
            resolved = strdup("/");
        } else {
            size_t len = 1;
            for (int i = 0; i < seg_count; i++) len += strlen(segments[i]) + 1;
            resolved = malloc(len + 1);
            if (resolved) {
                resolved[0] = '/';
                char *dst = resolved + 1;
                for (int i = 0; i < seg_count; i++) {
                    size_t n = strlen(segments[i]);
                    memcpy(dst, segments[i], n);
                    dst += n;
                    if (i < seg_count - 1) *dst++ = '/';
                }
                *dst = '\0';
            }
        }
        free(work);
    }
    return resolved;
}

/* ============================================================
 * 白名单检查
 * ============================================================ */
static int check_whitelist(const char *normalized) {
    if (!normalized) return 0;
    for (int i = 0; WHITELIST[i]; i++) {
        if (strncmp(normalized, WHITELIST[i], strlen(WHITELIST[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ============================================================
 * 黑名单检查
 * ============================================================ */
static int check_blacklist(const char *normalized, char *reason, size_t reason_sz) {
    if (!normalized) return 0;

    /* 精确匹配 */
    for (int i = 0; BLACKLIST_EXACT[i]; i++) {
        if (strcmp(normalized, BLACKLIST_EXACT[i]) == 0) {
            snprintf(reason, reason_sz,
                "目标路径 '%s' 在保护名单中（系统关键路径）", normalized);
            return 1;
        }
    }

    /* 前缀匹配 */
    for (int i = 0; BLACKLIST_PREFIX[i]; i++) {
        if (strncmp(normalized, BLACKLIST_PREFIX[i], strlen(BLACKLIST_PREFIX[i])) == 0) {
            snprintf(reason, reason_sz,
                "目标路径 '%s' 在保护名单中（前缀匹配 '%s'）",
                normalized, BLACKLIST_PREFIX[i]);
            return 1;
        }
    }

    /* 额外检查：路径自身是块设备文件 */
    /* stat + S_ISBLK 太贵，用前缀 /dev/ 兜底 */
    if (strncmp(normalized, "/dev/", 5) == 0) {
        /* /dev/null, /dev/zero, /dev/random 等允许读 */
        /* 但写操作需要额外检查 */
        snprintf(reason, reason_sz,
            "目标路径 '%s' 在 /dev 下，写操作被禁止", normalized);
        return 1;
    }

    return 0;
}

/* ============================================================
 * 公共 API
 * ============================================================ */

NlPathResult nl_path_check(const char *path, int allow_write) {
    NlPathResult result;
    memset(&result, 0, sizeof(result));
    result.status = NL_PATH_ERROR;

    if (!path || !*path) {
        snprintf(result.reason, sizeof(result.reason), "路径为空");
        return result;
    }

    /* Step 1: 规范化 */
    result.normalized_path = resolve_path(path);
    if (!result.normalized_path) {
        snprintf(result.reason, sizeof(result.reason),
            "无法解析路径 '%s'", path);
        return result;
    }

    /* Step 2: 黑名单（仅写操作拦截——读操作允许通过）*/
    if (allow_write && check_blacklist(result.normalized_path,
                        result.reason, sizeof(result.reason))) {
        result.status = NL_PATH_REJECT_L4;
        return result;
    }

    /* Step 3: 白名单（仅写操作检查）*/
    if (allow_write && !check_whitelist(result.normalized_path)) {
        result.status = NL_PATH_REJECT_WHITE;
        snprintf(result.reason, sizeof(result.reason),
            "路径 '%s' 不在可写白名单中（仅 /home /tmp /var /opt /mnt 允许写操作）",
            result.normalized_path);
        return result;
    }

    result.status = NL_PATH_PASS;
    return result;
}

/* ============================================================
 * 输入净化
 * ============================================================ */

NlSanitizeResult nl_input_sanitize(const char *input) {
    NlSanitizeResult result;
    memset(&result, 0, sizeof(result));

    if (!input) {
        snprintf(result.warning, sizeof(result.warning), "输入为 NULL");
        return result;
    }

    size_t len = strlen(input);
    result.cleaned_input = malloc(len + 1);
    if (!result.cleaned_input) return result;

    size_t out = 0;
    int has_warning = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)input[i];

        /* 控制字符（保留 \t \n \r） */
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            result.chars_stripped++;
            has_warning = 1;
            continue;
        }

        /* DEL */
        if (c == 0x7f) {
            result.chars_stripped++;
            has_warning = 1;
            continue;
        }

        /* UTF-8 多字节序列 */
        if (c >= 0x80) {
            int seq_len = 1;
            if ((c & 0xE0) == 0xC0) seq_len = 2;
            else if ((c & 0xF0) == 0xE0) seq_len = 3;
            else if ((c & 0xF8) == 0xF0) seq_len = 4;

            /* 检查是否零宽字符 U+200B-U+200D, U+FEFF */
            if (seq_len == 3 && c == 0xE2 && (unsigned char)input[i+1] == 0x80) {
                unsigned char c2 = (unsigned char)input[i+2];
                if (c2 >= 0x8B && c2 <= 0x8D) {
                    /* U+200B-U+200D: 零宽空格/不连字/连字 */
                    result.chars_stripped += seq_len;
                    has_warning = 1;
                    i += seq_len - 1;
                    continue;
                }
            }
            if (seq_len == 3 && c == 0xEF && (unsigned char)input[i+1] == 0xBB &&
                (unsigned char)input[i+2] == 0xBF) {
                /* U+FEFF: BOM / 零宽不换行空格 */
                result.chars_stripped += seq_len;
                has_warning = 1;
                i += seq_len - 1;
                continue;
            }

            /* 检查是否 bidi 控制字符 U+200E-U+200F, U+202A-U+202E, U+2066-U+2069 */
            if (seq_len == 3 && c == 0xE2 && (unsigned char)input[i+1] == 0x80) {
                unsigned char c2 = (unsigned char)input[i+2];
                if (c2 == 0x8E || c2 == 0x8F || (c2 >= 0xAA && c2 <= 0xAE)) {
                    result.chars_stripped += seq_len;
                    has_warning = 1;
                    i += seq_len - 1;
                    continue;
                }
            }
            if (seq_len == 3 && c == 0xE2 && (unsigned char)input[i+1] == 0x81 &&
                (unsigned char)input[i+2] >= 0xA6 &&
                (unsigned char)input[i+2] <= 0xA9) {
                /* U+2066-U+2069: bidi 隔离 */
                result.chars_stripped += seq_len;
                has_warning = 1;
                i += seq_len - 1;
                continue;
            }

            /* 复制完整的 UTF-8 序列 */
            for (int j = 0; j < seq_len && (i + j) < len; j++) {
                result.cleaned_input[out++] = input[i + j];
            }
            i += seq_len - 1;
            continue;
        }

        /* ASCII 可打印字符 + 空白直接复制 */
        result.cleaned_input[out++] = c;
    }
    result.cleaned_input[out] = '\0';

    if (has_warning && result.chars_stripped > 0) {
        snprintf(result.warning, sizeof(result.warning),
            "⚠️ 输入包含 %d 个不可见字符，已自动清理", result.chars_stripped);
    }

    return result;
}

/* ============================================================
 * 路径提取
 * ============================================================ */

char **nl_extract_paths(const char *nl_input) {
    if (!nl_input) return NULL;

    /* 简单实现：按空格和引号分割，收集看起来像路径的 token */
    size_t cap = 16;
    char **paths = calloc(cap + 1, sizeof(char *));
    if (!paths) return NULL;
    size_t count = 0;

    const char *p = nl_input;
    while (*p) {
        /* 跳过非路径字符 */
        while (*p && *p != '/' && *p != '~' && *p != '.' && !isalnum((unsigned char)*p))
            p++;
        if (!*p) break;

        /* 收集 token */
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;

        size_t tok_len = p - start;
        if (tok_len > 0 && tok_len < PATH_MAX) {
            char *token = strndup(start, tok_len);
            if (token) {
                /* 只保留看起来像路径的 */
                if (token[0] == '/' || token[0] == '~' ||
                    (token[0] == '.' && token[1] == '/') ||
                    strchr(token, '/')) {
                    if (count >= cap) {
                        cap *= 2;
                        paths = realloc(paths, (cap + 1) * sizeof(char *));
                    }
                    paths[count++] = token;
                } else {
                    free(token);
                }
            }
        }
    }
    paths[count] = NULL;
    return paths;
}

void nl_free_paths(char **paths) {
    if (!paths) return;
    for (int i = 0; paths[i]; i++) free(paths[i]);
    free(paths);
}

void nl_path_result_free(NlPathResult *r) {
    if (r) free(r->normalized_path);
}

void nl_sanitize_result_free(NlSanitizeResult *r) {
    if (r) free(r->cleaned_input);
}
