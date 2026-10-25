# ADR-010: C11 标准 + GGUF v3 锁定

- **状态**: accepted
- **日期**: 2026-07-01
- **关联决策**: D7

## Context

NL-OS 需要在多个架构（x86_64, ARM64, 未来 RISC-V）上编译和运行。不同架构的交叉编译工具链对 C 标准的支持程度不同。旧 ARM 嵌入式工具链（如 Buildroot 2020）可能不支持 C17/C23。同时，GGUF 格式版本在快速演进，锁定版本可以避免上游格式变更导致的模型不兼容。

## Decision

**C11 标准**（非 C99、C17 或 C23）：
- C11 有 `_Generic`（泛型宏）、`_Static_assert`（编译期断言）、`_Atomic`（原子操作）
- 主流 ARM 交叉编译链（GCC 9+, Clang 14+）完整支持 C11
- 放弃 C17 的 `[[fallthrough]]` / `<stdatomic.h>` 改进，影响极小

**GGUF v3 锁定**：
- 模型加载器只解析 GGUF v3
- 提供 `nl-model-convert` 工具做格式转换（GGUF vN → v3）
- 每次 NL-OS 大版本评估 GGUF 新版本，决定是否升级

## Consequences

- ✅ 最大兼容性：旧 ARM 工具链 + 现代 x86 编译器都能编译
- ✅ GGUF v3 锁定避免上游格式 break
- ✅ `nl-model-convert` 保证社区新模型可用
- ❌ 放弃 C17 的部分语法糖（影响极小）
- ❌ GGUF 格式升级需要主动维护转换工具
