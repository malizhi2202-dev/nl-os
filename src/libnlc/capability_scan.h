/*
 * capability_scan.h — 能力自发现 / 自检（ADR-012）
 *
 * dirs 由调用方传入：CLI 传硬编码白名单，测试传临时目录（不为测试开后门）。
 */
#ifndef NL_CAPABILITY_SCAN_H
#define NL_CAPABILITY_SCAN_H

#include "capability_registry.h"

typedef struct {
    int usable;
    int needs_privilege;
    int missing;
    int total;
} NlSelfTestReport;

/* 默认白名单目录（CLI 使用）：返回编译期常量数组 */
const char *const *nl_cap_default_dirs(int *count);

/* 扫描 dirs 中的能力，追加到 out；目录不可读只记 warning，返回 0 */
int nl_cap_scan_dirs(const char *const *dirs, int n, NlCapRegistry *out);

/* 只读自检，三档分类；探测不写任何文件 */
int nl_cap_selftest(NlCapRegistry *r, NlSelfTestReport *rep);

#endif /* NL_CAPABILITY_SCAN_H */
