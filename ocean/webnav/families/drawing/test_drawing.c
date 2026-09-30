#include "../common/family_api.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
static uint32_t rows[8192];
static const WFFamily *f;
static void act(unsigned k,unsigned x,unsigned y,unsigned ms){
 WFAction a={.kind=k,.target=k?1:0,.arg0=x,.arg1=y,.elapsed_ms=ms};assert(!f->action(rows,&a));
 f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
int main(void){
 prctl(PR_SET_DUMPABLE,0,0,0,0);f=webnav_family_v2();
 for(unsigned lane=0;lane<4;lane++){uint32_t *r=rows+2048*lane;r[0]=2;r[1]=lane%2;r[2]=WF_RESET;r[3]=100+lane;}
 f->batch(rows);signal(SIGABRT,SIG_DFL);
 for(unsigned lane=0;lane<4;lane++)assert(!f->validate(rows+2048*lane));
 act(WF_POINTER_MOVE,20*256,30*256,100);assert(!rows[36]);
 act(WF_POINTER_DOWN,20*256,30*256,200);assert(rows[36]==1&&rows[37]);
 act(WF_POINTER_MOVE,50*256,60*256,300);assert(rows[36]==2&&rows[128]==50*256);
 act(WF_POINTER_UP,50*256,60*256,400);assert(!rows[37]);
 act(WF_POINTER_MOVE,60*256,70*256,500);assert(rows[36]==2);
 act(WF_POINTER_DOWN,90*256,50*256,600);assert(rows[36]==1);
 act(WF_WAIT,0,0,10000);assert(rows[9]==WF_TIMEOUT);
 puts("PASS: drawing first increment; capture, release, replacement, timeout and four lanes");
}
