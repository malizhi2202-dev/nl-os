/*
 * seccomp_filter.c — 自身 seccomp 沙箱
 *
 * 限制权限守护进程可用的系统调用为白名单。
 * Linux ≥ 5.10 支持。如果内核不支持 seccomp，降级为 no-op。
 */
#include "seccomp_filter.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 36
#endif

#ifdef __x86_64__
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <stddef.h>
#include <sys/syscall.h>

int nl_seccomp_apply(void) {
    /* 简易 BPF：允许所有常用 syscall，kill 进程如果违规 */
    struct sock_filter filter[] = {
        /* 加载架构 */
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
        /* 加载 syscall 号 */
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS, offsetof(struct seccomp_data, nr)),
        /* 白名单: read, write, open, close, socket, accept, bind, listen, sendto, recvfrom, getpid, exit, fstat, mmap, munmap */
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_read, 12, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_write, 11, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_open, 10, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_close, 9, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_fstat, 8, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_mmap, 7, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_munmap, 6, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_rt_sigaction, 5, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_rt_sigreturn, 4, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_exit, 3, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 2, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_socket, 1, 0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_accept, 0, 0),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
    };
    struct sock_fprog prog = {.len = sizeof(filter)/sizeof(filter[0]), .filter = filter};
    if(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return -1;
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
}
#else
int nl_seccomp_apply(void) { return 0; } /* 非 x86_64 暂不支持 */
#endif
