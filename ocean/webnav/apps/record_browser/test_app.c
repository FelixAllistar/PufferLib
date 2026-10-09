#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/prctl.h>
int wa_fixture(uint32_t *);
static void send(uint32_t *r,unsigned action,unsigned argument,unsigned delta) {
    r[64]=action;r[65]=argument;r[66]=delta;assert(!wa_fixture(r));
}
static void reset(uint32_t *r) {
    memset(r,0,512*sizeof *r);r[1]=17;r[2]=50;r[11]=1;r[67]=12;
    for (unsigned i=0;i<12;i++) {
        r[192+4*i]=i+1;r[193+4*i]=0;r[194+4*i]=12-i;r[195+4*i]=i%3;
    }
    send(r,0,0,0);
}
int main(void) {
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    uint32_t r[512],records[64];reset(r);memcpy(records,r+192,sizeof records);
    assert(r[260]==4 && r[256]==12 && r[259]==9);
    send(r,12,0,0);assert(r[70]==1 && r[256]==8 && r[259]==5);
    send(r,13,1,0);assert(r[0]==0); /* Not on the visible page. */
    send(r,13,7,0);assert(r[0]==7 && r[4]==1);
    send(r,9,2,0);assert(r[69]==0); /* No page actions during loading. */
    send(r,0,0,50);assert(r[4]==0);
    send(r,1,0,0);send(r,0,0,50);assert(r[0]==0 && r[70]==1 && r[256]==8);
    send(r,9,2,0);assert(r[70]==0 && r[260]==4 && r[256]==11 && r[259]==2);
    send(r,10,1,0);assert(r[256]==2 && r[259]==11);
    send(r,4,0,0);r[160]='1';r[76]=1;send(r,5,0,0);
    assert(r[72]==1 && r[75]==0 && r[260]==4); /* Unsubmitted search draft. */
    send(r,8,0,0);assert(r[75]==1 && r[260]==1 && r[256]==11);
    send(r,13,11,0);send(r,0,0,50);assert(r[0]==11 && r[4]==0);
    send(r,1,0,0);send(r,0,0,50);assert(r[0]==0 && r[256]==11 && r[75]==1);
    assert(!memcmp(records,r+192,sizeof records));
    send(r,4,0,0);send(r,6,0,0);r[160]='z';r[76]=1;send(r,5,0,0);send(r,8,0,0);
    assert(r[260]==0);send(r,12,0,0);assert(r[70]==0);
    reset(r);r[3]=1;send(r,13,12,0);send(r,0,0,50);assert(r[4]==2);
    send(r,3,0,0);send(r,0,0,50);assert(r[4]==0 && r[0]==12 && r[3]==0);
    send(r,14,0,0);assert(r[280]==1 && r[281]==12 && r[284]==1 && r[320]=='1');
    send(r,1,0,0);assert(r[0]==12); /* Modal owns interaction. */
    send(r,17,0,0);r[160]='9';r[161]='9';r[76]=2;send(r,16,0,0);
    assert(r[320]=='9' && r[284]==2 && r[194+4*11]==1);
    send(r,20,0,0);assert(r[280]==3 && r[194+4*11]==1);
    send(r,14,0,0);send(r,17,0,0);r[160]='x';r[76]=1;send(r,16,0,0);send(r,19,0,0);
    assert(r[280]==1 && r[283]==3 && r[194+4*11]==1);
    send(r,17,0,0);r[160]='9';r[161]='9';r[76]=2;send(r,16,0,0);send(r,19,0,0);
    assert(r[280]==2 && r[283]==0 && r[194+4*11]==99 && r[193+4*11]==1);
    send(r,19,0,0);assert(r[193+4*11]==1);
    send(r,1,0,0);send(r,0,0,50);assert(r[0]==0 && r[256]==11); /* Query sees committed amount. */
    puts("PASS: composed record browser, list/detail/history, search drafts, sort/filter/page, empty results and failure recovery");
    return 0;
}
