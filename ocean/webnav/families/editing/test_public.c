#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static void change_goal(uint32_t *r,unsigned task,uint32_t *old){
    unsigned at=task==0u?1024u:task<=2u?2048u:
        task==3u?34u:32u;
    *old=r[at];
    r[at]=task<=2u?(*old==65u?66u:65u):
        task==3u?(*old+1u)%4u:(*old+1u)%6u;
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5u);
    uint32_t *rows=malloc(f->row_words*f->batch_lanes*sizeof *rows);
    assert(rows);unsigned solved=0u;
    for(unsigned task=0u;task<5u;task++)for(unsigned seed=0u;seed<64u;seed++){
        memset(rows,0xa5,f->row_words*f->batch_lanes*sizeof *rows);
        for(unsigned lane=0u;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2u;r[WF_TASK]=task;r[WF_OP]=WF_RESET;
            r[WF_SEED]=seed+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0u;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView before,after;
            assert(!f->validate(r)&&!f->observe(r,&before));
            uint32_t old;change_goal(r,task,&old);
            int valid=f->validate(r),projected=valid?-1:f->observe(r,&after);
            int different=projected?-1:memcmp(&before,&after,sizeof before);
            if(valid||projected||different)
                fprintf(stderr,"editing private view task=%u seed=%u lane=%u valid=%d observe=%d difference=%d\n",
                    task,seed,lane,valid,projected,different);
            assert(!valid&&!projected&&!different);
            unsigned at=task==0u?1024u:task<=2u?2048u:
                task==3u?34u:32u;
            r[at]=old;
        }
        for(unsigned step=0u;step<32u;step++){
            unsigned running=0u;
            for(unsigned lane=0u;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                if(r[WF_STATUS]!=WF_RUNNING)continue;
                WFView v;WFAction a;char scratch[256];
                assert(!f->observe(r,&v));
                int rc=editing_public_next(&v,scratch,sizeof scratch,&a);
                if(rc){
                    const char *q=wf_text_get(&v,v.instruction);
                    fprintf(stderr,"editing public parse task=%u seed=%u lane=%u step=%u query=%s\n",
                        task,seed,lane,step,q?q:"<invalid>");
                }
                assert(!rc);running++;
                assert(!f->action(r,&a));
            }
            if(!running)break;
            f->batch(rows);signal(SIGABRT,SIG_DFL);
        }
        for(unsigned lane=0u;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            if(r[WF_STATUS]!=WF_TERMINAL||r[WF_RAW_REWARD]!=1065353216u){
                WFView v;assert(!f->observe(r,&v));
                const char *q=wf_text_get(&v,v.instruction);
                fprintf(stderr,"editing public failure task=%u seed=%u lane=%u status=%u raw=%08x elapsed=%u query=%s\n",
                    task,seed,lane,r[WF_STATUS],r[WF_RAW_REWARD],r[WF_ELAPSED],
                    q?q:"<invalid>");
            }
            assert(r[WF_STATUS]==WF_TERMINAL&&r[WF_RAW_REWARD]==1065353216u);
            solved++;
        }
    }
    free(rows);printf("PASS: %u public-only generated editing episodes\n",solved);
    return 0;
}
