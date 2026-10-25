#include "confirm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nl_confirm_required(int needs_confirm, int perm_level)
{
    return (needs_confirm != 0) || (perm_level >= 2);
}

int nl_confirm_yes(const char *answer)
{
    if (!answer) return 0;
    while (*answer == ' ' || *answer == '\t') answer++;
    if (*answer == '\0' || *answer == '\n') return 0;   /* 直接回车 = 取消（默认 No） */
    if (answer[0] == 'y' || answer[0] == 'Y') return 1;
    /* 中文"是"（UTF-8: E6 98 AF） */
    if ((unsigned char)answer[0] == 0xE6 &&
        (unsigned char)answer[1] == 0x98 &&
        (unsigned char)answer[2] == 0xAF) return 1;
    return 0;
}

int nl_confirm_assume_yes(void)
{
    const char *v = getenv("NL_ASSUME_YES");
    if (!v || !v[0]) return 0;
    return v[0] == '1' || v[0] == 'y' || v[0] == 'Y';
}

int nl_confirm_ask(const char *cmd, int perm_level)
{
    if (nl_confirm_assume_yes()) return 1;

    printf("⚠️ 需要确认 (L%d): %s\n执行? [y/N] ", perm_level, cmd ? cmd : "");
    fflush(stdout);

    char line[32];
    if (!fgets(line, sizeof(line), stdin)) {
        printf("\n已取消（无输入）\n");
        return 0;
    }
    if (!nl_confirm_yes(line)) {
        printf("已取消（未执行）\n");
        return 0;
    }
    return 1;
}
