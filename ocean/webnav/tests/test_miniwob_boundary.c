#define _POSIX_C_SOURCE 200809L
#include "bridge.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

/* Exercise the public boundary: malformed lengths must abort before Bend's
 * wrapping array addressing can cross into the next lane. Each child starts
 * before the parent initializes the runtime, avoiding a fork of live threads. */
static unsigned cases;
static void rejects(const uint32_t row[WEBNAV_WORDS]) {
    pid_t pid=fork(); assert(pid>=0);
    if (!pid) {
        struct rlimit limit={0,0}; setrlimit(RLIMIT_CORE,&limit);
        uint32_t words[WEBNAV_BATCH*WEBNAV_WORDS]={0};
        memcpy(words+(WEBNAV_BATCH-1)*WEBNAV_WORDS,row,WEBNAV_WORDS*sizeof *row);
        webnav_batch(words);
        _exit(0);
    }
    int status; assert(waitpid(pid,&status,0)==pid);
    assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT); cases++;
}
int main(void) {
    uint32_t r[WEBNAV_WORDS]={0};
    r[0]=UINT32_MAX; rejects(r);
    memset(r,0,sizeof r); r[1]=17; rejects(r);
    memset(r,0,sizeof r); r[0]=4; r[1]=1; r[16]=65; rejects(r);
    r[16]=0; r[19]=65; rejects(r);
    r[19]=0; r[11]=65; rejects(r);
    r[11]=0; r[17]=1; rejects(r);
    r[17]=0; r[18]=1; rejects(r);
    r[18]=0; r[16]=1; r[32]=0; rejects(r);
    r[32]=127; rejects(r);
    r[32]='a'; r[16]=64; r[17]=r[18]=64;
    for(unsigned i=0;i<64;i++)r[32+i]='a';
    r[2]=1; r[7]=2; r[11]=1; r[160]='b'; rejects(r);
    memset(r,0,sizeof r); r[0]=5; r[1]=2; r[20]=33; rejects(r);
    r[20]=0; r[23]=33; rejects(r);
    memset(r,0,sizeof r); r[0]=9; r[1]=9; rejects(r);
    r[1]=1; r[19]=1; r[20]=1; rejects(r); /* Cyclic/self parent. */
    r[19]=0; r[18]=1; rejects(r); /* A file cannot be expanded. */
    r[18]=0; r[20]=256; rejects(r); /* Subtree outside its row's nodes. */
    memset(r,0,sizeof r); r[0]=7;r[17]=1;r[14]=r[16]=50;r[13]=UINT32_MAX;rejects(r);
    memset(r,0,sizeof r); r[0]=8;r[12]=1;r[13]=10;rejects(r);
    r[13]=3;r[10]=3;rejects(r);
    r[10]=0;r[17]=65;rejects(r);
    memset(r,0,sizeof r);r[0]=10;r[1]=1;r[13]=r[14]=2;
    r[160]='C';r[161]='a';r[192]=r[193]='z';
    r[16]=65;rejects(r);
    r[16]=0;r[19]=65;rejects(r);
    r[19]=0;r[7]=2;r[11]=33;rejects(r);
    r[11]=0;r[17]=1;rejects(r);
    r[17]=0;r[15]=2;r[20]=1;r[21]=2;r[19]=1;rejects(r);
    r[15]=r[20]=r[21]=r[19]=0;r[16]=r[17]=r[18]=64;r[2]=1;
    for(unsigned i=0;i<64;i++)r[32+i]='A';
    r[11]=1;r[224]='B';rejects(r);
    printf("PASS: %u malformed ABI rows rejected before runtime entry\n",cases);
    return 0;
}
