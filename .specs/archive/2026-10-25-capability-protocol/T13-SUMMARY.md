# SUMMARY: T13 — http 适配器（beta，仅 http://）

- **Change ID**: `capability-protocol` · **Task ID**: T13 · **完成时间**: 2026-10-25

## 做了什么

1. `adapter_http.{h,c}`：`nl_http_parse_url`（host/port/path，仅 http://；https 返回 1）
   + `nl_http_request`（socket + connect + 手写 HTTP/1.1 + poll 超时 + 跳过响应头）
2. 能力 ID：`http:<METHOD>|<url>`
3. dispatch：https → **`ok:false` + `error:"E_SCHEME_UNSUPPORTED"` + `result.alternatives:["cli:curl"]`**
4. 不引入 libcurl / TLS（维持 ADR-006）

## 实现中修掉的两个问题

**1. 非 cli 能力被注册表拒收**：`parse_cap_line` 强制要求 `path`，而 `http:`/`mcp:` 能力没有真实路径
→ 全部被当成坏行跳过（`E_CAP_UNKNOWN`）。改为：**非 `cli` 命名空间的 `path` 可缺省**，用命名空间占位；
`cli:` 仍必须有真实路径。

**2. 失败响应丢失 `error` 字段**（渲染顺序 bug）：`nl_cip_render` 先判 `payload` 再判 `ok`，
导致"带 payload 的失败"输出成 `{"ok":false,"result":...}` 而**没有 error**。
改为**先处理失败分支**。这个 bug 会让所有"拒绝但附替代方案"的响应丢失错误码。

## verify

```
adapter_http tests
  parse host/port/path ................ PASS
  https returns scheme-unsupported .... PASS
  invalid urls rejected ............... PASS
  live GET against local python server  PASS
4/4 passed, 0 failed        （ctest 20/20）

端到端（broker 内）：
https invoke -> ok:false, error:E_SCHEME_UNSUPPORTED, alternatives:['cli:curl']
http  invoke -> allow, plan:{method:GET, url:http://127.0.0.1:18098/health}
http  commit -> ok, body: hello-http-t13
```

## 边界 / 遗留

- 只支持 GET/POST 空 body（POST 可传 body 但 dispatch 当前传空）——v2 再支持
- 不处理 chunked / gzip；响应体上限 4KB（`NL_CLI_MAX_OUT`）
- 无重定向、无 cookie、无超时重试
