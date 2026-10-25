# SUMMARY: T05 — ticket（一次性 + TTL + args 哈希 + 1024 环形淘汰）

- **Change ID**: `capability-protocol` · **Task ID**: T05 · **完成时间**: 2026-10-25

## 做了什么

1. **内置 SHA-256**（`libnlc/sha256.{h,c}`，98 行，FIPS 180-4）：ticket 的 args 绑定与后续固化校验和共用
2. **`ticket.{h,c}`**：128bit 随机 token（`/dev/urandom`，32 hex）、绑定 `capability_id` + `args_hash`、TTL 60s
3. **四态**：`OK / INVALID / EXPIRED / MISMATCH`（重放与参数不符都归 MISMATCH）
4. **表上限 1024 + 环形淘汰**：覆盖未用且未过期的凭证时累加 `evictions`
5. **每会话限速** 100/s（G2 安全条件）
6. `nl_ticket_reset()` 供测试与 broker 重启使用

## verify

```
ticket tests
  SHA-256 NIST vectors ................ PASS   ← 空串与 "abc" 两个标准向量
  issue then check -> OK .............. PASS
  forged token -> INVALID ............. PASS
  expired -> EXPIRED .................. PASS
  args mismatch -> MISMATCH ........... PASS
  replay -> MISMATCH .................. PASS
  table full -> oldest evicted ........ PASS
  rate limit per second ............... PASS
8/8 passed, 0 failed
```

## 边界 / 遗留

- 改动：`src/libnlc/sha256.{h,c}`、`src/nlbroker/ticket.{h,c}`、`tests/unit/ticket_test.c`、`CMakeLists.txt`、`tests/CMakeLists.txt`
- 全局表是单线程假设（ADR-017 已接受）；v2 若并发需加锁
- `args_hash` 只覆盖 args 文本，不覆盖 capability_id（后者单独比较）
