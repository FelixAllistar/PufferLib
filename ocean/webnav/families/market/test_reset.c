#include "../common/family_api.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==1);
    size_t count=(size_t)f->row_words*f->batch_lanes;
    uint32_t *fresh=calloc(count,sizeof *fresh),*dirty=malloc(count*sizeof *dirty);
    assert(fresh&&dirty);
    for(unsigned seed=0;seed<32;seed++){
        memset(fresh,0,count*sizeof *fresh);
        memset(dirty,0xa5,count*sizeof *dirty);
        for(unsigned lane=0;lane<f->batch_lanes;lane++){
            uint32_t *a=fresh+(size_t)lane*f->row_words;
            uint32_t *b=dirty+(size_t)lane*f->row_words;
            a[WF_VERSION]=b[WF_VERSION]=2;
            a[WF_TASK]=b[WF_TASK]=0;
            a[WF_OP]=b[WF_OP]=WF_RESET;
            a[WF_SEED]=b[WF_SEED]=seed*4u+lane;
        }
        f->batch(fresh);signal(SIGABRT,SIG_DFL);
        f->batch(dirty);signal(SIGABRT,SIG_DFL);
        assert(!memcmp(fresh,dirty,count*sizeof *fresh));
        for(unsigned lane=0;lane<f->batch_lanes;lane++){
            const uint32_t *r=fresh+(size_t)lane*f->row_words;WFView v;
            assert(!f->validate(r)&&!f->observe(r,&v));
        }
    }
    free(dirty);free(fresh);
    puts("PASS: 128 fresh-vs-dirty full-row market reset comparisons");
}
