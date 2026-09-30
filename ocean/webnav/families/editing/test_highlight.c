#include "../common/family_api.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float x;memcpy(&x,&bits,4);return x;}
static int source_equal(const uint32_t *r){
    char got[512],wanted[1024];unsigned ng=0u,nw=0u;
    for(unsigned i=r[34];i<r[35];i++){
        unsigned c=r[512u+i];if(!isspace(c))got[ng++]=(char)c;
    }
    got[ng]=0;
    for(unsigned i=0;i<r[37];i++){
        unsigned c=r[2048u+i];if(!isspace(c))wanted[nw++]=(char)c;
    }
    wanted[nw]=0;return !strcmp(got,wanted);
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count>=3u);
    uint32_t *rows=malloc(f->row_words*f->batch_lanes*sizeof *rows);
    assert(rows);unsigned checked=0u,variation=0u,first_target=0u;
    for(unsigned task=1u;task<=2u;task++)
      for(unsigned seed=0u;seed<64u;seed++)
      for(unsigned trial=0u;trial<5u;trial++){
        memset(rows,0xa5,f->row_words*f->batch_lanes*sizeof *rows);
        for(unsigned lane=0u;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2u;r[WF_TASK]=task;r[WF_OP]=WF_RESET;
            r[WF_SEED]=seed+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        unsigned begin[4],end[4];
        for(unsigned lane=0u;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView before,after;
            assert(!f->validate(r)&&!f->observe(r,&before));
            assert(r[WF_STATUS]==WF_RUNNING&&r[WF_DEADLINE]==10000u&&
                   r[33]==(task==1u?1u:3u)&&r[8191]==0u);
            if(task==2u&&trial==0u&&lane==0u){
                if(!seed)first_target=r[32];
                else variation+=(r[32]!=first_target);
            }
            unsigned saved=r[2048u];r[2048u]=saved==65u?66u:65u;
            assert(!f->validate(r)&&!f->observe(r,&after)&&
                   !memcmp(&before,&after,sizeof before));
            r[2048u]=saved;
            unsigned target=r[32];
            begin[lane]=r[40u+2u*target];
            end[lane]=begin[lane]+r[41u+2u*target];
            if(trial==1u)end[lane]--;
            if(trial==2u&&task==2u){
                target=(target+1u)%3u;
                begin[lane]=r[40u+2u*target];
                end[lane]=begin[lane]+r[41u+2u*target];
            }else if(trial==2u)begin[lane]++;
            if(trial==3u)begin[lane]=end[lane];
        }
        if(trial==4u){
            for(unsigned lane=0u;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                WFAction a={.kind=WF_WAIT,.elapsed_ms=10000u};
                assert(!f->action(r,&a));
            }
            f->batch(rows);
        }else{
            for(unsigned lane=0u;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                WFAction a={.kind=WF_SELECT_RANGE,.arg0=begin[lane],
                    .arg1=end[lane],.elapsed_ms=100u};
                assert(!f->action(r,&a));
            }
            f->batch(rows);
            for(unsigned lane=0u;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                assert(r[34]==begin[lane]&&r[35]==end[lane]);
                WFAction a={.kind=WF_CLICK,.target=1u,.elapsed_ms=200u};
                assert(!f->action(r,&a));
            }
            f->batch(rows);
        }
        for(unsigned lane=0u;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            int good=trial!=4u&&source_equal(r);
            assert(r[WF_STATUS]==(trial==4u?WF_TIMEOUT:WF_TERMINAL));
            assert(real(r[WF_RAW_REWARD])==(good?1.0f:-1.0f));
            float timed=good?1.0f-200.0f/10000.0f:-1.0f;
            assert(fabsf(real(r[WF_TIMED_REWARD])-timed)<1e-6f);
            checked++;
        }
    }
    assert(variation>0u);
    free(rows);printf("PASS: %u independent highlight range outcomes\n",checked);
    return 0;
}
