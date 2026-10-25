/*
 * capability_scan.c — 能力扫描与自检（T01 占位骨架，T03/T04 实现）
 */
#include "capability_scan.h"

static const char *const DEFAULT_DIRS[] = {
    "/usr/bin", "/bin", "/usr/sbin", "/sbin", "/usr/local/bin", "/usr/local/sbin"
};

const char *const *nl_cap_default_dirs(int *count)
{
    if (count) *count = (int)(sizeof(DEFAULT_DIRS) / sizeof(DEFAULT_DIRS[0]));
    return DEFAULT_DIRS;
}

int nl_cap_scan_dirs(const char *const *dirs, int n, NlCapRegistry *out)
{
    (void)dirs; (void)n; (void)out;
    return -1;
}

int nl_cap_selftest(NlCapRegistry *r, NlSelfTestReport *rep)
{
    (void)r; (void)rep;
    return -1;
}
