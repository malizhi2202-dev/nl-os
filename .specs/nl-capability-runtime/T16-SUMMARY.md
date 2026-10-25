# SUMMARY: T16 — 审计回放 CLI

- **Change ID**: `nl-capability-runtime` · **Task ID**: T16 · **完成时间**: 2026-10-25

## 做了什么

`nlsh --audit [--since N] [--limit N]`：直接复用 `nl_audit_query`（capability-protocol 已实现），
输出 JSON 数组（最近 N 条、按时间正序）。

## verify

```
scripts/verify_segment3.sh
  AC-16 输出 JSON 数组 ....... PASS
  AC-16 含 audit_id 字段 ...... PASS
```

样例输出：
```
[{"audit_id":"829f56829d95fd2e","ts":1792931096,"capability_id":"fastpath","verdict":"allow",
  "effect":"read","perm_level":0,"fast_path":true,"probability":-1,"duration_us":2498,"result":"ok",...
```

## 边界 / 遗留

- 无导出格式选择（JSON 直出，`jq` 可自行转 CSV）
- 受 `payload_json` 8KB 限制，单次约 30 条；更多用 `--since` 翻页
