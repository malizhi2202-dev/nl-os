/*
 * ipc_server.h — Unix domain socket 服务端
 */
#ifndef NL_IPC_SERVER_H
#define NL_IPC_SERVER_H

int nl_ipc_server_start(const char *socket_path);
void nl_ipc_server_stop(void);
int nl_ipc_server_accept(void);  /* 阻塞等待一个连接，处理请求，返回 0=成功 */

#endif
