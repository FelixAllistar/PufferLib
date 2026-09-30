#include "../common/family_api.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5);
    size_t words=(size_t)f->row_words*f->batch_lanes;
    uint32_t *fresh=calloc(words,sizeof *fresh),*dirty=malloc(words*sizeof *dirty);
    assert(fresh&&dirty);
    for(unsigned task=0;task<5;task++)for(unsigned seed=0;seed<32;seed++){
        memset(fresh,0,words*sizeof *fresh);
        memset(dirty,0xa5,words*sizeof *dirty);
        for(unsigned lane=0;lane<f->batch_lanes;lane++){
            uint32_t *clean=fresh+lane*f->row_words,*used=dirty+lane*f->row_words;
            clean[WF_VERSION]=used[WF_VERSION]=2;
            clean[WF_TASK]=used[WF_TASK]=task;
            clean[WF_OP]=used[WF_OP]=WF_RESET;
            clean[WF_SEED]=used[WF_SEED]=seed*4+lane;
        }
        f->batch(fresh);signal(SIGABRT,SIG_DFL);
        f->batch(dirty);signal(SIGABRT,SIG_DFL);
        assert(!memcmp(fresh,dirty,words*sizeof *fresh));
        for(unsigned lane=0;lane<f->batch_lanes;lane++){
            const uint32_t *r=fresh+lane*f->row_words;WFView v;
            assert(!f->validate(r)&&!f->observe(r,&v));
        }
    }
    free(dirty);free(fresh);
    puts("PASS: 640 fresh-vs-dirty full-row comparisons across all five tasks");
}
