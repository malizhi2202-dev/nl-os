#include <stddef.h>
#ifndef NL_DISPATCH_H
#define NL_DISPATCH_H
int nl_is_traditional_cmd(const char *input);
int nl_dispatch(const char *input, char *output, size_t out_sz);
#endif
