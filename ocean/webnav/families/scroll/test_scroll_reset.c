#include "../common/family_api.h"
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 8192u
#define LANES 4u

static void headers(uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned lane=0;lane<LANES;lane++){
        uint32_t *r=rows+lane*ROW;
        r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;
        r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
    }
}

int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();
    assert(f&&f->row_words==ROW&&f->batch_lanes==LANES&&f->task_count==4);
    uint32_t fresh[ROW*LANES],dirty[ROW*LANES],copy[ROW];
    WFView before,after;
    for(unsigned task=0;task<4;task++)for(unsigned seed=1;seed<30;seed+=7){
        memset(fresh,0,sizeof fresh);
        for(unsigned i=0;i<ROW*LANES;i++)dirty[i]=0xA5A50000u+i;
        headers(fresh,task,seed);headers(dirty,task,seed);
        f->batch(fresh);signal(SIGABRT,SIG_DFL);
        f->batch(dirty);signal(SIGABRT,SIG_DFL);
        assert(!memcmp(fresh,dirty,sizeof fresh));
        for(unsigned lane=0;lane<LANES;lane++)
            assert(!f->validate(fresh+lane*ROW));

        assert(!f->observe(fresh,&before));
        memcpy(copy,fresh,ROW*sizeof *copy);
        if(task==0){
            copy[38]^=1u;
            copy[39]=copy[39]==1?2:1;
        }else if(task==1||task==3){
            copy[4736]=copy[4736]=='X'?'Y':'X';
        }else copy[35]^=1u;
        assert(!f->validate(copy));
        assert(!f->observe(copy,&after));
        assert(!memcmp(&before,&after,sizeof before));
    }
    puts("scroll full reset and private target projection passed");
    return 0;
}
