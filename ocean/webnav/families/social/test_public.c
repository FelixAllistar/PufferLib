#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/prctl.h>

int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==3u);
    static const char *const single_names[]={"Reply","Retweet","Like","Share via DM",
        "Copy link to Tweet","Embed Tweet","Mute","Block","Report"};
    static const char *const multi_names[]={"Reply","Retweet","Like","Share"};
    uint32_t *rows=calloc(f->row_words*f->batch_lanes,sizeof *rows);
    assert(rows);unsigned solved=0;
    for(unsigned task=0;task<3;task++)for(unsigned seed=0;seed<64;seed++){
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView before,after;
            assert(!f->observe(r,&before));
            const char *query=wf_text_get(&before,before.instruction);
            const char *name=task?multi_names[r[35]]:single_names[r[35]];
            char quoted[64];snprintf(quoted,sizeof quoted,"\"%s\"",name);
            assert(query&&strstr(query,quoted));
            unsigned old_goal=r[34],old_action=r[35],old_count=r[36];
            r[34]=1u+(old_goal%r[32]);r[35]=(old_action+1u)%(task?4u:9u);
            r[36]=1u+(old_count%r[32]);
            assert(!f->observe(r,&after)&&!memcmp(&before,&after,sizeof before));
            r[34]=old_goal;r[35]=old_action;r[36]=old_count;
        }
        for(unsigned step=0;step<32;step++){
            unsigned running=0;
            for(unsigned lane=0;lane<4;lane++){
                uint32_t *r=rows+lane*f->row_words;WFView v;
                assert(!f->observe(r,&v));WFAction a;
                char scratch[128];
                if(r[WF_STATUS]==WF_RUNNING){
                    assert(!social_public_next(&v,scratch,sizeof scratch,&a));running++;
                }else a=(WFAction){.kind=WF_WAIT,.elapsed_ms=r[WF_ELAPSED]+100u};
                assert(!f->action(r,&a));
            }
            if(!running)break;
            f->batch(rows);
        }
        for(unsigned lane=0;lane<4;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            if(r[WF_STATUS]!=WF_TERMINAL||r[WF_RAW_REWARD]!=1065353216u){
                WFView v;assert(!f->observe(r,&v));
                const char *q=wf_text_get(&v,v.instruction);
                fprintf(stderr,"public failure task=%u seed=%u lane=%u status=%u raw=%08x elapsed=%u count=%u goal_post=%u action=%u required=%u query=%s\n",
                    task,seed,lane,r[WF_STATUS],r[WF_RAW_REWARD],r[WF_ELAPSED],
                    r[32],r[34],r[35],r[36],q?q:"<invalid>");
                for(unsigned post=1;post<=r[32];post++){
                    const WFNode *n=social_public_ref(&v,post*16u+10u);
                    const char *user=n?wf_text_get(&v,n->value):NULL;
                    fprintf(stderr,"  post=%u user=%s mask=%u\n",post,
                        user?user:"<invalid>",r[64u+(post-1u)*256u+252u]);
                }
            }
            assert(r[WF_STATUS]==WF_TERMINAL&&r[WF_RAW_REWARD]==1065353216u);
            solved++;
        }
    }
    free(rows);printf("PASS: %u public-only generated social episodes\n",solved);
    return 0;
}
