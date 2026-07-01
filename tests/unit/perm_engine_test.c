/*
 * perm_engine 单元测试
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int tr=0,tp=0,tf=0;
#define TEST(n) do{tr++;printf("  TEST %s ... ",n);}while(0)
#define PASS() do{tp++;printf("PASS\n");}while(0)
#define FAIL(m) do{tf++;printf("FAIL: %s\n",m);return;}while(0)
#define ASSERT(c,m) do{if(!(c)){FAIL(m);return;}}while(0)

#include "../../src/nl-perm/perm_engine.c"

static void test_l1_read(void) {
    TEST("L1 read");
    NlPermRequest req = {.command_type="read", .nl_input="ls", .generated_cmd="ls -la"};
    NlPermResponse resp;
    ASSERT(nl_perm_check(&req,&resp)==0,"check");
    ASSERT(resp.decision==0,"allow"); ASSERT(resp.level==NL_PERM_L1,"L1");
    PASS();
}
static void test_l4_root(void) {
    TEST("L4 root deny");
    char *paths[] = {"/etc"};
    NlPermRequest req = {.command_type="write", .paths=paths, .path_count=1};
    NlPermResponse resp;
    ASSERT(nl_perm_check(&req,&resp)==0,"check");
    ASSERT(resp.decision==2,"deny"); ASSERT(resp.level==NL_PERM_L4,"L4");
    PASS();
}
static void test_l3_delete_home(void) {
    TEST("L3 delete /home");
    char *paths[] = {"/home/user/test"};
    NlPermRequest req = {.command_type="delete", .paths=paths, .path_count=1};
    NlPermResponse resp;
    ASSERT(nl_perm_check(&req,&resp)==0,"check");
    ASSERT(resp.decision==1,"confirm"); ASSERT(resp.level==NL_PERM_L3,"L3");
    PASS();
}
static void test_l2_write(void) {
    TEST("L2 write");
    NlPermRequest req = {.command_type="write", .nl_input="创建文件"};
    NlPermResponse resp;
    ASSERT(nl_perm_check(&req,&resp)==0,"check");
    ASSERT(resp.level==NL_PERM_L2,"L2"); ASSERT(resp.decision==1,"confirm");
    PASS();
}
int main(void) {
    printf("perm_engine test\n===============\n\n");
    test_l1_read(); test_l4_root(); test_l3_delete_home(); test_l2_write();
    printf("\n%d pass / %d fail / %d total\n",tp,tf,tr);
    return tf>0?1:0;
}
