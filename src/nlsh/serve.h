/*
 * serve.h — 常驻解析守护进程（ADR-021）
 *
 * `nlsh --serve` 进入此模式：一条 unix socket，行分隔 JSON，单线程。
 */

#ifndef NL_SERVE_H
#define NL_SERVE_H

/*
 * 跑守护进程循环（阻塞直到 SIGTERM 或空闲超时）。
 * idle_secs：空闲多久卸载模型（0 = 不卸载）；max_idle_secs：空闲多久进程退出（0 = 不退出）。
 * 返回 0 正常退出；-1 无法建立 socket。
 */
int nl_serve_run(const char *socket_path, int idle_secs, int max_idle_secs);

#endif /* NL_SERVE_H */
