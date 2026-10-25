/*
 * freeze.h — .tools 固化与校验（ADR-014 / T14）
 *
 * 格式 v2：
 *   # @format: 2
 *   # @checksum: sha256:<hex>      ← 对「头部之后的全部正文」做逐行链式 hash
 *   <正文行…>
 *
 * 边界（诚实声明）：只存最终哈希，**只能判定是否被改动，无法定位到具体行**。
 */
#ifndef NL_FREEZE_H
#define NL_FREEZE_H

#include <stddef.h>

#define NL_FREEZE_FORMAT 2

typedef enum {
    NL_FREEZE_OK = 0,
    NL_FREEZE_TAMPERED = 1,     /* 校验和不匹配 */
    NL_FREEZE_PLAIN = 2,        /* 没有固化头（旧格式 / 未固化） */
    NL_FREEZE_IO_ERR = -1
} NlFreezeStatus;

/* 把 body 写成固化文件（覆盖 path）；成功返回 0。内容幂等。 */
int nl_freeze_write(const char *path, char *const *body, int n_body);

/* 校验固化文件：OK / TAMPERED / PLAIN */
NlFreezeStatus nl_freeze_verify(const char *path);

/* 是否为固化文件（有 @checksum 头） */
int nl_freeze_is_frozen(const char *path);

#endif /* NL_FREEZE_H */
