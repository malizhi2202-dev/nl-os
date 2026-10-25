/*
 * nl_eval.h — nlsh --eval 决策层评测入口（T13）
 */
#ifndef NL_EVAL_H
#define NL_EVAL_H

/* 返回 0：无 schema violation；1：有 violation 或输入错误 */
int nl_eval_run(const char *reg_path, const char *bench_path);

#endif /* NL_EVAL_H */
