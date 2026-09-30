#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 1024u
#define LANES 4u
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static unsigned next(unsigned rng){return (rng*73u+19u)%10000u;}
static void reset(const WFFamily *f,uint32_t *rows,unsigned seed){
    for(unsigned lane=0;lane<LANES;lane++){
        uint32_t *r=rows+lane*ROW;
        r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    for(unsigned lane=0;lane<LANES;lane++)assert(!f->validate(rows+lane*ROW));
}
static void action(const WFFamily *f,uint32_t *rows,unsigned kind,
                   unsigned target,unsigned now){
    WFAction a={.kind=kind,.target=target,.elapsed_ms=now};
    assert(!f->action(rows,&a));f->batch(rows);signal(SIGABRT,SIG_DFL);
    assert(!f->validate(rows));
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();
    assert(f&&f->task_count==1&&f->row_words==ROW&&f->batch_lanes==LANES);
    uint32_t *rows=calloc(LANES*ROW,sizeof *rows);assert(rows);
    reset(f,rows,2);assert(!rows[33]&&!rows[34]);
    WFView v;assert(!f->observe(rows,&v));
    assert(v.count==9&&!strcmp(wf_text_get(&v,v.instruction),
           "Playing as 'X', win a game of tic-tac-toe."));
    unsigned rng=rows[32],chance=next(rng),sample=next(chance);
    unsigned rank=(sample*8u)/10000u,enemy=0;
    for(unsigned i=1;i<9;i++)if(rank--==0){enemy=i;break;}
    assert(enemy>=1&&enemy<=8);
    action(f,rows,WF_CLICK,1,100);
    assert(rows[33]==1u&&rows[34]==(1u<<enemy)&&rows[32]==sample);
    assert(rows[WF_STATUS]==WF_RUNNING);
    uint32_t before[ROW];memcpy(before,rows,sizeof before);
    action(f,rows,WF_CLICK,1,200);
    assert(rows[33]==before[33]&&rows[34]==before[34]&&rows[32]==before[32]);
    reset(f,rows,4);rows[33]=3;rows[34]=24;rows[32]=17;
    action(f,rows,WF_CLICK,3,100);
    assert(rows[33]==7&&rows[34]==24&&rows[32]==17);
    assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==1.0f);
    assert(fabsf(real(rows[WF_TIMED_REWARD])-0.99f)<1e-5f);
    reset(f,rows,6);action(f,rows,WF_WAIT,0,10000);
    assert(rows[WF_STATUS]==WF_TIMEOUT&&real(rows[WF_RAW_REWARD])==-1.0f);
    free(rows);puts("board first source increment passed");return 0;
}
