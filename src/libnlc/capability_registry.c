/*
 * capability_registry.c — 能力注册表实现（T01 占位骨架，T02 实现）
 */
#include "capability_registry.h"

NlCapRegistry *nl_cap_create(void) { return NULL; }

NlCapRegistry *nl_cap_load(const char *path, NlCapLoadErr *err)
{
    (void)path;
    if (err) *err = NL_CAP_ERR_IO;
    return NULL;
}

int nl_cap_save_atomic(NlCapRegistry *r, const char *path)
{
    (void)r; (void)path;
    return NL_CAP_ERR_IO;
}

int nl_cap_add(NlCapRegistry *r, const NlCapability *c)
{
    (void)r; (void)c;
    return NL_CAP_ERR_IO;
}

const NlCapability *nl_cap_find(const NlCapRegistry *r, const char *id)
{
    (void)r; (void)id;
    return NULL;
}

int nl_cap_count(const NlCapRegistry *r) { (void)r; return 0; }

const char *nl_cap_default_path(void) { return NULL; }

const char *nl_cap_err_str(NlCapLoadErr e)
{
    switch (e) {
    case NL_CAP_OK:          return "ok";
    case NL_CAP_ERR_IO:      return "io error";
    case NL_CAP_ERR_PARSE:   return "parse error";
    case NL_CAP_ERR_VERSION: return "unsupported format_version";
    case NL_CAP_ERR_LOCK:    return "lock timeout";
    case NL_CAP_ERR_MEM:     return "out of memory";
    default:                 return "unknown";
    }
}

void nl_cap_free(NlCapRegistry *r) { (void)r; }
