/*
 * shell_builtin.h — shell 内建命令（cd）的"父进程执行"通道（ADR-020）
 *
 * nlsh 在子进程执行命令，`cd` 因此改不了父 shell 的目录。内建命令改为：
 * nlsh 只把结果写成 `__NL_CD__ <dir>` 指令，由 shell 钩子在父 shell 执行 `cd`。
 */

#ifndef NL_SHELL_BUILTIN_H
#define NL_SHELL_BUILTIN_H

#include <stddef.h>

/* argv0 是否是受支持的 shell 内建（当前只有 cd） */
int nl_shell_builtin_is(const char *argv0);

/*
 * 把一条内建命令交给父 shell。
 * 成功返回 0（指令已写入 $NL_SHELL_DIRECTIVE）；
 * 失败返回 -1，err 里给出人能看懂的原因（err 可传 NULL）。
 */
int nl_shell_builtin_emit(const char *command, char *err, size_t errsz);

#endif /* NL_SHELL_BUILTIN_H */
