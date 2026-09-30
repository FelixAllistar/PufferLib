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
        memset(r,0x91,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
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
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==9u);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);unsigned variation=0,swatches=0,first_goal=0;
    for(unsigned seed=0;seed<32u;seed++){
        reset(f,rows,seed);unsigned goal=rows[36],target=0;
        if(!seed)first_goal=goal;variation+=goal!=first_goal;swatches+=rows[37];
        for(unsigned i=0;i<4u;i++)if(rows[32+i]==goal)target=10u+i;
        assert(target>=10u&&target<=13u);
        WFView v;assert(!f->observe(rows,&v)&&v.count==4u+rows[37]);
        act(f,rows,WF_CLICK,target,100u);
        assert(rows[WF_STATUS]==WF_TERMINAL&&
               real(rows[WF_RAW_REWARD])==1.0f&&
               fabsf(real(rows[WF_TIMED_REWARD])-0.99f)<1e-6f);
        uint32_t frozen=rows[WF_TIMED_REWARD];
        act(f,rows,WF_WAIT,0u,900u);
        assert(rows[WF_STATUS]==WF_TERMINAL&&rows[WF_TIMED_REWARD]==frozen);
        reset(f,rows,seed);
        act(f,rows,WF_CLICK,target==10u?11u:10u,100u);
        assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
        reset(f,rows,seed);
        if(rows[37]){
            act(f,rows,WF_CLICK,1u,100u);
            assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
        }
        reset(f,rows,seed);
        act(f,rows,WF_CLICK,target,10000u);
        assert(rows[WF_STATUS]==WF_TIMEOUT&&real(rows[WF_TIMED_REWARD])==-1.0f);
    }
    assert(variation&&swatches);free(rows);
    puts("PASS: 32 click-color generated goals, wrong boxes, query swatch, deadline and absorption");
}
