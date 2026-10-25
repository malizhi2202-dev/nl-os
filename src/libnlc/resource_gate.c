/*
 * resource_gate.c — 加载模型前的资源闸门（见 resource_gate.h 的事故说明）
 */

#include "resource_gate.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MB (1024L * 1024L)

long nl_mem_available_mb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    char line[256];
    long kb = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemAvailable:", 13) == 0) {
            if (sscanf(line + 13, "%ld", &kb) != 1) kb = -1;
            break;
        }
    }
    fclose(f);
    if (kb < 0) return -1;
    return kb / 1024;
}

long nl_model_need_mb(const char *gguf_path)
{
    if (!gguf_path || !*gguf_path) return -1;

    struct stat st;
    if (stat(gguf_path, &st) != 0) return -1;
    if (st.st_size <= 0) return -1;

    long file_mb = (long)((st.st_size + MB - 1) / MB);
    return file_mb * NL_MODEL_RSS_FACTOR;
}

int nl_resource_gate_allow(long need_mb, long avail_mb, long headroom_mb)
{
    /* 任何一个数读不到都不冒险：拒绝加载，降级到规则层 */
    if (need_mb <= 0 || avail_mb <= 0 || headroom_mb < 0) return 0;

    /* 用减法而不是加法：避免 need_mb + headroom_mb 溢出成负数后误判为"够用" */
    return avail_mb - headroom_mb >= need_mb;
}
