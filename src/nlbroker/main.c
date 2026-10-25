/*
 * main.c — nlbroker 入口（ADR-017 / T02）
 */
#include "server.h"
#include "dispatch.h"
#include "adapter_dbus.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

#define NL_DEFAULT_SOCKET "/run/nl-os/broker.sock"

static void on_term(int sig)
{
    (void)sig;
    nl_server_request_stop();
}

static void usage(void)
{
    printf("nlbroker — NL-OS capability broker (CIP v1)\n");
    printf("用法: nlbroker [--socket PATH] [--registry PATH] [--scan-dbus] [--dry-run]\n");
}

int main(int argc, char **argv)
{
    NlServerOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.socket_path = NL_DEFAULT_SOCKET;
    const char *registry_path = NULL;
    int dry_run = 0;
    int scan_dbus = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            opts.socket_path = argv[++i];
        } else if (strcmp(argv[i], "--registry") == 0 && i + 1 < argc) {
            registry_path = argv[++i];
        } else if (strcmp(argv[i], "--scan-dbus") == 0) {
            scan_dbus = 1;
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "nlbroker: 未知参数 %s\n", argv[i]);
            usage();
            return 2;
        }
    }
    opts.dry_run = dry_run;
    if (dry_run) {
        printf("socket=%s (dry-run, 不监听)\n", opts.socket_path);
        return 0;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sa.sa_flags = 0;              /* 不设 SA_RESTART：让 accept 可被信号中断 */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    NlDispatch disp;
    if (nl_dispatch_init(&disp, registry_path) != 0) {
        fprintf(stderr, "nlbroker: 注册表初始化失败\n");
        return 1;
    }
    if (scan_dbus) {
        int added = nl_dbus_discover(disp.registry);
        if (added < 0) {
            fprintf(stderr, "nlbroker: busctl 不可用，跳过 dbus 发现\n");
        } else {
            const char *rp = (registry_path && registry_path[0]) ? registry_path
                                                                 : nl_cap_default_path();
            if (nl_cap_save_atomic(disp.registry, rp) != 0) {
                fprintf(stderr, "nlbroker: 注册表写回失败 (%s)\n", rp);
            } else {
                printf("dbus 能力发现：新增 %d 条 → %s\n", added, rp);
            }
        }
        nl_dispatch_free(&disp);
        return 0;
    }

    nl_server_set_handler(nl_dispatch_line, &disp);

    int rc = nl_server_run(&opts);
    nl_dispatch_free(&disp);
    if (rc != 0) {
        fprintf(stderr, "nlbroker: 启动失败 (%s)\n", opts.socket_path);
        return 1;
    }
    return 0;
}
