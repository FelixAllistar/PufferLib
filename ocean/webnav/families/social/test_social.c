#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/prctl.h>

static float as_float(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static unsigned same_name(const uint32_t *r,unsigned a,unsigned b){
    const uint32_t *x=r+64u+(a-1u)*256u+64u;
    const uint32_t *y=r+64u+(b-1u)*256u+64u;
    for(unsigned i=0;i<64;i++)if(x[i]!=y[i])return 0;
    return 1;
}
static int source_submit(const uint32_t *r,unsigned task){
    unsigned selected=0;
    for(unsigned i=1;i<=r[32];i++)for(unsigned slot=0;slot<4;slot++){
        unsigned active=!!(r[64u+(i-1u)*256u+252u]&(1u<<slot));
        unsigned wanted=same_name(r,i,r[34])&&slot==r[35];
        if(task==1u){if(active!=wanted)return 0;}
        else if(active){if(!wanted)return 0;selected++;}
    }
    return task==1u||selected==r[36];
}
static void advance(const WFFamily *f,uint32_t *rows,unsigned *clock,
                    const unsigned refs[4]){
    *clock+=100u;
    for(unsigned lane=0;lane<4;lane++){
        uint32_t *r=rows+lane*f->row_words;
        WFAction a={.kind=refs[lane]?WF_CLICK:WF_WAIT,
                    .target=refs[lane],.elapsed_ms=*clock};
        assert(!f->action(r,&a));
    }
    f->batch(rows);
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();
    assert(f&&f->task_count==3u&&f->row_words==8192u&&f->batch_lanes==4u);
    uint32_t *rows=calloc(f->row_words*f->batch_lanes,4);assert(rows);
    unsigned checked=0;
    for(unsigned seed=0;seed<128;seed++){
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2;r[WF_TASK]=0;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
            assert(!f->validate(r));
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView v;
            assert(!f->validate(r)&&!f->observe(r,&v));
            assert(r[32]>=5u&&r[32]<=9u&&r[34]>=1u&&r[34]<=r[32]&&r[35]<=8u);
            const char *q=wf_text_get(&v,v.instruction);assert(q&&strstr(q,"For the user "));
            unsigned post=(seed%2u)?r[34]:1u;
            unsigned slot=r[35]<3u?r[35]:r[35]+1u;
            if(slot>=4u){
                WFAction open={.kind=WF_CLICK,.target=post*16u+3u,.elapsed_ms=100u};
                assert(!f->action(r,&open));
            }else{
                WFAction wait={.kind=WF_WAIT,.elapsed_ms=100u};assert(!f->action(r,&wait));
            }
            unsigned expected_menu=slot>=4u?post:0u;
            r[20]=expected_menu;
        }
        f->batch(rows);
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            assert(r[WF_STATUS]==WF_RUNNING&&r[33]==r[20]);
            unsigned post=(seed%2u)?r[34]:1u;
            unsigned slot=r[35]<3u?r[35]:r[35]+1u;
            unsigned success=same_name(r,post,r[34]);
            WFAction click={.kind=WF_CLICK,.target=post*16u+slot,.elapsed_ms=200u};
            assert(!f->action(r,&click));r[21]=success;
        }
        f->batch(rows);
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            assert(r[WF_STATUS]==WF_TERMINAL);
            assert(as_float(r[WF_RAW_REWARD])==(r[21]?1.0f:-1.0f));
            assert(fabsf(as_float(r[WF_TIMED_REWARD])-(r[21]?(1.0f-200.0f/15000.0f):-1.0f))<1e-6f);
            checked++;
        }
    }
    unsigned multi=0;
    for(unsigned task=1;task<=2;task++)for(unsigned seed=0;seed<64;seed++)
    for(unsigned trial=0;trial<5;trial++){
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
            assert(!f->validate(r));
        }
        f->batch(rows);unsigned clock=0u,refs[4]={0};
        for(unsigned lane=0;lane<4;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            assert(!f->validate(r)&&r[32]>=6u&&r[32]<=11u&&r[36]>=1u&&r[36]<=r[32]);
            WFView v;assert(!f->observe(r,&v));
            const char *q=wf_text_get(&v,v.instruction);assert(q&&strstr(q,"Submit"));
            refs[lane]=16u*r[34]+r[35];
        }
        if(trial==4u){advance(f,rows,&clock,refs);advance(f,rows,&clock,refs);}
        for(unsigned index=1;index<=11;index++){
            for(unsigned lane=0;lane<4;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                refs[lane]=0u;if(index>r[32])continue;
                unsigned matched=same_name(r,index,r[34]);
                if(trial==0u||trial==4u){
                    unsigned earlier=0u;
                    for(unsigned j=1;j<index;j++)earlier+=same_name(r,j,r[34]);
                    if(matched&&(task==1u||earlier<r[36]))refs[lane]=16u*index+r[35];
                }else if(trial==2u&&index==r[34])refs[lane]=16u*index+(r[35]+1u)%4u;
                else if(trial==3u&&!matched){
                    unsigned earlier=0u;for(unsigned j=1;j<index;j++)earlier+=!same_name(r,j,r[34]);
                    if(!earlier)refs[lane]=16u*index+r[35];
                }
            }
            advance(f,rows,&clock,refs);
        }
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[22]=source_submit(r,task);
            assert(r[22]==(trial==0u||trial==4u));
            refs[lane]=1u;
        }
        advance(f,rows,&clock,refs);
        for(unsigned lane=0;lane<4;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            assert(r[WF_STATUS]==WF_TERMINAL);
            assert(as_float(r[WF_RAW_REWARD])==(r[22]?1.0f:-1.0f));
            assert(fabsf(as_float(r[WF_TIMED_REWARD])-
                  (r[22]?1.0f-(float)clock/20000.0f:-1.0f))<1e-6f);
            multi++;
        }
    }
    free(rows);printf("PASS: %u direct and %u multi-post independent transitions across four lanes\n",checked,multi);
    return 0;
}
