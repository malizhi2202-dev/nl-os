/*
 * snapshot.h — 快照与撤销（ADR-014 / T15）
 *
 * 约束（写死，可被环境变量放宽以便运维/测试）：
 *   - 仅对调用方传入的路径做快照；单文件/单目录 ≤ 8MB（NL_SNAPSHOT_MAX_FILE 可覆盖）
 *   - 敏感路径一律不落盘（~/.ssh ~/.gnupg ~/.aws ~/.kube / *.pem *.key *.p12 / *secret* *token* *password*）
 *   - 总量上限 200MB（NL_SNAPSHOT_MAX_TOTAL）、保留最近 10 次（NL_SNAPSHOT_KEEP），LRU 淘汰
 *   - 目录与文件权限 0600/0700；写入用 flock 互斥
 */
#ifndef NL_SNAPSHOT_H
#define NL_SNAPSHOT_H

#include <stddef.h>

typedef enum {
    NL_SNAP_OK = 0,
    NL_SNAP_SKIPPED = 1,   /* 超限或含敏感路径：什么都没做 */
    NL_SNAP_ERR = -1
} NlSnapStatus;

/* 为 paths 建一个快照；成功时 out_dir 写入快照目录 */
NlSnapStatus nl_snapshot_take(const char *const *paths, int n, char *out_dir, size_t cap);

/* 撤销第 n 次快照（1 = 最近）；成功返回 0 */
int nl_snapshot_undo(int n, char *err, size_t errcap);

/* 快照根目录（${XDG_STATE_HOME}/nl-os/snapshots） */
const char *nl_snapshot_root(void);

/* 是否命中敏感清单（供测试与其它模块复用） */
int nl_snapshot_is_sensitive(const char *path);

#endif /* NL_SNAPSHOT_H */
