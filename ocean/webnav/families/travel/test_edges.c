#include "../common/family_api.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+(size_t)lane*f->row_words;
        memset(r,0x7b,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static void act(const WFFamily *f,uint32_t *rows,unsigned kind,
                unsigned target,unsigned value,unsigned now){
    WFAction a={.kind=kind,.target=target,.arg0=value,.elapsed_ms=now};
    assert(!f->action(rows,&a));
    for(unsigned lane=1;lane<f->batch_lanes;lane++)
        memcpy(rows+(size_t)lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==3);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);
    for(unsigned task=0;task<2;task++)for(unsigned seed=0;seed<32;seed++){
        reset(f,rows,task,seed);
        unsigned from=rows[35],to=rows[36],date=rows[37];
        act(f,rows,WF_CLICK,4,0,100);
        assert(rows[39]==0&&rows[41]==7&&rows[WF_STATUS]==WF_RUNNING);
        act(f,rows,WF_SELECT_OPTION,1,7,200);
        act(f,rows,WF_SELECT_OPTION,2,to,300);
        act(f,rows,WF_SELECT_OPTION,3,date,400);
        assert(rows[41]==7); /* Styling clears on Search, not field entry. */
        act(f,rows,WF_CLICK,4,0,500);
        assert(rows[39]==0&&rows[41]==1);
        act(f,rows,WF_SELECT_OPTION,1,from,600);
        assert(rows[41]==1);
        act(f,rows,WF_CLICK,4,0,700);
        assert(rows[39]==1&&rows[41]==0&&rows[42]==0);
        act(f,rows,WF_CLICK,5,0,800);
        assert(rows[39]==0&&rows[41]==0);
        act(f,rows,WF_SELECT_OPTION,3,92,900);
        act(f,rows,WF_CLICK,4,0,1000);
        assert(rows[39]==1&&rows[42]==1&&rows[41]==0);
        act(f,rows,WF_CLICK,6,0,1100);
        assert(rows[WF_STATUS]==WF_TERMINAL&&rows[WF_RAW_REWARD]==3212836864u);
        uint32_t raw=rows[WF_RAW_REWARD],timed=rows[WF_TIMED_REWARD];
        act(f,rows,WF_WAIT,0,0,1200);
        assert(rows[WF_STATUS]==WF_TERMINAL&&
               rows[WF_RAW_REWARD]==raw&&rows[WF_TIMED_REWARD]==timed);
        reset(f,rows,task,seed);
        act(f,rows,WF_WAIT,0,0,29999);
        assert(rows[WF_STATUS]==WF_RUNNING);
        act(f,rows,WF_WAIT,0,0,30000);
        assert(rows[WF_STATUS]==WF_TIMEOUT&&rows[WF_RAW_REWARD]==3212836864u);
    }
    for(unsigned seed=0;seed<32;seed++){
        reset(f,rows,2,seed);
        act(f,rows,WF_WAIT,0,0,9999);
        assert(rows[WF_STATUS]==WF_RUNNING);
        act(f,rows,WF_WAIT,0,0,10000);
        assert(rows[WF_STATUS]==WF_TIMEOUT&&rows[WF_RAW_REWARD]==3212836864u);
        uint32_t raw=rows[WF_RAW_REWARD];
        act(f,rows,WF_CLICK,1,0,10001);
        assert(rows[WF_STATUS]==WF_TIMEOUT&&rows[WF_RAW_REWARD]==raw);
    }
    free(rows);
    puts("PASS: flight validation/recovery, wrong date, terminal absorption and exact deadline across three travel tasks");
}
