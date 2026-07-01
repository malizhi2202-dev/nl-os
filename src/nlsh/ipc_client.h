#include <stddef.h>
#ifndef NL_IPC_CLIENT_H
#define NL_IPC_CLIENT_H
int nl_ipc_connect(const char *path);
int nl_ipc_request(int fd, const char *json_req, char *response, size_t resp_sz);
void nl_ipc_close(int fd);
#endif
