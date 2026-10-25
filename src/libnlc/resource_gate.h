/*
 * resource_gate.h — 加载模型前的资源闸门（ADR-015 运行期自降档的代码落点）
 *
 * 起因（真实事故）：一台 8GB 机器上可用内存只剩 657MB，一句"规则未命中"的话
 * 仍去加载 428MB 的模型（实测 RSS 677–815MB），把整机拖进内存回收，用户感受到的是
 * "本来很快，现在整个终端都卡了"。
 *
 * 原则：**宁可这条命令不理解，也不拿整台机器的响应性去换**。
 * 内存不够就不加载，如实告诉用户，并保留模型路径（内存释放后下次自动可用）。
 *
 * 判定所需的三个数都是纯值，便于单测；读 /proc/meminfo 与 stat 只是取数。
 */
#ifndef NL_RESOURCE_GATE_H
#define NL_RESOURCE_GATE_H

/* 加载后要为其它进程留出的余量（MB）。低于它就等于开始 swap/回收，全机变慢。 */
#define NL_MODEL_HEADROOM_MB 512

/* 模型真实 RSS ≈ 文件大小的 1.6–1.9 倍（mmap + KV cache + 计算缓冲），取 2 倍留余量。 */
#define NL_MODEL_RSS_FACTOR 2

/* MemAvailable（MB）；读不到返回 -1 */
long nl_mem_available_mb(void);

/* 加载某个 GGUF 预计需要多少 MB（文件大小 × NL_MODEL_RSS_FACTOR）；文件不可读返回 -1 */
long nl_model_need_mb(const char *gguf_path);

/* 纯判定：need 与 headroom 都要能在 avail 里放下才允许加载。参数非法一律拒绝（fail-closed） */
int nl_resource_gate_allow(long need_mb, long avail_mb, long headroom_mb);

#endif
