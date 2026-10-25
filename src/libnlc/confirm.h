/*
 * confirm.h — 统一确认门（ADR-019）
 *
 * 此前 nlsh 自然语言路径、nl-run .tools 路径、nlsh 快路径各有一套语义：
 * 触发条件不同、接受词不同（`y` 在 nl-run 里等于取消）、绕过开关不同
 * （NL_ASSUME_YES vs # @perm: allow-l2）、取消后的退出码不同。
 * 本模块把"要不要问、怎么算同意、能不能跳过"收敛成一个实现。
 */
#ifndef NL_CONFIRM_H
#define NL_CONFIRM_H

/* 取消的统一退出码（成功 0 / 失败或拒绝 1 / 用户取消 3） */
#define NL_CONFIRM_EXIT_CANCELLED 3

/* 统一触发条件：写操作或 L2 及以上才问；只读命令不问 */
int nl_confirm_required(int needs_confirm, int perm_level);

/* 统一回答词表：y / Y / yes / YES / 是（前缀匹配）；空行与非以上 = 取消 */
int nl_confirm_yes(const char *answer);

/* 有没有被显式跳过：NL_ASSUME_YES=1（所有路径通用）；nlsh --yes / nl-run --yes 亦设此项 */
int nl_confirm_assume_yes(void);

/*
 * 问一次。返回 1 = 继续执行，0 = 取消（含 EOF：非 TTY 无输入时按取消处理，fail-closed）。
 * cmd 只用于展示；perm_level 用于展示 L 级别。
 */
int nl_confirm_ask(const char *cmd, int perm_level);

#endif /* NL_CONFIRM_H */
