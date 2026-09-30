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
        r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static void act(const WFFamily *f,uint32_t *rows,unsigned kind,unsigned ms){
    WFAction a={.kind=kind,.target=kind==WF_CLICK?1u:0u,.elapsed_ms=ms};
    assert(!f->action(rows,&a));
    for(unsigned lane=1;lane<f->batch_lanes;lane++)
        memcpy(rows+(size_t)lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==1);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);
    unsigned distinct_thresholds=0,distinct_symbols=0,wrong=0,success=0;
    unsigned first_threshold=0;uint32_t first_symbol=0;
    for(unsigned seed=0;seed<64;seed++){
        reset(f,rows,seed);WFView v;
        assert(!f->observe(rows,&v)&&v.count==3);
        assert(rows[35]==100u&&rows[64]>=4000u&&rows[64]<=5900u);
        assert(rows[34]==rows[64+75]);
        if(!seed){first_threshold=rows[34];first_symbol=rows[1000];}
        distinct_thresholds+=rows[34]!=first_threshold;
        distinct_symbols+=rows[1000]!=first_symbol;
        for(unsigned i=1;i<100;i++){
            unsigned before=rows[63+i],after=rows[64+i];
            if(before>150u){
                int difference=(int)after-(int)before;
                assert(difference>=-150&&difference<=150&&difference%10==0);
            }else assert(after>=1u&&after<=before+150u);
        }
        act(f,rows,WF_CLICK,50);
        assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
        reset(f,rows,seed);
        act(f,rows,WF_WAIT,99);
        assert(rows[32]==0u&&rows[33]==0u);
        act(f,rows,WF_WAIT,100);
        assert(rows[32]==1u&&rows[33]==rows[64]);
        act(f,rows,WF_CLICK,7600);
        assert(rows[WF_STATUS]==WF_TERMINAL&&rows[32]==76u&&
               rows[33]==rows[64+75]&&
               real(rows[WF_RAW_REWARD])==1.0f&&
               fabsf(real(rows[WF_TIMED_REWARD])-0.24f)<1e-6f);
        unsigned frozen_tick=rows[32],frozen_price=rows[33];
        uint32_t frozen_reward=rows[WF_TIMED_REWARD];
        act(f,rows,WF_WAIT,9000);
        assert(rows[WF_STATUS]==WF_TERMINAL&&rows[32]==frozen_tick&&
               rows[33]==frozen_price&&
               rows[WF_TIMED_REWARD]==frozen_reward);
        success++;
        for(unsigned i=0;i<99;i++)if(rows[64+i]>rows[34]){
            reset(f,rows,seed);
            act(f,rows,WF_CLICK,(i+1u)*100u);
            assert(rows[WF_STATUS]==WF_TERMINAL&&
                   real(rows[WF_RAW_REWARD])==-1.0f);
            wrong++;break;
        }
        reset(f,rows,seed);
        act(f,rows,WF_WAIT,9999);
        assert(rows[WF_STATUS]==WF_RUNNING&&rows[32]==99u&&
               rows[33]==rows[64+98]);
        act(f,rows,WF_CLICK,10000);
        assert(rows[WF_STATUS]==WF_TIMEOUT&&rows[33]==rows[64+98]&&
               real(rows[WF_RAW_REWARD])==-1.0f);
        reset(f,rows,seed);
        act(f,rows,WF_CLICK,10000);
        assert(rows[WF_STATUS]==WF_TIMEOUT&&rows[32]==99u&&
               rows[33]==rows[64+98]);
        reset(f,rows,seed);
        WFView before,after;assert(!f->observe(rows,&before));
        unsigned future=rows[64+99];rows[64+99]=future==1u?2u:1u;
        assert(!f->observe(rows,&after)&&!memcmp(&before,&after,sizeof before));
    }
    assert(distinct_thresholds&&distinct_symbols&&wrong&&success==64u);
    free(rows);
    printf("PASS: 64 generated 100-point market cases; %u wrong buys, clock boundaries, tie, deadline, variation and future-price noninterference\n",wrong);
}
