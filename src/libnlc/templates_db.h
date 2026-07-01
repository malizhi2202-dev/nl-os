/*
 * templates_db.h — 命令模板数据库
 *
 * 12 种 intent × 对应命令模板。模板格式：printf 风格格式串。
 * 槽位通过名称匹配填充。
 */

#ifndef NL_TEMPLATES_DB_H
#define NL_TEMPLATES_DB_H

#include "template_fill.h"
#include <stddef.h>

/* 单条模板 */
typedef struct {
    int          template_id;
    NlIntentType intent;
    NlPermLevel  perm_level;
    const char  *template_str;   /* printf 格式串 */
    const char  *slot_names[6];  /* 槽位名列表（按 format 参数顺序） */
    const char  *description;
} NlCmdTemplate;

/* ============================================================
 * 模板表
 * ============================================================ */

static const NlCmdTemplate TEMPLATES[] = {
    /* ── create_dir ── */
    {1,  NL_INTENT_CREATE_DIR, NL_PERM_L2,
     "mkdir -p {name}",        {"name", NULL}, "创建目录"},
    {101,NL_INTENT_CREATE_DIR, NL_PERM_L2,
     "mkdir -p {location}/{name}", {"location", "name", NULL}, "在指定位置创建目录"},

    /* ── create_file ── */
    {2,  NL_INTENT_CREATE_FILE, NL_PERM_L2,
     "touch {name}",           {"name", NULL}, "创建空文件"},
    {201,NL_INTENT_CREATE_FILE, NL_PERM_L2,
     "touch {location}/{name}", {"location", "name", NULL}, "在指定位置创建文件"},

    /* ── delete ── */
    {3,  NL_INTENT_DELETE, NL_PERM_L3,
     "rm -rf {target}",        {"target", NULL}, "删除（高危）"},
    {301,NL_INTENT_DELETE, NL_PERM_L3,
     "rm -rf {location}/{filter_type}", {"location", "filter_type", NULL}, "按条件删除"},

    /* ── move ── */
    {4,  NL_INTENT_MOVE, NL_PERM_L2,
     "mv {source} {dest}",     {"source", "dest", NULL}, "移动/重命名"},
    {401,NL_INTENT_MOVE, NL_PERM_L3,
     "mv -f {source} {dest}",  {"source", "dest", NULL}, "强制覆盖移动"},

    /* ── copy ── */
    {5,  NL_INTENT_COPY, NL_PERM_L2,
     "cp -r {source} {dest}",  {"source", "dest", NULL}, "复制"},
    {501,NL_INTENT_COPY, NL_PERM_L1,
     "cp {source} {dest}",     {"source", "dest", NULL}, "复制（只读源）"},

    /* ── list_dir ── */
    {6,  NL_INTENT_LIST_DIR, NL_PERM_L1,
     "ls -la",                 {NULL},        "列出当前目录"},
    {601,NL_INTENT_LIST_DIR, NL_PERM_L1,
     "ls -la {location}",      {"location", NULL}, "列出指定目录"},

    /* ── view_file ── */
    {7,  NL_INTENT_VIEW_FILE, NL_PERM_L1,
     "cat {target}",           {"target", NULL}, "查看文件"},
    {701,NL_INTENT_VIEW_FILE, NL_PERM_L1,
     "less {target}",          {"target", NULL}, "分页查看"},

    /* ── find_files ── */
    {8,  NL_INTENT_FIND_FILES, NL_PERM_L1,
     "find . -name '{pattern}'", {"pattern", NULL}, "按名称查找"},
    {801,NL_INTENT_FIND_FILES, NL_PERM_L1,
     "find {location} -name '{pattern}' -type f",
                               {"location", "pattern", NULL}, "在指定位置查找"},
    {802,NL_INTENT_FIND_FILES, NL_PERM_L1,
     "find {location} -size +{filter_value}",
                               {"location", "filter_value", NULL}, "按大小查找"},

    /* ── change_dir ── */
    {9,  NL_INTENT_CHANGE_DIR, NL_PERM_L1,
     "cd {location}",          {"location", NULL}, "切换目录"},
    {901,NL_INTENT_CHANGE_DIR, NL_PERM_L1,
     "cd ..",                  {NULL},        "上级目录"},

    /* ── show_process ── */
    {10, NL_INTENT_SHOW_PROCESS, NL_PERM_L1,
     "ps aux",                 {NULL},        "进程列表"},
    {1001,NL_INTENT_SHOW_PROCESS, NL_PERM_L1,
     "ps aux --sort=-%mem | head -20", {NULL}, "内存排序"},
    {1002,NL_INTENT_SHOW_PROCESS, NL_PERM_L1,
     "top -b -n 1",            {NULL},        "系统负载"},

    /* ── search_content ── */
    {11, NL_INTENT_SEARCH_CONTENT, NL_PERM_L1,
     "grep -rn '{pattern}' .", {"pattern", NULL}, "搜索内容"},
    {1101,NL_INTENT_SEARCH_CONTENT, NL_PERM_L1,
     "grep -rn '{pattern}' {location}", {"pattern", "location", NULL}, "在指定位置搜索"},

    /* ── run_cmd ── */
    {12, NL_INTENT_RUN_CMD, NL_PERM_L2,
     "{command}",              {"command", NULL}, "执行命令（透传）"},
    {1201,NL_INTENT_RUN_CMD, NL_PERM_L1,
     "make",                   {NULL},        "编译"},
    {1202,NL_INTENT_RUN_CMD, NL_PERM_L2,
     "make -j$(nproc)",        {NULL},        "并行编译"},
    {1203,NL_INTENT_RUN_CMD, NL_PERM_L2,
     "sudo systemctl restart {command}", {"command", NULL}, "重启服务"},
    {1204,NL_INTENT_RUN_CMD, NL_PERM_L2,
     "sudo systemctl start {command}", {"command", NULL}, "启动服务"},

    /* ── other ── */
    {0,  NL_INTENT_OTHER, NL_PERM_L1,
     NULL,                     {NULL},        "哨兵"},
};

static const int TEMPLATE_COUNT = sizeof(TEMPLATES) / sizeof(TEMPLATES[0]) - 1;

#endif /* NL_TEMPLATES_DB_H */
