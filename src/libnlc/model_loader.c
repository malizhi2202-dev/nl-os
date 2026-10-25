/*
 * model_loader.c — 模型加载与推理
 * BUILD_STUB_MODEL=OFF: 使用系统 llama.cpp 真实推理
 * BUILD_STUB_MODEL=ON:  stub 固定 JSON 响应
 */
#include "model_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <math.h>

#ifdef GGML_USE_CPU
#include <llama.h>
#endif

struct NlModel {
    NlModelInfo info;
    int is_stub;
    FILE *file;
#ifdef GGML_USE_CPU
    struct llama_model   *llama_model;
    struct llama_context *llama_ctx;
    const struct llama_vocab *vocab;
    /* 最近一次推理的置信度：grammar 掩码后各 token 概率的几何平均 */
    double conf_sum;
    int    conf_n;
    double confidence;
#endif
};

/* 在目录里找第一个 *.gguf（按目录顺序；候选名差异足够，不必排序） */
static int find_gguf_in(const char *dir, char *out, size_t cap)
{
    if (!dir || !dir[0]) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    int found = 0;
    struct dirent *ent;
    while (!found && (ent = readdir(d)) != NULL) {
        size_t n = strlen(ent->d_name);
        if (n < 7 || strcmp(ent->d_name + n - 5, ".gguf") != 0) continue;
        snprintf(out, cap, "%s/%s", dir, ent->d_name);
        if (access(out, R_OK) == 0) found = 1;
    }
    closedir(d);
    return found;
}

const char *nl_model_default_path(void)
{
    static char buf[1024];
    const char *env = getenv("NL_MODEL_PATH");
    /* 显式指定就尊重它（即使读不到也要原样返回，让调用方按"模型不可用"处理） */
    if (env && env[0]) return env;

    if (find_gguf_in("/usr/share/nl-os", buf, sizeof(buf))) return buf;

    char dir[512];
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0]) {
        snprintf(dir, sizeof(dir), "%s/nl-os", xdg);
        if (find_gguf_in(dir, buf, sizeof(buf))) return buf;
    }
    if (home && home[0]) {
        snprintf(dir, sizeof(dir), "%s/.local/share/nl-os", home);
        if (find_gguf_in(dir, buf, sizeof(buf))) return buf;
    }
    return NULL;
}

#ifdef GGML_USE_CPU

static int g_backend_ok = 0;

NlModel *nl_model_init(const char *path) {
    if (!path) return NULL;
    if (!g_backend_ok) { llama_backend_init(); g_backend_ok = 1; }

    NlModel *m = calloc(1, sizeof(NlModel));
    if (!m) return NULL;

    struct llama_model_params mp = llama_model_default_params();
    m->llama_model = llama_model_load_from_file(path, mp);
    if (!m->llama_model) { free(m); return NULL; }

    struct llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 512;
    cp.n_batch = 256;    /* prefill 批处理；n_batch=1 会把 prompt 处理拖成逐 token */
    /* 线程数按实际核数（上限 8）：默认值在少核机器上会过订阅，反而更慢 */
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    int nt = (cores > 0) ? (int)cores : 1;
    if (nt > 8) nt = 8;
    cp.n_threads = nt;
    cp.n_threads_batch = nt;
    m->llama_ctx = llama_init_from_model(m->llama_model, cp);
    if (!m->llama_ctx) { llama_model_free(m->llama_model); free(m); return NULL; }

    m->vocab = llama_model_get_vocab(m->llama_model);
    snprintf(m->info.arch, sizeof(m->info.arch), "llama");
    m->info.vocab_size = llama_vocab_n_tokens(m->vocab);
    snprintf(m->info.gguf_version, sizeof(m->info.gguf_version), "v3");
    return m;
}

const NlModelInfo *nl_model_get_info(const NlModel *m) { return m ? &m->info : NULL; }

