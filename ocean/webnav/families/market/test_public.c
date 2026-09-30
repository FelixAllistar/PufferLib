#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static void reset(const WFFamily *f,uint32_t *rows,unsigned seed){
    for(unsigned lane=0;lane<f->batch_lanes;lane++){
        uint32_t *r=rows+(size_t)lane*f->row_words;
        memset(r,0x89,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
static void apply(const WFFamily *f,uint32_t *rows,const WFAction *a){
    assert(!f->action(rows,a));
    for(unsigned lane=1;lane<f->batch_lanes;lane++)
        memcpy(rows+(size_t)lane*f->row_words,rows,f->row_words*sizeof *rows);
    f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==1);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);unsigned actions=0;
    for(unsigned seed=0;seed<64;seed++){
        reset(f,rows,seed);
        WFView before,after;assert(!f->observe(rows,&before));
        unsigned future=rows[64+99];rows[64+99]=future==1u?2u:1u;
        assert(!f->observe(rows,&after)&&!memcmp(&before,&after,sizeof before));
        rows[64+99]=future;
        for(unsigned step=0;step<78&&rows[WF_STATUS]==WF_RUNNING;step++){
            WFView v;WFAction a={0};assert(!f->observe(rows,&v));
            assert(!market_public_next(&v,&a));
            assert(a.elapsed_ms>=rows[WF_ELAPSED]&&a.elapsed_ms<10000u);
            apply(f,rows,&a);actions++;
        }
        assert(rows[WF_STATUS]==WF_TERMINAL&&
               rows[WF_RAW_REWARD]==1065353216u);
    }
    free(rows);
    printf("PASS: 64 public-view-only market solutions in %u actions; private future-price noninterference\n",actions);
}
