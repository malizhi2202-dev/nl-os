/*
 * model_loader.c — 模型加载与推理
 * BUILD_STUB_MODEL=OFF: 使用系统 llama.cpp 真实推理
 * BUILD_STUB_MODEL=ON:  stub 固定 JSON 响应
 */
#include "model_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#endif
};

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
    cp.n_ctx = 512; cp.n_batch = 1;
    m->llama_ctx = llama_init_from_model(m->llama_model, cp);
    if (!m->llama_ctx) { llama_model_free(m->llama_model); free(m); return NULL; }

    m->vocab = llama_model_get_vocab(m->llama_model);
    snprintf(m->info.arch, sizeof(m->info.arch), "llama");
    m->info.vocab_size = llama_vocab_n_tokens(m->vocab);
    snprintf(m->info.gguf_version, sizeof(m->info.gguf_version), "v3");
    return m;
}

const NlModelInfo *nl_model_get_info(const NlModel *m) { return m ? &m->info : NULL; }

int nl_model_infer(NlModel *m, const char *prompt, char *output, size_t max_len) {
    if (!m || !output || max_len == 0) return -1;

    /* Tokenize */
    llama_token tokens[1024];
    int n = llama_tokenize(m->vocab, prompt, (int)strlen(prompt), tokens, 1024, 1, 0);
    if (n < 0) return -1;

    /* Decode prompt */
    uint32_t n_ctx = llama_n_ctx(m->llama_ctx);
    for (int pos = 0; pos < n; pos += (int)n_ctx) {
        int batch = (n - pos > (int)n_ctx) ? (int)n_ctx : (n - pos);
        if (llama_decode(m->llama_ctx, llama_batch_get_one(tokens + pos, batch)) != 0)
            return -1;
    }

    /* Setup greedy sampler */
    struct llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    struct llama_sampler *smpl = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

    /* Generate */
    char *out = output;
    size_t rem = max_len - 1;
    int total = 0;
    for (int i = 0; i < 128 && rem > 0; i++) {
        llama_token tid = llama_sampler_sample(smpl, m->llama_ctx, -1);
        if (llama_vocab_is_eog(m->vocab, tid)) break;
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
    return total;
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
#endif
