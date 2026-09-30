#include "../common/family_api.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5u);
    size_t n=(size_t)f->row_words*f->batch_lanes;
    uint32_t *fresh=calloc(n,sizeof *fresh),*dirty=malloc(n*sizeof *dirty);
    assert(fresh&&dirty);
    for(unsigned task=0;task<5u;task++)for(unsigned seed=0;seed<32u;seed++){
        memset(fresh,0,n*sizeof *fresh);memset(dirty,0x9a,n*sizeof *dirty);
        for(unsigned lane=0;lane<f->batch_lanes;lane++){
            uint32_t *a=fresh+(size_t)lane*f->row_words;
            uint32_t *b=dirty+(size_t)lane*f->row_words;
            a[WF_VERSION]=b[WF_VERSION]=2;
            a[WF_TASK]=b[WF_TASK]=task;
            a[WF_OP]=b[WF_OP]=WF_RESET;
            a[WF_SEED]=b[WF_SEED]=seed*4u+lane;
        }
        f->batch(fresh);signal(SIGABRT,SIG_DFL);
        f->batch(dirty);signal(SIGABRT,SIG_DFL);
        assert(!memcmp(fresh,dirty,n*sizeof *fresh));
        for(unsigned lane=0;lane<f->batch_lanes;lane++){
            WFView v;uint32_t *r=fresh+(size_t)lane*f->row_words;
            assert(!f->validate(r)&&!f->observe(r,&v));
        }
        if(task==3u){
            WFView before,after;assert(!f->observe(fresh,&before));
            unsigned goal=fresh[32];fresh[32]=(goal+1u)%25u;
            assert(!f->observe(fresh,&after)&&!memcmp(&before,&after,sizeof before));
            fresh[32]=goal;
        }
    }
    free(dirty);free(fresh);
    puts("PASS: 640 full-row fresh-vs-dirty geometry resets and grid private-goal noninterference");
}
