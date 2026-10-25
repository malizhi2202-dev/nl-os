/*
 * adapter_cli.h — cli: 执行面（ADR-018 / T06）
 *
 * plan 是 argv 数组，从不经过 shell。执行由 commit 阶段调用。
 */
#ifndef NL_BROKER_ADAPTER_CLI_H
#define NL_BROKER_ADAPTER_CLI_H

#include "capability_registry.h"

#include <stddef.h>

#define NL_CLI_MAX_ARGV 32
#define NL_CLI_MAX_OUT  4096

typedef struct {
    char *argv[NL_CLI_MAX_ARGV + 2];   /* NULL 结尾 */
    char *exe_path;                    /* 注册表里的 realpath */
} NlPlan;

typedef enum {
    NL_CLI_OK = 0,
    NL_CLI_E_BAD_PLAN,
    NL_CLI_E_EXEC,
    NL_CLI_E_TIMEOUT
} NlCliStatus;

typedef struct {
    NlCliStatus status;
    int         exit_code;
    char        output[NL_CLI_MAX_OUT];
} NlCliResult;

int         nl_cli_build_plan(const NlCapability *cap, const char *args_json, NlPlan *plan);
void        nl_cli_plan_free(NlPlan *plan);
int         nl_cli_execute(const NlPlan *plan, int timeout_ms, NlCliResult *res);
const char *nl_cli_status_str(NlCliStatus s);

#endif /* NL_BROKER_ADAPTER_CLI_H */
