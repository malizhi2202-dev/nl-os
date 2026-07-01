#ifndef NL_SCRIPT_PARSER_H
#define NL_SCRIPT_PARSER_H
typedef struct { char **lines; int count; int allow_l2; } NlScript;
NlScript *nl_script_parse(const char *filepath);
void nl_script_free(NlScript *s);
#endif
