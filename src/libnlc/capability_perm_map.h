/*
 * capability_perm_map.h — 能力权限等级显式映射表（ADR-012 D12）
 *
 * 原则：表中没有的能力一律 L4（禁止），deny-by-default。
 * 新增命令默认不可用，由 `nlsh --allow <id>` 或改此表显式放行。
 */
#ifndef NL_CAPABILITY_PERM_MAP_H
#define NL_CAPABILITY_PERM_MAP_H

#include <string.h>

typedef struct {
    const char *name;
    int         perm_level;
} NlCapPermEntry;

static const NlCapPermEntry NL_CAP_PERM_MAP[] = {
    /* ── L1 · 只读安全 ── */
    { "ls", 1 }, { "cat", 1 }, { "find", 1 }, { "ps", 1 }, { "grep", 1 },
    { "head", 1 }, { "tail", 1 }, { "wc", 1 }, { "stat", 1 }, { "df", 1 },
    { "du", 1 }, { "whoami", 1 }, { "id", 1 }, { "uname", 1 }, { "date", 1 },
    { "echo", 1 }, { "pwd", 1 }, { "which", 1 }, { "file", 1 }, { "sort", 1 },
    { "uniq", 1 }, { "diff", 1 }, { "free", 1 }, { "uptime", 1 }, { "lsof", 1 },
    /* 网络诊断（ADR-024）：`ip` 能写（ip link set … down），`dig` 能用 `-f` 读任意文件，
     * 所以"按名字放行"本身不足以为它们背书。它们能列在 L1 的前提是 exec 层
     * nl_exec_form_allowed() 只放行登记过的**只读形态**（ip -br addr / dig +short <域名>），
     * 其它形态一律拒绝。改这里的等级前，先看那道形态白名单是否还拦得住。 */
    { "ip", 1 }, { "dig", 1 },
    /* ── L2 · 修改确认 ── */
    { "mkdir", 2 }, { "touch", 2 }, { "cp", 2 }, { "mv", 2 }, { "tee", 2 },
    { "chmod", 2 }, { "ln", 2 }, { "tar", 2 }, { "gzip", 2 }, { "zip", 2 },
    { "curl", 2 }, { "wget", 2 }, { "systemctl", 2 }, { "apt", 2 }, { "dnf", 2 },
    { "opkg", 2 }, { "pip", 2 }, { "git", 2 },
    /* ── L3 · 删除高危 ── */
    { "rm", 3 }, { "rmdir", 3 }, { "dd", 3 }, { "truncate", 3 }, { "kill", 3 },
    { "pkill", 3 }, { "shred", 3 }, { "unlink", 3 },
    /* ── L4 · 禁止执行 ── */
    { "sudo", 4 }, { "su", 4 }, { "mount", 4 }, { "umount", 4 }, { "passwd", 4 },
    { "reboot", 4 }, { "shutdown", 4 }, { "modprobe", 4 }, { "insmod", 4 },
    { "rmmod", 4 }, { "iptables", 4 }, { "nft", 4 }, { "chroot", 4 },
};

/* 表中没有 → 4（L4） */
static inline int nl_cap_perm_for(const char *name)
{
    if (name) {
        size_t n = sizeof(NL_CAP_PERM_MAP) / sizeof(NL_CAP_PERM_MAP[0]);
        for (size_t i = 0; i < n; i++)
            if (strcmp(NL_CAP_PERM_MAP[i].name, name) == 0)
                return NL_CAP_PERM_MAP[i].perm_level;
    }
    return 4;
}

#endif /* NL_CAPABILITY_PERM_MAP_H */
