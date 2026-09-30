#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static void reset(const WFFamily *f,uint32_t *rows,unsigned seed){
    for(unsigned lane=0;lane<4;lane++){
        uint32_t *r=rows+lane*f->row_words;
        memset(r,0xae,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=2;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static void click(const WFFamily *f,uint32_t *rows,unsigned ref,unsigned now){
    WFAction a={.kind=WF_CLICK,.target=ref,.elapsed_ms=now};
    assert(!f->action(rows,&a));
    for(unsigned lane=1;lane<4;lane++)
        memcpy(rows+lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static float expected(const uint32_t *r,unsigned index){
    unsigned criterion=r[32],duration=criterion==1||criterion==2;
    uint32_t lo=UINT32_MAX,hi=0,score=r[64+4*index+duration];
    for(unsigned i=0;i<4;i++){
        uint32_t x=r[64+4*i+duration];if(x<lo)lo=x;if(x>hi)hi=x;
    }
    if(criterion==2||criterion==3){
        if(score==hi)return 1.0f;
        return score>lo?-0.5f:-1.0f;
    }
    if(score==lo)return 1.0f;
    return score<hi?-0.5f:-1.0f;
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==3);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);unsigned best=0,middle=0,worst=0;
    for(unsigned seed=0;seed<64;seed++)for(unsigned i=0;i<4;i++){
        reset(f,rows,seed);WFView before,after;
        assert(!f->observe(rows,&before)&&before.count==4);
        uint32_t date=rows[33];rows[33]=date==150?149:150;
        assert(!f->observe(rows,&after)&&!memcmp(&before,&after,sizeof before));
        rows[33]=date;
        float raw=expected(rows,i);
        click(f,rows,i+1,500);
        assert(rows[WF_STATUS]==WF_TERMINAL&&
               fabsf(real(rows[WF_RAW_REWARD])-raw)<1e-6f);
        float timed=raw>0?1.0f-500.0f/10000.0f:raw;
        assert(fabsf(real(rows[WF_TIMED_REWARD])-timed)<1e-6f);
        if(raw>0)best++;else if(raw>-1)middle++;else worst++;
        unsigned status=rows[WF_STATUS],reward=rows[WF_RAW_REWARD];
        WFAction wait={.kind=WF_WAIT,.elapsed_ms=1000};
        assert(!f->action(rows,&wait));f->batch(rows);signal(SIGABRT,SIG_DFL);
        assert(rows[WF_STATUS]==status&&rows[WF_RAW_REWARD]==reward);
    }
    assert(best&&middle&&worst);
    free(rows);
    printf("PASS: 256 independent four-criterion ticket rankings; best=%u middle=%u worst=%u\n",
           best,middle,worst);
}
