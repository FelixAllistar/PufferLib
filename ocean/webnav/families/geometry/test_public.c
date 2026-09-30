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
        memset(r,0x97,f->row_words*sizeof *r);
        r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
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
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5u);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);unsigned actions=0;
    for(unsigned task=0;task<5u;task++)for(unsigned seed=0;seed<64u;seed++){
        reset(f,rows,task,seed);
        for(unsigned phase=0;phase<(task==3u?1u:2u);phase++){
            WFView v;WFAction a={0};assert(!f->observe(rows,&v));
            assert(!geometry_public_next(&v,task,phase,&a));
            a.elapsed_ms=100u*(phase+1u);
            apply(f,rows,&a);actions++;
        }
        assert(rows[WF_STATUS]==WF_TERMINAL&&rows[WF_RAW_REWARD]!=0u);
        float raw;memcpy(&raw,rows+WF_RAW_REWARD,sizeof raw);
        assert(raw>=0.95f);
    }
    free(rows);
    printf("PASS: 320 public-scene-only geometry solutions (raw reward >= 0.95) in %u actions\n",actions);
}
