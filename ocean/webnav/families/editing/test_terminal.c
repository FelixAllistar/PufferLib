#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float x;memcpy(&x,&bits,4);return x;}
static void filename(char *out,const uint32_t *r,unsigned index){
    unsigned n=r[56u+index];assert(n<128u);
    for(unsigned i=0;i<n;i++)out[i]=(char)r[512u+index*128u+i];
    out[n]=0;
}
static unsigned matching(const uint32_t *r,int want_match){
    for(unsigned i=0;i<r[33];i++)
        if((r[48u+i]==r[32])==want_match)return i;
    return r[33];
}
static void batch(const WFFamily *f,uint32_t *rows,
                  WFAction acts[4],unsigned ms){
    for(unsigned lane=0;lane<4u;lane++){
        acts[lane].elapsed_ms=ms;
        assert(!f->action(rows+lane*f->row_words,&acts[lane]));
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5u);
    uint32_t *rows=malloc(f->row_words*f->batch_lanes*sizeof *rows);
    assert(rows);unsigned checked=0u,wrong_ext=0u,variation=0u;
    unsigned first_goal=0u;char names[4][128],commands[4][128];
    for(unsigned seed=0u;seed<64u;seed++)
    for(unsigned trial=0u;trial<8u;trial++){
        memset(rows,0xa5,f->row_words*f->batch_lanes*sizeof *rows);
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2u;r[WF_TASK]=4u;
            r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        WFAction acts[4]={{0}};
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView before,after;
            assert(!f->validate(r)&&!f->observe(r,&before));
            assert(r[WF_DEADLINE]==20000u&&r[33]>=3u&&r[33]<=5u&&
                   r[34]==0u&&r[35]==0u&&r[8191]==0u);
            if(!trial&&lane==0u){
                if(!seed)first_goal=r[32];
                else variation+=(r[32]!=first_goal);
            }
            unsigned saved=r[32];r[32]=(saved+1u)%6u;
            assert(!f->validate(r)&&!f->observe(r,&after)&&
                   !memcmp(&before,&after,sizeof before));
            r[32]=saved;
            unsigned index=matching(r,1);assert(index<r[33]);
            filename(names[lane],r,index);
            snprintf(commands[lane],sizeof commands[lane],"rm %s",names[lane]);
        }
        if(trial==6u){
            for(unsigned lane=0;lane<4u;lane++)acts[lane]=(WFAction){.kind=WF_WAIT};
            batch(f,rows,acts,20000u);
        }else{
            for(unsigned lane=0;lane<4u;lane++)
                acts[lane]=(WFAction){.kind=WF_CLICK,.target=1u};
            batch(f,rows,acts,100u);
            if(trial<=4u){
                static const char *const preliminary[]={"ls","","rm nonexistent",
                    "rm *","help"};
                if(trial!=1u){
                    for(unsigned lane=0;lane<4u;lane++){
                        acts[lane]=(WFAction){.kind=WF_INSERT,
                            .text=preliminary[trial],
                            .text_length=strlen(preliminary[trial])};
                    }
                    batch(f,rows,acts,200u);
                    for(unsigned lane=0;lane<4u;lane++)
                        acts[lane]=(WFAction){.kind=WF_ENTER};
                    batch(f,rows,acts,300u);
                    for(unsigned lane=0;lane<4u;lane++){
                        const uint32_t *r=rows+lane*f->row_words;
                        assert(r[WF_STATUS]==WF_RUNNING&&
                               r[36]==(trial==0u?2u:trial==2u?6u:
                                        trial==3u?5u:1u));
                    }
                }
            }
            if(trial==1u){
                for(unsigned lane=0;lane<4u;lane++){
                    const uint32_t *r=rows+lane*f->row_words;
                    unsigned other=matching(r,0);
                    if(other<r[33]){
                        char wrong[128];filename(wrong,r,other);
                        snprintf(commands[lane],sizeof commands[lane],"rm %s",wrong);
                        wrong_ext++;
                    }else snprintf(commands[lane],sizeof commands[lane],"exit");
                }
            }else if(trial==7u)
                for(unsigned lane=0;lane<4u;lane++)
                    snprintf(commands[lane],sizeof commands[lane],"exit");
            if(trial==5u)
                for(unsigned lane=0;lane<4u;lane++)
                    strncat(commands[lane],"x",sizeof commands[lane]-strlen(commands[lane])-1u);
            unsigned typed=trial==1u||trial==5u||trial==7u?200u:400u;
            for(unsigned lane=0;lane<4u;lane++){
                acts[lane]=(WFAction){.kind=WF_INSERT,.text=commands[lane],
                    .text_length=strlen(commands[lane])};
            }
            batch(f,rows,acts,typed);
            if(trial==5u){
                for(unsigned lane=0;lane<4u;lane++)
                    acts[lane]=(WFAction){.kind=WF_BACKSPACE};
                batch(f,rows,acts,300u);
            }
            unsigned submitted=trial==5u?400u:typed+100u;
            for(unsigned lane=0;lane<4u;lane++)
                acts[lane]=(WFAction){.kind=WF_ENTER};
            batch(f,rows,acts,submitted);
        }
        for(unsigned lane=0;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            int good=trial!=1u&&trial!=6u&&trial!=7u;
            assert(r[WF_STATUS]==(trial==6u?WF_TIMEOUT:WF_TERMINAL));
            assert(real(r[WF_RAW_REWARD])==(good?1.0f:-1.0f));
            unsigned ms=trial==5u?400u:
                trial==1u||trial==7u?300u:500u;
            float timed=good?1.0f-(float)ms/20000.0f:-1.0f;
            assert(fabsf(real(r[WF_TIMED_REWARD])-timed)<1e-6f);
            checked++;
        }
    }
    assert(wrong_ext>0u&&variation>0u);
    free(rows);printf("PASS: %u independent terminal command/timeout outcomes\n",checked);
    return 0;
}
