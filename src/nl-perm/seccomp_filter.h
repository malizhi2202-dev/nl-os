/*
 * seccomp_filter.h — 自身 seccomp 沙箱
 */
#ifndef NL_SECCOMP_FILTER_H
#define NL_SECCOMP_FILTER_H

int nl_seccomp_apply(void);  /* 限制自身系统调用为白名单 */

#endif
