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
/* UTF-8 续字节：0b10xxxxxx */
static int utf8_cont(unsigned char c){return (c & 0xC0) == 0x80;}

/*
 * 非 TTY（管道/重定向/脚本）：按行读，不做行编辑、不进 raw 模式。
 * 早先所有情况共用行编辑器，而它只接受 32..126 的 ASCII 字节，
 * 中文输入被静默丢弃（`printf '创建…' | nlsh` 得到 " 的文件夹 "）——见 LESSONS。
 */
static char *read_plain(void)
{
    char buf[MAX_LINE];
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return strdup(buf);
}

char *nl_readline(const char *prompt){
    if(!isatty(0)){ wr(prompt,strlen(prompt)); return read_plain(); }

    wr(prompt,strlen(prompt)); raw_mode();
    char buf[MAX_LINE];int pos=0,n=0;memset(buf,0,MAX_LINE);
    while(1){
        char c;if(read(0,&c,1)!=1){restore();return NULL;}
        if(c=='\n'||c=='\r'){wr("\r\n",2);buf[pos]='\0';restore();return pos>0||n>0?strdup(buf):strdup("");}
        else if(c==127||c=='\b'){
            if(pos>0){
                /* 一次删掉一个完整 UTF-8 字符（中文按 1 个字符算） */
                int removed=0;
                do{pos--;removed++;}while(pos>0&&utf8_cont((unsigned char)buf[pos]));
                int tail=n-(pos+removed);
                memmove(buf+pos,buf+pos+removed,(size_t)tail);
                n-=removed;buf[n]='\0';
                for(int i=0;i<removed;i++) wr("\b \b",3);
                if(tail>0) wr(buf+pos,tail);
                for(int i=0;i<tail;i++) wr("\b",1);
            }
        }
        else if(c==3){wr("^C\r\n",4);restore();return strdup("");}
        else if(c==4&&pos==0){restore();return NULL;}
        else if(((c>=32&&c<127)||(unsigned char)c>=128)&&pos<MAX_LINE-1){
            /* 可打印 ASCII + UTF-8 多字节（中文靠这条才进得来） */
            memmove(buf+pos+1,buf+pos,n-pos);buf[pos++]=c;n++;wr(&c,1);
            if(n>pos)wr(buf+pos,n-pos);
            for(int i=0;i<n-pos;i++)wr("\b",1);
        }
    }
}
