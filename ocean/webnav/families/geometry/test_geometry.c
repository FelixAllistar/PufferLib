#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static void reset(const WFFamily *f,uint32_t *rows,unsigned seed){
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+(size_t)lane*f->row_words;
        memset(r,0xa5,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=3;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static void act(const WFFamily *f,uint32_t *rows,
                unsigned kind,unsigned target,unsigned elapsed){
    WFAction a={.kind=kind,.target=target,.elapsed_ms=elapsed};
    assert(!f->action(rows,&a));
    for(unsigned lane=1;lane<f->batch_lanes;lane++)
        memcpy(rows+(size_t)lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5u);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);
    unsigned variation=0,first_goal=0;
    for(unsigned seed=0;seed<32u;seed++){
        reset(f,rows,seed);unsigned goal=rows[32];assert(goal<25u&&rows[33]==25u);
        if(!seed)first_goal=goal;variation+=goal!=first_goal;
        WFView v;assert(!f->observe(rows,&v)&&v.count==25u);
        char label[24];snprintf(label,sizeof label,"(%d,%d)",(int)(goal/5u)-2,2-(int)(goal%5u));
        assert(strstr(wf_text_get(&v,v.instruction),label));
        assert(!strcmp(wf_text_get(&v,v.nodes[goal].name),label));
        act(f,rows,WF_CLICK,goal+1u,500u);
        assert(rows[WF_STATUS]==WF_TERMINAL&&
               real(rows[WF_RAW_REWARD])==1.0f&&
               fabsf(real(rows[WF_TIMED_REWARD])-0.95f)<1e-6f);
        uint32_t frozen=rows[WF_TIMED_REWARD];
        act(f,rows,WF_WAIT,0u,9000u);
        assert(rows[WF_STATUS]==WF_TERMINAL&&rows[WF_TIMED_REWARD]==frozen);
        reset(f,rows,seed);
        act(f,rows,WF_CLICK,(goal+1u)%25u+1u,500u);
        assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
        reset(f,rows,seed);
        act(f,rows,WF_CLICK,goal+1u,10000u);
        assert(rows[WF_STATUS]==WF_TIMEOUT&&real(rows[WF_TIMED_REWARD])==-1.0f);
    }
    assert(variation);free(rows);
    puts("PASS: 32 grid-coordinate goals, wrong points, terminal absorption and deadline");
}
