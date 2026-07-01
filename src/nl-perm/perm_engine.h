/*
 * perm_engine.h — L1-L4 权限判定引擎
 */
#ifndef NL_PERM_ENGINE_H
#define NL_PERM_ENGINE_H

#define NL_PERM_L1 1  /* 读安全 · 自动放行 */
#define NL_PERM_L2 2  /* 修改确认 · 按 Enter */
#define NL_PERM_L3 3  /* 删除高危 · 输入 yes */
#define NL_PERM_L4 4  /* 禁止执行 · 硬拦截 */

typedef struct {
    const char *command_type;  /* 操作类型: read/write/delete/execute */
    const char *nl_input;
    const char *generated_cmd;
    char       **paths;
    int          path_count;
    const char *user;
    const char *cwd;
} NlPermRequest;

typedef struct {
    int   decision;      /* 0=allow, 1=confirm, 2=deny */
    int   level;          /* L1-L4 */
    char  reason[256];
    char  audit_id[64];
} NlPermResponse;

int nl_perm_check(const NlPermRequest *req, NlPermResponse *resp);
int nl_perm_reload_config(void);
int nl_perm_load_config(const char *config_path);

#endif