/* 每次推理前清空 KV：REPL 场景下不能复用上一次的上下文 */
static void clear_kv(NlModel *m)
{
    if (!m || !m->llama_ctx) return;
    llama_memory_t mem = llama_get_memory(m->llama_ctx);
    if (mem) llama_memory_clear(mem, true);
}

/* 记录选中 token 的对数概率（grammar 掩码后）：用于 AC-6 的 probability 字段 */
static void record_conf(NlModel *m, llama_token tid)
{
    float *logits = llama_get_logits_ith(m->llama_ctx, -1);
    int n = llama_vocab_n_tokens(m->vocab);
    if (!logits || n <= 0 || tid < 0 || tid >= n) return;

    float mx = logits[0];
    for (int i = 1; i < n; i++) if (logits[i] > mx) mx = logits[i];
    if (isinf(mx) && mx < 0) return;   /* 全被掩码：无法归一化 */
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += exp((double)(logits[i] - mx));
    if (!(sum > 0.0)) return;
    m->conf_sum += (double)(logits[tid] - mx) - log(sum);
    m->conf_n++;
}

static int infer_impl(NlModel *m, const char *prompt, const char *grammar,
                      char *output, size_t max_len)
{
    if (!m || !output || max_len == 0) return -1;

    clear_kv(m);
    m->conf_sum = 0.0;
    m->conf_n = 0;
    m->confidence = -1.0;

    /* Tokenize */
    llama_token tokens[1024];
    int n = llama_tokenize(m->vocab, prompt, (int)strlen(prompt), tokens, 1024, 1, 0);
    if (n < 0) return -1;

    /* Decode prompt
     * 分块上限是 **n_batch**（不是 n_ctx）：llama_decode 断言 n_tokens_all <= n_batch，
     * 按 n_ctx（512）分块时一段 300 token 的 prompt 会直接 GGML_ASSERT 杀掉进程
     * （实测 `显示当前工作目录` 的 few-shot prompt 就超过 256 而 abort）。 */
    uint32_t n_ctx = llama_n_ctx(m->llama_ctx);
    uint32_t chunk = llama_n_batch(m->llama_ctx);
    if (chunk == 0 || chunk > n_ctx) chunk = n_ctx;
    /* prompt 超出上下文窗口：直接失败，不要靠 decode 报错（会留下半截 KV） */
    if (n > (int)n_ctx) return -1;
    for (int pos = 0; pos < n; pos += (int)chunk) {
        int nb = (n - pos > (int)chunk) ? (int)chunk : (n - pos);
        if (llama_decode(m->llama_ctx, llama_batch_get_one(tokens + pos, nb)) != 0)
            return -1;
    }

    struct llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    struct llama_sampler *smpl = llama_sampler_chain_init(sparams);
    if (grammar && grammar[0]) {
        struct llama_sampler *g = llama_sampler_init_grammar(m->vocab, grammar, "root");
        if (!g) {
            /* 语法解析失败：退回贪心，避免整条链路失败 */
        } else {
            llama_sampler_chain_add(smpl, g);
        }
    }
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

    char *out = output;
    size_t rem = max_len - 1;
    int total = 0;
    for (int i = 0; i < 128 && rem > 0; i++) {
        llama_token tid = llama_sampler_sample(smpl, m->llama_ctx, -1);
        if (llama_vocab_is_eog(m->vocab, tid)) break;
        record_conf(m, tid);
        char buf[16];
        int cn = llama_token_to_piece(m->vocab, tid, buf, (int)sizeof(buf), 0, 1);
        if (cn < 0) break;
        if ((size_t)cn > rem) cn = (int)rem;
        memcpy(out, buf, (size_t)cn);
        out += cn; rem -= (size_t)cn; total += cn;
        if (llama_decode(m->llama_ctx, llama_batch_get_one(&tid, 1)) != 0) break;
    }
    *out = '\0';
    llama_sampler_free(smpl);
    if (m->conf_n > 0) m->confidence = exp(m->conf_sum / (double)m->conf_n);
    return total;
}

int nl_model_infer(NlModel *m, const char *prompt, char *output, size_t max_len)
{
    return infer_impl(m, prompt, NULL, output, max_len);
}

