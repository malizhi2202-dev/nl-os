/*
 * freeze.h — .tools 固化与校验（ADR-014 / T14）
 *
 * 格式 v2（可选策略头）：
 *   # @format: 2
 *   # @checksum: sha256:<hex>      ← 对「checksum 行之后的全部行」做逐行链式 hash
 *   # @perm: <策略>                ← 可选，策略头（如 allow-l2），参与校验
 *   <正文行…>
 *
 * 策略头放在 checksum 之后、正文之前，既被 hash 覆盖（改了就 TAMPERED），
 * 又不会因为固化而丢失——否则 `# @perm: allow-l2` 在 --freeze 后会静默失效。
 * 对不认识策略头的旧读取方而言，它只是正文里多一行注释，校验和仍然成立。
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

/* 把 body 写成固化文件（覆盖 path）；成功返回 0。内容幂等。
 * policy_line 可为 NULL；非 NULL 时原样写入（必须是 `# …` 注释行）并参与校验。 */
int nl_freeze_write(const char *path, char *const *body, int n_body,
                    const char *policy_line);

/* 校验固化文件：OK / TAMPERED / PLAIN */
NlFreezeStatus nl_freeze_verify(const char *path);

/* 是否为固化文件（有 @checksum 头） */
int nl_freeze_is_frozen(const char *path);

#endif /* NL_FREEZE_H */
