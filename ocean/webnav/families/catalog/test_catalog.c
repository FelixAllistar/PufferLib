#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float value;memcpy(&value,&bits,4);return value;}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count>=1u);
    uint32_t *rows=malloc(f->row_words*f->batch_lanes*sizeof *rows);assert(rows);
    unsigned checked=0;
    for(unsigned seed=0;seed<64u;seed++)for(unsigned trial=0;trial<4u;trial++){
        memset(rows,0xa5,f->row_words*f->batch_lanes*sizeof *rows);
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
            assert(!f->validate(r));
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView v;
            assert(!f->validate(r)&&!f->observe(r,&v));
            assert(r[32]==1u&&r[33]>=1u&&r[33]<=5u&&r[34]<=2u&&r[35]==5u);
            assert(r[37]==0u&&r[8191]==0u);
            unsigned page=(trial&2u)?1u+(r[33]%5u):r[33];
            r[20]=page;
        }
        unsigned clock=0u;
        for(unsigned step=0;step<4u;step++){
            clock+=100u;
            for(unsigned lane=0;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                unsigned page=r[32],desired=r[20];
                WFAction a={.kind=page==desired?WF_WAIT:WF_CLICK,
                    .target=page==desired?0u:page<desired?page+1u:page-1u,
                    .elapsed_ms=clock};
                assert(!f->action(r,&a));
            }
            f->batch(rows);
        }
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            assert(r[WF_STATUS]==WF_RUNNING&&r[32]==r[20]);
            unsigned property=(trial&1u)?(r[34]+1u)%3u:r[34];
            WFAction a={.kind=WF_CLICK,.target=100u+property,
                .elapsed_ms=clock+100u};
            assert(!f->action(r,&a));
            unsigned grade=2u*(r[32]==r[33])+(property==r[34]);r[21]=grade;
        }
        f->batch(rows);
        for(unsigned lane=0;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            float expected[]={-1.0f,-0.4f,0.4f,1.0f};
            assert(r[WF_STATUS]==WF_TERMINAL&&r[36]==r[21]);
            assert(fabsf(real(r[WF_RAW_REWARD])-expected[r[21]])<1e-6f);
            float timed=r[21]>=2u?expected[r[21]]*
                (1.0f-(float)(clock+100u)/15000.0f):expected[r[21]];
            assert(fabsf(real(r[WF_TIMED_REWARD])-timed)<1e-6f);
            checked++;
        }
    }
    free(rows);printf("PASS: %u independent phone-book grades and dirty resets\n",checked);
    return 0;
}
