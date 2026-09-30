#include "../common/family_api.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t bits){float value;memcpy(&value,&bits,4);return value;}
static void advance(const WFFamily *f,uint32_t *rows,unsigned *clock,
                    WFAction actions[4]){
    *clock+=100u;
    for(unsigned lane=0;lane<4u;lane++){
        actions[lane].elapsed_ms=*clock;
        assert(!f->action(rows+lane*f->row_words,&actions[lane]));
    }
    f->batch(rows);
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==3u);
    uint32_t *rows=malloc(f->row_words*f->batch_lanes*sizeof *rows);assert(rows);
    unsigned checked=0;
    for(unsigned seed=0;seed<64u;seed++)for(unsigned trial=0;trial<7u;trial++){
        memset(rows,0xa5,f->row_words*f->batch_lanes*sizeof *rows);
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            r[WF_VERSION]=2;r[WF_TASK]=2;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
            assert(!f->validate(r));
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        WFAction a[4]={{0}};char text[4][128];unsigned clock=0u;
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView before,after;
            assert(!f->validate(r)&&!f->observe(r,&before));
            assert(r[32]==0u&&r[33]<9u&&r[34]>=1u&&r[34]<=63u&&
                   r[35]==0u&&r[38]==0u&&r[39]==0u&&r[8191]==0u);
            unsigned target=r[33];r[33]=(target+1u)%9u;
            assert(!f->observe(r,&after)&&!memcmp(&before,&after,sizeof before));
            r[33]=target;
            const char *q=wf_text_get(&before,before.instruction);
            assert(q&&strstr(q,"press \"Search\""));
            for(unsigned i=0;i<r[34];i++)text[lane][i]=(char)r[2700+i];
            text[lane][r[34]]=0;
        }
        if(trial==5u){
            for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_WAIT,.elapsed_ms=20000u};
            for(unsigned lane=0;lane<4u;lane++)assert(!f->action(rows+lane*f->row_words,&a[lane]));
            f->batch(rows);
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                assert(r[WF_STATUS]==WF_TIMEOUT&&real(r[WF_RAW_REWARD])==-1.0f);checked++;
            }
            continue;
        }
        if(trial==6u){
            for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_CLICK,.target=1u};
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_INSERT,.text="bad",.text_length=3u};
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_CLICK,.target=2u};
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++){
                assert(rows[lane*f->row_words+39u]==0u);
                a[lane]=(WFAction){.kind=WF_CLICK,.target=1u};
            }
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_SELECT_ALL};
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                a[lane]=(WFAction){.kind=WF_INSERT,.text=text[lane],.text_length=r[34]};
            }
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_CLICK,.target=11u};
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                assert(r[32]==2u&&r[39]==1u);
                unsigned page=r[33]/3u+1u;
                a[lane]=(WFAction){.kind=page==2u?WF_WAIT:WF_CLICK,
                    .target=page==2u?0u:9u+page};
            }
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                a[lane]=(WFAction){.kind=WF_CLICK,.target=100u+r[33]};
            }
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                assert(r[WF_STATUS]==WF_TERMINAL&&
                       real(r[WF_RAW_REWARD])==1.0f);
                assert(fabsf(real(r[WF_TIMED_REWARD])-
                             (1.0f-(float)clock/20000.0f))<1e-6f);
                checked++;
            }
            continue;
        }
        for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_CLICK,.target=1u};
        advance(f,rows,&clock,a);
        if(trial==4u){
            for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_INSERT,.text="bad",.text_length=3u};
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_CLICK,.target=2u};
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++){
                const uint32_t *r=rows+lane*f->row_words;
                assert(r[32]==1u&&r[39]==0u);
                a[lane]=(WFAction){.kind=WF_CLICK,.target=1u};
            }
            advance(f,rows,&clock,a);
            for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_SELECT_ALL};
            advance(f,rows,&clock,a);
        }
        for(unsigned lane=0;lane<4u;lane++){
            uint32_t *r=rows+lane*f->row_words;
            if(trial==1u)a[lane]=(WFAction){.kind=WF_INSERT,.text="zzzz",.text_length=4u};
            else{
                if(trial==3u)for(unsigned i=0;i<r[34];i++)text[lane][i]=(char)tolower((unsigned char)text[lane][i]);
                a[lane]=(WFAction){.kind=WF_INSERT,.text=text[lane],.text_length=r[34]};
            }
        }
        advance(f,rows,&clock,a);
        for(unsigned lane=0;lane<4u;lane++)a[lane]=(WFAction){.kind=WF_CLICK,.target=2u};
        advance(f,rows,&clock,a);
        for(unsigned lane=0;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            assert(r[32]==1u&&r[39]==(trial!=1u));
            unsigned page=1u+r[33]/3u;
            a[lane]=(WFAction){.kind=page==1u?WF_WAIT:WF_CLICK,
                .target=page==1u?0u:9u+page};
        }
        advance(f,rows,&clock,a);
        for(unsigned lane=0;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            unsigned target=r[33];
            unsigned chosen=trial==2u?(target/3u)*3u+(target+1u)%3u:target;
            a[lane]=(WFAction){.kind=WF_CLICK,
                .target=trial==1u?200u:100u+chosen};
        }
        advance(f,rows,&clock,a);
        for(unsigned lane=0;lane<4u;lane++){
            const uint32_t *r=rows+lane*f->row_words;
            unsigned success=trial!=1u&&trial!=2u;
            assert(r[WF_STATUS]==WF_TERMINAL);
            assert(real(r[WF_RAW_REWARD])==(success?1.0f:-1.0f));
            float expected=success?1.0f-(float)clock/20000.0f:-1.0f;
            assert(fabsf(real(r[WF_TIMED_REWARD])-expected)<1e-6f);checked++;
        }
    }
    free(rows);printf("PASS: %u independent search text, requery, wrong result and timeout cases\n",checked);
    return 0;
}
