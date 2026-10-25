# T04b · 行编辑器支持 UTF-8 / 非 TTY（扩范围）

- **状态**: ✅ 完成
- **为什么进来**: 接执行后必须验 REPL 路径，实测 `printf '创建一个叫 from-repl 的文件夹\n' | nlsh` 得到 `" 的文件夹 "`——`line_edit.c` 只接受 `32..126` 的 ASCII 字节，中文整段被丢；同一缺陷也让**交互式**中文输入无法工作
- **write_files**: `src/nlsh/line_edit.c`
- **实现**: ① `!isatty(0)` 走 `fgets` 读一行（不进 raw 模式）；② TTY 分支接受 `>=128` 的 UTF-8 字节；③ 退格按「一个完整 UTF-8 字符」删除（跳过续字节 0b10xxxxxx）
- **verify**:
  ```
  $ printf '创建一个叫 from-repl 的文件夹\n' | NL_ASSUME_YES=1 ./build/nlsh --no-model
  → mkdir -p from-repl
  ✅ 完成（11 ms）
  $ test -d from-repl && echo OK      # OK
  ```
- **未覆盖**: 输入法组合串（IME pending）光标处理
