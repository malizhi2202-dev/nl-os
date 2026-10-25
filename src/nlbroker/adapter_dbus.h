/*
 * adapter_dbus.h — dbus: 执行面（ADR-018 / T09、T10）
 *
 * v1 通过 busctl 子进程桥，不引入 libdbus；仅支持无参方法调用。
 * 能力 ID 形态：dbus:<service>|<method>（path=/，interface=service）
 *            或 dbus:<service>|<path>|<interface>|<method>
 */
#ifndef NL_BROKER_ADAPTER_DBUS_H
#define NL_BROKER_ADAPTER_DBUS_H

#include "capability_registry.h"
#include "adapter_cli.h"     /* 复用 NlPlan / nl_cli_execute */

#define NL_DBUS_SERVICE_MAX 128
#define NL_DBUS_PATH_MAX    192
#define NL_DBUS_IFACE_MAX   128
#define NL_DBUS_METHOD_MAX  96

int  nl_dbus_tool_path(char *out, size_t cap);
int  nl_dbus_build_plan(const NlCapability *cap, const char *args_json, NlPlan *plan);

/* 自省 org.freedesktop.DBus 并把方法登记进注册表；返回新增条数，-1 表示工具不可用 */
int  nl_dbus_discover(NlCapRegistry *reg);

#endif /* NL_BROKER_ADAPTER_DBUS_H */
