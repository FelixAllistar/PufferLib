#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 2048u
static uint32_t rows[4*ROW];
static const WFFamily *f;
static void reset(unsigned task,unsigned seed){
    for(unsigned lane=0;lane<4;lane++){
        uint32_t *r=rows+lane*ROW;r[0]=2;r[1]=task;r[2]=WF_RESET;
        r[3]=seed+lane;r[6]=0;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    for(unsigned lane=0;lane<4;lane++)assert(!f->validate(rows+lane*ROW));
}
static void act(unsigned kind,unsigned target,unsigned key,const char *text,unsigned ms){
    WFAction a={.kind=kind,.target=target,.arg0=key,.text=text,
        .text_length=text?strlen(text):0,.elapsed_ms=ms};
    assert(!f->action(rows,&a));
    for(unsigned lane=1;lane<4;lane++)rows[lane*ROW+2]=WF_OBSERVE;
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static unsigned option(const char *label){
    WFView v;assert(!f->observe(rows,&v));
    for(unsigned i=0;i<v.count;i++)if(v.nodes[i].role==WF_OPTION&&
       !strcmp(wf_text_get(&v,v.nodes[i].name),label))return v.nodes[i].ref;
    return 0;
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);f=webnav_family_v2();assert(f->task_count==2);
    reset(1,123);act(WF_CLICK,1,0,NULL,0);
    act(WF_INSERT,0,0,"Al",100);assert(rows[47]==0&&rows[288]==400);
    act(WF_WAIT,0,0,NULL,399);assert(rows[47]==0);
    act(WF_WAIT,0,0,NULL,400);assert(rows[47]==1&&option("Albania"));
    unsigned old_count=rows[52];
    act(WF_SELECT_ALL,0,0,NULL,410);
    act(WF_INSERT,0,0,"Ba",450);
    assert(rows[288]==750&&rows[52]==old_count&&option("Albania"));
    act(WF_WAIT,0,0,NULL,749);assert(option("Albania"));
    act(WF_WAIT,0,0,NULL,750);assert(!option("Albania")&&option("Bahamas"));
    act(WF_CLICK,option("Bahamas"),0,NULL,760);assert(rows[47]==0&&rows[288]==0);

    /* ArrowDown performs synchronous search even while a debounce is queued. */
    reset(1,123);act(WF_CLICK,1,0,NULL,0);act(WF_INSERT,0,0,"Al",100);
    act(WF_KEY_DOWN,0,40,NULL,150);assert(rows[47]==1&&option("Albania"));
    act(WF_KEY_DOWN,0,40,NULL,160);assert(rows[47]==2&&rows[53]==1);
    act(WF_ENTER,0,0,NULL,170);assert(rows[47]==0);
    act(WF_WAIT,0,0,NULL,400);assert(rows[47]==1); /* source timer can reopen it */

    reset(1,123);act(WF_CLICK,1,0,NULL,0);act(WF_INSERT,0,0,"Al",100);
    act(WF_CLICK,2,0,NULL,200);assert(rows[9]==WF_TERMINAL&&rows[288]==0);
    uint32_t terminal[ROW];memcpy(terminal,rows,sizeof terminal);
    act(WF_WAIT,0,0,NULL,900);
    assert(rows[9]==terminal[9]&&rows[10]==terminal[10]&&rows[11]==terminal[11]&&rows[47]==0);
    reset(1,123);act(WF_CLICK,1,0,NULL,0);act(WF_INSERT,0,0,"Al",9800);
    act(WF_WAIT,0,0,NULL,10000);assert(rows[9]==WF_TIMEOUT&&rows[47]==0);

    unsigned solved=0,dirty=0;
    uint32_t fresh[4*ROW];
    for(unsigned task=0;task<2;task++)for(unsigned seed=0;seed<128;seed++){
        memset(rows,0xa5,sizeof rows);reset(task,seed);memcpy(fresh,rows,sizeof rows);
        memset(rows,0x5a,sizeof rows);reset(task,seed);
        assert(!memcmp(fresh,rows,sizeof rows));dirty+=4;
        for(unsigned step=1;step<32&&!rows[9];step++){
            WFView v,other;assert(!f->observe(rows,&v));
            unsigned goal=rows[192];rows[192]='x';assert(!f->observe(rows,&other));
            assert(!memcmp(&v,&other,sizeof v));rows[192]=goal;
            char scratch[64];WFAction a;
            assert(!autocomplete_public_next(&v,scratch,sizeof scratch,&a));
            act(a.kind,a.target,a.arg0,a.text,step*100);
        }
        assert(rows[9]==WF_TERMINAL&&rows[10]==1065353216u);solved++;
    }
    printf("PASS: debounce boundaries, replacement/stale menu, synchronous arrows, queued reopen, cancellation/timeout; %u public solves and %u dirty-row comparisons\n",solved,dirty);
}
