#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+(size_t)lane*f->row_words;
        memset(r,0xa5,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    assert(!f->validate(rows));
}
static void apply(const WFFamily *f,uint32_t *rows,WFAction *a,unsigned elapsed){
    a->elapsed_ms=elapsed;assert(!f->action(rows,a));
    for(unsigned lane=1;lane<f->batch_lanes;lane++)
        memcpy(rows+(size_t)lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    assert(!f->validate(rows));
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==3);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);unsigned actions=0;
    for(unsigned task=0;task<3;task++)for(unsigned seed=0;seed<64;seed++){
        reset(f,rows,task,seed);
        WFView before,after;
        assert(!f->observe(rows,&before));
        if(task==2){
            unsigned date=rows[33];rows[33]=date==150?149:150;
            assert(!f->observe(rows,&after)&&!memcmp(&before,&after,sizeof before));
            rows[33]=date;
        }else{
            unsigned origin=rows[35];rows[35]=origin==6?5:6;
            assert(!f->observe(rows,&after)&&!memcmp(&before,&after,sizeof before));
            rows[35]=origin;
        }
        for(unsigned step=0;step<8&&!rows[WF_STATUS];step++){
            WFView view;WFAction action={0};
            assert(!f->observe(rows,&view));
            assert(!travel_public_action(&view,&action));
            apply(f,rows,&action,100u+step*100u);actions++;
        }
        assert(rows[WF_STATUS]==WF_TERMINAL);
        assert(rows[WF_RAW_REWARD]==1065353216u);
    }
    free(rows);
    printf("PASS: 192 public-view-only travel solutions in %u actions; private target noninterference\n",actions);
    return 0;
}
