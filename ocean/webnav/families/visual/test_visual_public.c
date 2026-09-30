#include "public_controller.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float reward(uint32_t bits){float f;memcpy(&f,&bits,sizeof f);return f;}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==9u);
    uint32_t *rows=calloc((size_t)f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);unsigned passed=0;
    for(unsigned task=0u;task<9u;task++)for(unsigned seed=0u;seed<16u;seed++){
        for(unsigned lane=0;lane<f->batch_lanes;lane++){
            uint32_t *r=rows+(size_t)lane*f->row_words;
            memset(r,0x57,f->row_words*sizeof *r);
            r[WF_VERSION]=2u;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
        if(task==1u||task==2u||task==3u||
           task==4u||task==5u||task==6u||task==7u){
            WFView before,after;assert(!f->observe(rows,&before));
            uint32_t saved[4]={rows[32],rows[33],rows[34],rows[35]};
            uint32_t saved_count=rows[36];
            if(task==3u)rows[32]=(rows[32]+1u)%3u;
            else if(task==1u||task==2u){
                unsigned i=0;while(rows[64u+i]==saved[1])i++;
                assert(i<rows[32]);rows[33]=rows[64u+i];
            }else if(task==4u||task==5u){
                rows[33]=0u;rows[34]=0u;rows[35]=0u;rows[36]=0u;
            }else if(task==6u)rows[32]=rows[32]==7u?3u:rows[32]+1u;
            else rows[32]=(rows[32]+1u)%5u;
            assert(!f->validate(rows));
            assert(!f->observe(rows,&after));
            assert(!memcmp(&before,&after,sizeof before));
            rows[32]=saved[0];rows[33]=saved[1];rows[34]=saved[2];
            rows[35]=saved[3];rows[36]=saved_count;
        }
        for(unsigned step=1u;step<=32u&&rows[WF_STATUS]==WF_RUNNING;step++){
            WFView view;WFAction action={0};
            assert(!f->observe(rows,&view));
            assert(!visual_public_action(&view,&action));
            action.elapsed_ms=step*250u;
            assert(!f->action(rows,&action));
            for(unsigned lane=1;lane<f->batch_lanes;lane++)
                memcpy(rows+(size_t)lane*f->row_words,
                    rows,f->row_words*sizeof *rows);
            f->batch(rows);signal(SIGABRT,SIG_DFL);assert(!f->validate(rows));
        }
        assert(rows[WF_STATUS]==WF_TERMINAL);
        assert(reward(rows[WF_RAW_REWARD])==1.0f);
        passed++;
    }
    free(rows);assert(passed==144u);
    puts("PASS: 144 visual instances solved from public WFView only");
}
