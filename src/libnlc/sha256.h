/*
 * sha256.h — 内置 SHA-256（ADR-014 / ADR-016）
 *
 * 用途：ticket 的 args_hash、固化文件校验和。
 * 明确边界：无密钥哈希只能防误改与损坏，不提供认证/签名保证。
 */
#ifndef NL_SHA256_H
#define NL_SHA256_H

#include <stddef.h>
#include <stdint.h>

void nl_sha256(const void *data, size_t len, uint8_t out[32]);
void nl_sha256_hex(const void *data, size_t len, char out[65]);

#endif /* NL_SHA256_H */
