#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_HIST 1000
static char *hist[MAX_HIST]; static int count=0; static const char *file=NULL;
void nl_history_init(const char *p) { file=p; FILE *f=fopen(p,"r"); if(!f)return; char buf[4096]; while(fgets(buf,sizeof(buf),f)&&count<MAX_HIST){size_t l=strlen(buf);while(l>0&&(buf[l-1]=='\n'||buf[l-1]=='\r'))buf[--l]=0;hist[count++]=strdup(buf);} fclose(f); }
void nl_history_add(const char *l) { if(!l||!*l)return; if(count>0&&strcmp(hist[count-1],l)==0)return; if(count>=MAX_HIST){free(hist[0]);memmove(hist,hist+1,(MAX_HIST-1)*sizeof(char*));count--;} hist[count++]=strdup(l); }
const char *nl_history_get(int i) { return (i>=0&&i<count)?hist[i]:NULL; }
int nl_history_count(void) { return count; }
void nl_history_save(void) { if(!file)return; FILE *f=fopen(file,"w"); if(!f)return; for(int i=0;i<count;i++) fprintf(f,"%s\n",hist[i]); fclose(f); }
