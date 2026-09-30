#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static void run(const WFFamily *f,uint32_t *rows,unsigned kind,
                unsigned target,unsigned arg0,unsigned elapsed){
    WFAction a={.kind=kind,.target=target,.arg0=arg0,.elapsed_ms=elapsed};
    assert(!f->action(rows,&a));
    for(unsigned lane=1;lane<4;lane++)
        memcpy(rows+lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    assert(!f->validate(rows));
}
static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned lane=0;lane<4;lane++){
        uint32_t *r=rows+lane*f->row_words;
        memset(r,0xa5,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==3);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);
    for(unsigned task=0;task<2;task++)for(unsigned seed=0;seed<64;seed++){
        reset(f,rows,task,seed);WFView v;assert(!f->observe(rows,&v)&&v.count==102);
        unsigned origin=rows[35],destination=rows[36],date=rows[37];
        run(f,rows,WF_SELECT_OPTION,1,origin,250);
        run(f,rows,WF_SELECT_OPTION,2,destination,500);
        run(f,rows,WF_SELECT_OPTION,3,date,750);
        run(f,rows,WF_CLICK,4,0,1000);
        assert(rows[39]==1u&&rows[41]==0u&&rows[42]==0u);
        unsigned best=0;uint32_t value=UINT32_MAX;
        for(unsigned i=0;i<rows[40];i++){
            const uint32_t *flight=rows+64+4*i;
            uint32_t metric=flight[rows[38]?1:0];
            if(metric<value){value=metric;best=i;}
        }
        run(f,rows,WF_CLICK,best+6,0,1250);
        assert(rows[WF_STATUS]==WF_TERMINAL&&
               real(rows[WF_RAW_REWARD])==1.0f&&
               fabsf(real(rows[WF_TIMED_REWARD])-(1.0f-1250.0f/30000.0f))<1e-6f);

        reset(f,rows,task,seed);run(f,rows,WF_SELECT_OPTION,1,7,250);
        run(f,rows,WF_CLICK,4,0,500);
        assert(rows[39]==0u&&(rows[41]&1u));

        reset(f,rows,task,seed);
        run(f,rows,WF_SELECT_OPTION,1,origin==1?2:1,250);
        run(f,rows,WF_SELECT_OPTION,2,destination,500);
        run(f,rows,WF_SELECT_OPTION,3,date,750);
        run(f,rows,WF_CLICK,4,0,1000);
        assert(rows[39]==1u&&rows[42]==1u);
        run(f,rows,WF_CLICK,6,0,1250);
        assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==-1.0f);
    }
    free(rows);
    puts("PASS: 128 generated flight searches and bookings across both presets with independent rank, invalid search, fake result, and time reward checks");
}
