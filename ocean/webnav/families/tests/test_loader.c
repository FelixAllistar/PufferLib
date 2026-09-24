#include "../common/loader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
 assert(argc==3);WFLoaded a,b;char error[512];WFView v;
 assert(!wf_open(&a,argv[1],error,sizeof error));assert(!wf_open(&b,argv[2],error,sizeof error));
 assert(!wf_reset(&a,0,100)&&!wf_reset(&b,0,200));
 assert(!wf_observe(&a,0,&v));assert(!strcmp(wf_text_get(&v,v.instruction),"mock-a"));
 assert(!wf_observe(&b,1,&v));assert(!strcmp(wf_text_get(&v,v.instruction),"mock-b"));
 assert(wf_reset(&a,1,0)<0);assert(wf_observe(&a,2,&v)<0);
 WFAction wait={.kind=WF_WAIT,.elapsed_ms=250};assert(!wf_apply(&a,0,&wait));assert(!wf_batch_checked(&a));assert(a.words[14]==1&&b.words[14]==0);
 assert(!wf_apply(&b,0,&wait));assert(!wf_batch_checked(&b));assert(b.words[14]==1);/* separate DSO counters */
 wait.elapsed_ms=249;assert(wf_apply(&a,0,&wait)<0);wait.elapsed_ms=500;wait.kind=999;assert(wf_apply(&a,0,&wait)<0);
 a.words[WF_TASK]=1;assert(wf_batch_checked(&a)<0);a.words[WF_TASK]=0;
 a.words[WF_RAW_REWARD]=0x7fc00000;assert(wf_batch_checked(&a)<0);a.words[WF_RAW_REWARD]=0;
 assert(!wf_observe(&a,0,&v));v.instruction.length=WF_TEXT_BYTES;assert(!wf_view_valid(&v));
 wf_view_init(&v,0,10000);WFText t;assert(wf_text_add(&v,"",WF_TEXT_BYTES,&t)<0&&v.text_truncated);
 wf_close(&a);wf_close(&b);assert(!wf_open(&a,argv[1],error,sizeof error));assert(!wf_reset(&a,0,0));wf_close(&a);
 assert(wf_open(&a,"/does-not-exist",error,sizeof error)<0&&*error);
 puts("PASS: family loader ABI validation, public text bounds, clocks, row checks, DSO isolation and reopen");
}
