/*
 * capability_registry.h — 能力注册表（ADR-012）
 *
 * 唯一真相：本机可执行能力清单。JSON Lines + format_version + flock + 原子写 + 0600。
 * 本文件只定义接口；实现见 capability_registry.c。
 */
#ifndef NL_CAPABILITY_REGISTRY_H
#define NL_CAPABILITY_REGISTRY_H

#include <stddef.h>

#define NL_CAP_FORMAT_VERSION 1
#define NL_CAP_MAX_ID_LEN     128
#define NL_CAP_MAX_SLOTS      6

/* 能力种类 */
typedef enum {
    NL_CAP_KIND_FILE = 0,     /* 普通文件，可信 */
    NL_CAP_KIND_SYMLINK = 1   /* 符号链接，不可信（只列不执行） */
} NlCapKind;

/* 加载错误码 */
typedef enum {
    NL_CAP_OK          = 0,
    NL_CAP_ERR_IO      = -1,
    NL_CAP_ERR_PARSE   = -2,
    NL_CAP_ERR_VERSION = -3,  /* 更高的 format_version，调用方必须降级 */
    NL_CAP_ERR_LOCK    = -4,
    NL_CAP_ERR_MEM     = -5
} NlCapLoadErr;

/* 单条能力 */
typedef struct {
    char       id[NL_CAP_MAX_ID_LEN];
    char       name[NL_CAP_MAX_ID_LEN];
    char      *path;          /* realpath 结果 */
    unsigned long inode;
    NlCapKind  kind;
    int        perm_level;    /* 1..4；未登记能力固定为 4 */
    int        trusted;       /* 0/1 */
    char      *slots[NL_CAP_MAX_SLOTS];
    int        slot_count;
    char      *source;        /* scan | manual | plugin */
} NlCapability;

typedef struct NlCapRegistry NlCapRegistry;

NlCapRegistry            *nl_cap_create(void);
NlCapRegistry            *nl_cap_load(const char *path, NlCapLoadErr *err);
int                       nl_cap_save_atomic(NlCapRegistry *r, const char *path);
int                       nl_cap_add(NlCapRegistry *r, const NlCapability *c);
const NlCapability       *nl_cap_find(const NlCapRegistry *r, const char *id);
int                       nl_cap_count(const NlCapRegistry *r);
int                       nl_cap_skipped(const NlCapRegistry *r); /* 加载时跳过的坏行数 */
const char               *nl_cap_default_path(void);
const char               *nl_cap_err_str(NlCapLoadErr e);
void                      nl_cap_free(NlCapRegistry *r);

#endif /* NL_CAPABILITY_REGISTRY_H */
