#include "ipc_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
int nl_ipc_connect(const char *path) { int fd=socket(AF_UNIX,SOCK_STREAM,0); if(fd<0)return -1; struct sockaddr_un a={.sun_family=AF_UNIX}; strncpy(a.sun_path,path,sizeof(a.sun_path)-1); if(connect(fd,(struct sockaddr*)&a,sizeof(a))<0){close(fd);return -1;} return fd; }
int nl_ipc_request(int fd,const char *req,char *resp,size_t sz) { if(write(fd,req,strlen(req))<0)return -1; ssize_t n=read(fd,resp,sz-1); if(n<=0)return -1; resp[n]='\0'; return 0; }
void nl_ipc_close(int fd) { if(fd>=0)close(fd); }
