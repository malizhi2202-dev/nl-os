# SUMMARY: T14 — 全量 AC 验证（AC-1 ~ AC-10）

- **Change ID**: `capability-protocol` · **Task ID**: T14 · **完成时间**: 2026-10-25
- **门禁**: `auto="false"`，执行前已获用户签核

## 执行结果

```
bash scripts/verify_all.sh
  AC-1  discover limit / cursor / prefix filter           PASS
  AC-2  effect 缺省 delete + effect_defaulted             PASS
  AC-3  保护路径 deny / plan 为空 / 审计无对应 commit      PASS
  AC-4  confirm+ticket / 未执行 / 提交后生效 /
        重放拒绝 / 篡改拒绝 / 伪造拒绝                      PASS
  AC-5  伪造 effect=read 仍 deny + 打标记                  PASS
  AC-6  门面隐藏 L3 / 拒 L3 调用 / 执行 L1                 PASS
  AC-7  真实 DBus 调用                                     PASS
  AC-8  https 拒绝 + 建议 cli:curl / http 取回 body        PASS
  AC-9  审计字段齐全 / 无明文参数                           PASS
  AC-10 Release 构建 VmHWM ≤ 80MB / 512MB 限制下正常运行    PASS

==== 结果：0 项失败 ====
```

## 验证脚本的关键设计

1. **AC-3 双断言**：`plan` 为空 **且** 审计里没有对应 `commit` 记录（不依赖 `strace`，符合需求门第三轮结论）
2. **AC-5 是对抗用例**，不是功能用例：构造 `{"effect":"read"}` 伪装删除，必须仍被 deny 且 `untrusted_effect_attempt=true`
3. **AC-10 用 Release 构建**：Debug 带 ASan，RSS 不可比；脚本自动建 `build-rel` 并在 `ulimit -v 524288` 下读 `/proc/PID/status` 的 `VmHWM`
4. AC-7 在无 system bus 时打印 `SKIP` 而不是伪装通过

## 执行中发现并修复的问题

**Release 构建失败**：`audit.c` 里 `(void)write(fd, "\n", 1)` 在 `_FORTIFY_SOURCE` 下触发
`-Werror=unused-result`。Debug 构建不暴露（优化级别不同），**只有 Release 才报**。
修复：消费返回值（`if (write(...) < 0) { /* ignore */ }`）。

> 教训：只跑 Debug 构建的项目，会在第一次 Release 构建时集中爆雷。已写入教训候选。

## 门禁条件核对

| 条件（来自 G1/需求门/G2） | 状态 |
|---|---|
| AC-3 用 plan 标记 + 审计（不用 strace） | ✅ |
| AC-10 用 cgroup/ulimit + VmHWM，不用 x86 直测 | ✅（Release + ulimit） |
| AC-5 对抗脚本进矩阵 | ✅ |
| ticket 表上限 1024 + LRU + 限速 | ✅（T05 测试覆盖） |
| 每个适配器独立 verify | ✅（各自 ctest + 本节全量） |
