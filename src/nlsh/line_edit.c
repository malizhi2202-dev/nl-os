#include "line_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#define MAX_LINE 4096
static struct termios orig_tio;
static void raw_mode(void){tcgetattr(0,&orig_tio);struct termios raw=orig_tio;raw.c_lflag&=~(ECHO|ICANON);raw.c_cc[VMIN]=1;raw.c_cc[VTIME]=0;tcsetattr(0,TCSANOW,&raw);}
static void restore(void){tcsetattr(0,TCSANOW,&orig_tio);}
static void wr(const char *s,int n){if(write(1,s,n)<0){}}
char *nl_readline(const char *prompt){
    wr(prompt,strlen(prompt)); raw_mode();
    char buf[MAX_LINE];int pos=0,n=0;memset(buf,0,MAX_LINE);
    while(1){
        char c;if(read(0,&c,1)!=1){restore();return NULL;}
        if(c=='\n'||c=='\r'){wr("\r\n",2);buf[pos]='\0';restore();return pos>0||n>0?strdup(buf):strdup("");}
        else if(c==127||c=='\b'){if(pos>0){pos--;memmove(buf+pos,buf+pos+1,n-pos-1);n--;wr("\b \b",3);}}
        else if(c==3){wr("^C\r\n",4);restore();return strdup("");}
        else if(c==4&&pos==0){restore();return NULL;}
        else if(c>=32&&c<127&&pos<MAX_LINE-1){memmove(buf+pos+1,buf+pos,n-pos);buf[pos++]=c;n++;wr(buf+pos-1,1);if(n>pos)wr(buf+pos,n-pos);for(int i=0;i<n-pos;i++)wr("\b",1);}
    }
}
