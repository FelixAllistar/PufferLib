#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float value;memcpy(&value,&bits,4);return value;}
static unsigned first_eligible(const uint32_t *r,int eligible){
    for(unsigned i=0;i<12u;i++){
        const uint32_t *p=r+64u+i*128u;
        if(!!(p[1]&(1u<<r[35]))==!!eligible)return i;
    }
    return 12u;
}
static unsigned other_item(const uint32_t *r){
    for(unsigned i=0;i<12u;i++)if(i!=r[33]&&i!=r[34])return i;
    return 12u;
}
static int source_ok(const uint32_t *r){
    if(r[32]==0u){
        unsigned selected=0u;
        for(unsigned i=0;i<12u;i++){
            unsigned q=r[64u+i*128u];
            if(q)selected++;
            if(q!=((i==r[33]||i==r[34])?1u:0u))return 0;
        }
        return selected==2u;
    }
    unsigned total=0u,selected=0u;
    for(unsigned i=0;i<12u;i++){
        const uint32_t *p=r+64u+i*128u;
        if(p[0]){selected++;if(!(p[1]&(1u<<r[35])))return 0;total+=p[0];}
    }
    return selected>0u&&total==r[36];
}
static void advance(const WFFamily *f,uint32_t *rows,unsigned *clock,
                    const unsigned refs[4]){
    *clock+=100u;
    for(unsigned lane=0;lane<4u;lane++){
        uint32_t *r=rows+lane*f->row_words;
        WFAction a={.kind=refs[lane]?WF_CLICK:WF_WAIT,
                    .target=refs[lane],.elapsed_ms=*clock};
        assert(!f->action(r,&a));
    }
    f->batch(rows);
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count>=2u);
    uint32_t *rows=malloc(f->row_words*f->batch_lanes*sizeof *rows);assert(rows);
    unsigned checked=0;
    for(unsigned seed=0;seed<64u;seed++)for(unsigned trial=0;trial<6u;trial++){
        memset(rows,0xa5,f->row_words*f->batch_lanes*sizeof *rows);
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2;r[WF_TASK]=1;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
            assert(!f->validate(r));
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        if(seed==0u&&trial==0u){
            uint32_t *r=rows;
            r[64u]=UINT32_MAX;
            assert(!f->validate(r));
            WFAction overflow={.kind=WF_CLICK,.target=17u,.elapsed_ms=100u};
            assert(f->action(r,&overflow));
            r[64u+128u]=1u;
            assert(f->validate(r));
            r[64u]=0u;r[64u+128u]=0u;
            assert(!f->validate(r));
        }
        unsigned clock=0u,refs[4]={0};
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView v;
            assert(!f->validate(r)&&!f->observe(r,&v));
            assert(r[32]<=1u&&r[33]<12u&&r[34]<12u&&r[33]!=r[34]);
            assert(r[35]<5u&&r[36]>=2u&&r[36]<=4u&&r[8191]==0u);
            for(unsigned i=0;i<12u;i++)assert(!r[64u+i*128u]);
            const char *q=wf_text_get(&v,v.instruction);
            assert(q&&(strstr(q,"Order one of each item:")||strstr(q," items that are ")));
        }
        if(trial==5u){
            clock=20000u;
            for(unsigned lane=0;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                WFAction a={.kind=WF_WAIT,.elapsed_ms=clock};assert(!f->action(r,&a));
            }
            f->batch(rows);
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                assert(r[WF_STATUS]==WF_TIMEOUT&&real(r[WF_RAW_REWARD])==-1.0f);
                checked++;
            }
            continue;
        }
        if(trial==4u){
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                refs[lane]=16u*(r[33]+1u); /* Remove at zero must stay zero. */
            }
            advance(f,rows,&clock,refs);
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                assert(r[64u+r[33]*128u]==0u);
            }
        }
        for(unsigned step=0;step<5u;step++){
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                unsigned item=12u;
                if(trial==0u||trial==3u||trial==4u){
                    if(r[32]==0u){if(step==0u)item=r[33];else if(step==1u)item=r[34];
                        else if(trial==3u&&step==2u)item=r[33];}
                    else {if(step<r[36]||(trial==3u&&step==r[36]))
                        item=first_eligible(r,1);}
                }else if(trial==2u&&step==0u){
                    item=r[32]==0u?other_item(r):first_eligible(r,0);
                }
                assert(item<=12u);
                refs[lane]=item==12u?0u:16u*(item+1u)+1u;
            }
            advance(f,rows,&clock,refs);
        }
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[20]=source_ok(r);refs[lane]=1u;
            assert(r[20]==(trial==0u||trial==4u));
        }
        advance(f,rows,&clock,refs);
        for(unsigned lane=0;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            assert(r[WF_STATUS]==WF_TERMINAL);
            assert(real(r[WF_RAW_REWARD])==(r[20]?1.0f:-1.0f));
            float expected=r[20]?1.0f-(float)clock/20000.0f:-1.0f;
            assert(fabsf(real(r[WF_TIMED_REWARD])-expected)<1e-6f);
            checked++;
        }
    }
    free(rows);printf("PASS: %u independent order-food name/type/quantity/timeout cases\n",checked);
    return 0;
}
