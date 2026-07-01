/*
 * nl-perm main.c — 权限守护进程入口
 *
 * daemon 化 → 加载配置 → seccomp 自身 → 启动 IPC → 事件循环
 */
#include "ipc_server.h"
#include "perm_engine.h"
#include "audit_log.h"
#include "seccomp_filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <syslog.h>

#define SOCK_PATH "/run/nl-os/perm.sock"

static volatile int g_running = 1;

static void sig_handler(int sig) {
    if(sig == SIGTERM || sig == SIGINT) g_running = 0;
    if(sig == SIGHUP) { nl_perm_reload_config(); syslog(LOG_NOTICE, "配置已重载"); }
}

int main(void) {
    /* daemon 化 */
    pid_t pid = fork();
    if(pid < 0) return 1;
    if(pid > 0) _exit(0); /* 父进程退出 */
    setsid();
    umask(0);
    fclose(stdin); fclose(stdout); fclose(stderr);

    /* 信号处理 */
    signal(SIGTERM, sig_handler); signal(SIGINT, sig_handler);
    signal(SIGHUP, sig_handler); signal(SIGPIPE, SIG_IGN);

    /* 初始化 */
    nl_audit_open("nl-perm");
    mkdir("/run/nl-os", 0755);
    mkdir("/etc/nl-os", 0755);
    nl_perm_load_config("/etc/nl-os/perm_rules.conf");
    nl_seccomp_apply();

    if(nl_ipc_server_start(SOCK_PATH) != 0) {
        syslog(LOG_ERR, "IPC server 启动失败"); return 1;
    }
    syslog(LOG_NOTICE, "权限守护已启动 socket=%s pid=%d", SOCK_PATH, getpid());

    /* 事件循环 */
    while(g_running) {
        if(nl_ipc_server_accept() < 0 && g_running) usleep(100000);
    }

    nl_ipc_server_stop();
    nl_audit_close();
    syslog(LOG_NOTICE, "权限守护已停止");
    return 0;
}
