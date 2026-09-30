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
static void copy_units(char *out,const uint32_t *p,unsigned cap){
    unsigned i=0;for(;i+1u<cap&&p[i];i++)out[i]=(char)p[i];out[i]=0;
}
static int source_ok(const char *answer,const char *expected){
    char clean[128];unsigned n=0;
    for(unsigned i=0;answer[i];i++)
        if(isalnum((unsigned char)answer[i]))clean[n++]=answer[i];
    clean[n]=0;return !strcmp(clean,expected);
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count>=1u);
    uint32_t *rows=malloc(f->row_words*f->batch_lanes*sizeof *rows);
    assert(rows);unsigned checked=0u,variation=0u,first_index=0u;
    char first_expected[64]="";
    for(unsigned seed=0;seed<64u;seed++)for(unsigned trial=0;trial<5u;trial++){
        memset(rows,0xa5,f->row_words*f->batch_lanes*sizeof *rows);
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2u;r[WF_TASK]=0u;r[WF_OP]=WF_RESET;
            r[WF_SEED]=seed+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        char answers[4][128]={{0}},expected[4][64]={{0}};
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView before,after;
            assert(!f->validate(r)&&!f->observe(r,&before));
            assert(r[WF_STATUS]==WF_RUNNING&&r[WF_DEADLINE]==10000u&&
                   r[33]==6u&&r[32]<6u&&r[34]==0u&&r[8191]==0u);
            copy_units(expected[lane],r+1024u,64u);
            assert(strlen(expected[lane])==r[38]);
            if(trial==0u&&lane==0u){
                if(seed==0u){first_index=r[32];
                    snprintf(first_expected,sizeof first_expected,"%s",expected[lane]);}
                else variation+=(r[32]!=first_index||
                    strcmp(expected[lane],first_expected)!=0);
            }
            uint32_t saved=r[1024u];r[1024u]=saved==65u?66u:65u;
            assert(!f->validate(r)&&!f->observe(r,&after)&&
                   !memcmp(&before,&after,sizeof before));
            r[1024u]=saved;
            if(trial==0u)snprintf(answers[lane],sizeof answers[lane],"%s",expected[lane]);
            if(trial==1u)snprintf(answers[lane],sizeof answers[lane]," %s! ",expected[lane]);
            if(trial==2u){
                snprintf(answers[lane],sizeof answers[lane],"%s",expected[lane]);
                answers[lane][0]=islower((unsigned char)answers[lane][0])?
                    (char)toupper((unsigned char)answers[lane][0]):
                    (char)tolower((unsigned char)answers[lane][0]);
            }
        }
        if(trial==3u){
            for(unsigned lane=0;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                WFAction a={.kind=WF_WAIT,.elapsed_ms=10000u};
                assert(!f->action(r,&a));
            }
            f->batch(rows);
        }else{
            for(unsigned lane=0;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                WFAction a={.kind=WF_CLICK,.target=1u,.elapsed_ms=100u};
                assert(!f->action(r,&a));
            }
            f->batch(rows);
            if(trial!=4u){
                for(unsigned lane=0;lane<4u;lane++){
                    uint32_t *r=rows+lane*f->row_words;
                    WFAction a={.kind=WF_INSERT,.text=answers[lane],
                        .text_length=strlen(answers[lane]),.elapsed_ms=200u};
                    assert(!f->action(r,&a));
                }
                f->batch(rows);
            }
            for(unsigned lane=0;lane<4u;lane++){
                uint32_t *r=rows+lane*f->row_words;
                WFAction a={.kind=WF_CLICK,.target=2u,.elapsed_ms=300u};
                assert(!f->action(r,&a));
            }
            f->batch(rows);
        }
        for(unsigned lane=0;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            int good=trial!=3u&&source_ok(answers[lane],expected[lane]);
            assert(r[WF_STATUS]==(trial==3u?WF_TIMEOUT:WF_TERMINAL));
            assert(real(r[WF_RAW_REWARD])==(good?1.0f:-1.0f));
            float timed=good?1.0f-300.0f/10000.0f:-1.0f;
            assert(fabsf(real(r[WF_TIMED_REWARD])-timed)<1e-6f);
            checked++;
        }
    }
    assert(variation>0u);
    free(rows);printf("PASS: %u independent find-word outcomes and dirty resets\n",checked);
    return 0;
}
