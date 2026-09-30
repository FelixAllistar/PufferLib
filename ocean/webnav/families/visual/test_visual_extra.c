#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+(size_t)lane*f->row_words;
        memset(r,0x93,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
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
    assert(rows);unsigned pie=0,shade=0;
    for(unsigned task=1u;task<=3u;task++)for(unsigned seed=0;seed<32u;seed++){
        reset(f,rows,task,seed);
        if(task==1u||task==2u){
            unsigned count=rows[32],goal=rows[33],index=count;
            assert(count>=4u&&count<=8u&&rows[34]==0u);
            for(unsigned i=0;i<count;i++)if(rows[64+i]==goal)index=i;
            assert(index<count);
            act(f,rows,WF_CLICK,10u+index,50u);
            assert(rows[WF_STATUS]==WF_RUNNING&&rows[34]==0u);
            act(f,rows,WF_CLICK,1u,100u);
            assert(rows[WF_STATUS]==WF_RUNNING&&rows[34]==1u&&
                   rows[35]==(task==2u?100u:1600u));
            WFView v;assert(!f->observe(rows,&v));
            assert(v.count==(task==2u?count+1u:1u));
            if(task==2u)assert(v.nodes[index+1u].flags&WF_VISIBLE);
            if(task==1u){
                act(f,rows,WF_CLICK,10u+index,1599u);
                assert(rows[WF_STATUS]==WF_RUNNING);
            }
            unsigned when=task==2u?100u:1600u;
            act(f,rows,WF_CLICK,10u+index,when);
            assert(rows[WF_STATUS]==WF_TERMINAL&&
                   real(rows[WF_RAW_REWARD])==1.0f&&
                   fabsf(real(rows[WF_TIMED_REWARD])-
                         (1.0f-(float)when/10000.0f))<1e-6f);
            reset(f,rows,task,seed);
            act(f,rows,WF_CLICK,1u,100u);
            act(f,rows,WF_CLICK,10u+(index+1u)%count,when);
            assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
            reset(f,rows,task,seed);
            act(f,rows,WF_CLICK,1u,10000u);
            assert(rows[WF_STATUS]==WF_TIMEOUT);
            pie++;
        }else{
            unsigned target=rows[32],count=0;
            assert(rows[WF_DEADLINE]==15000u&&target<3u);
            for(unsigned i=0;i<12u;i++)count+=rows[64+i]==target;
            assert(count>=2u&&count<=6u);
            act(f,rows,WF_CLICK,2u,100u);
            assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
            reset(f,rows,task,seed);
            unsigned now=100u;
            for(unsigned i=0;i<12u;i++)if(rows[64+i]==target)
                act(f,rows,WF_CLICK,10u+i,now+=100u);
            assert(rows[WF_STATUS]==WF_RUNNING);
            act(f,rows,WF_CLICK,2u,now+=100u);
            assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==1.0f);
            reset(f,rows,task,seed);
            unsigned extra=0;while(extra<12u&&rows[64+extra]==target)extra++;
            assert(extra<12u);
            act(f,rows,WF_CLICK,10u+extra,100u);
            act(f,rows,WF_CLICK,2u,200u);
            assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
            reset(f,rows,task,seed);
            act(f,rows,WF_CLICK,2u,15000u);
            assert(rows[WF_STATUS]==WF_TIMEOUT);
            shade++;
        }
    }
    assert(pie==64u&&shade==32u);free(rows);
    puts("PASS: 64 pie and 32 shade cases; expansion timing, toggles, wrong choices and deadlines");
}
