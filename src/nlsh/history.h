#ifndef NL_HISTORY_H
#define NL_HISTORY_H
void nl_history_init(const char *path);
void nl_history_add(const char *line);
const char *nl_history_get(int idx);
int  nl_history_count(void);
void nl_history_save(void);
#endif
