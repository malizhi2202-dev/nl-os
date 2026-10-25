#ifndef NL_SCRIPT_PARSER_H
#define NL_SCRIPT_PARSER_H
typedef struct {
    char **lines;
    int count;
    int allow_l2;
    char *perm_line;   /* 原样的 `# @perm: …` 行；固化时必须保留（ADR-019 P-3） */
} NlScript;
NlScript *nl_script_parse(const char *filepath);
void nl_script_free(NlScript *s);
#endif