int nl_model_infer_grammar(NlModel *m, const char *prompt, const char *grammar,
                           char *output, size_t max_len)
{
    if (!grammar || !grammar[0]) return -1;
    return infer_impl(m, prompt, grammar, output, max_len);
}

double nl_model_last_confidence(const NlModel *m)
{
    return m ? m->confidence : -1.0;
}

int nl_model_format_chat(NlModel *m, const char *system, const char *user,
                         char *out, size_t cap)
{
    if (!m || !out || cap == 0) return -1;
    const char *sys = system ? system : "";
    const char *usr = user ? user : "";

    const char *tmpl = llama_model_chat_template(m->llama_model, NULL);
    if (tmpl) {
        llama_chat_message msgs[2] = { { "system", sys }, { "user", usr } };
        int32_t need = llama_chat_apply_template(tmpl, msgs, 2, true, out, (int32_t)cap);
        if (need > 0 && (size_t)need < cap) return need;
        if (need > 0) {
            char *buf = malloc((size_t)need + 1);
            if (!buf) return -1;
            int32_t got = llama_chat_apply_template(tmpl, msgs, 2, true, buf, need + 1);
            int ok = (got > 0 && (size_t)got < cap);
            if (ok) memcpy(out, buf, (size_t)got);
            free(buf);
            return ok ? got : -1;
        }
        return -1;
    }

    /* 回退：Qwen ChatML */
    int n = snprintf(out, cap,
                     "<|im_start|>system\n%s<|im_end|>\n<|im_start|>user\n%s<|im_end|>\n<|im_start|>assistant\n",
                     sys, usr);
    return (n > 0 && (size_t)n < cap) ? n : -1;
}

void nl_model_free(NlModel *m) {
    if (!m) return;
    if (m->llama_ctx) llama_free(m->llama_ctx);
    if (m->llama_model) llama_model_free(m->llama_model);
    free(m);
}

#else /* BUILD_STUB_MODEL=ON */

NlModel *nl_model_init(const char *path) {
    if (!path) return NULL;
    NlModel *m = calloc(1, sizeof(NlModel));
    if (!m) return NULL;
    m->is_stub = 1;
    m->file = fopen(path, "rb");
    if (!m->file) { free(m); return NULL; }
    char magic[4];
    if (fread(magic,1,4,m->file)==4 && memcmp(magic,"GGUF",4)!=0) { fclose(m->file); free(m); return NULL; }
    fseek(m->file,0,SEEK_END); m->info.model_size=(size_t)ftell(m->file); rewind(m->file);
    snprintf(m->info.arch,sizeof(m->info.arch),"stub");
    m->info.vocab_size=32000; m->info.hidden_size=2048;
    snprintf(m->info.gguf_version,sizeof(m->info.gguf_version),"v3");
    return m;
}
const NlModelInfo *nl_model_get_info(const NlModel *m) { return m?&m->info:NULL; }
int nl_model_infer(NlModel *m,const char *prompt,char *output,size_t max_len) {
    if(!m||!output||max_len==0)return -1;
    const char *r="{\"intent\":\"create_dir\",\"entities\":{\"target\":\"stub\"},\"confidence\":0.85}";
    size_t l=strlen(r); if(l>=max_len)l=max_len-1;
    memcpy(output,r,l); output[l]='\0'; (void)prompt; return (int)l;
}
void nl_model_free(NlModel *m) { if(!m)return; if(m->file)fclose(m->file); free(m); }
int nl_model_infer_grammar(NlModel *m,const char *prompt,const char *grammar,char *output,size_t max_len){
    (void)m;(void)prompt;(void)grammar;(void)output;(void)max_len;return -1;
}
int nl_model_format_chat(NlModel *m,const char *system,const char *user,char *out,size_t cap){
    (void)m;(void)system;(void)user;(void)out;(void)cap;return -1;
}
double nl_model_last_confidence(const NlModel *m){ (void)m; return -1.0; }
#endif
