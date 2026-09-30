#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

static float real(uint32_t x){float f;memcpy(&f,&x,4);return f;}
static void goal(char *out,const uint32_t *r,unsigned field){
    assert(r[44+field]<=63);
    for(unsigned i=0;i<r[44+field];i++)out[i]=(char)r[1024+field*128+i];
    out[r[44+field]]=0;
}
static void apply(const WFFamily *f,uint32_t *rows,unsigned task,
                  unsigned kind,unsigned target,unsigned arg0,
                  const char *text,unsigned now){
    for(unsigned lane=0;lane<4;lane++){
        uint32_t *r=rows+lane*f->row_words;
        WFAction a={.kind=kind,.target=target,.arg0=arg0,.elapsed_ms=now,
                    .text=text,.text_length=text?strlen(text):0};
        assert(r[WF_TASK]==task&&!f->action(r,&a));
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    for(unsigned lane=0;lane<4;lane++)assert(!f->validate(rows+lane*f->row_words));
}
int main(void){
    assert(!prctl(PR_SET_DUMPABLE,0,0,0,0));
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5);
    uint32_t *rows=calloc(f->row_words*f->batch_lanes,sizeof *rows);assert(rows);
    for(unsigned task=1;task<5;task++)for(unsigned seed=0;seed<32;seed++){
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            memset(r,0,f->row_words*sizeof *r);
            r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;
            r[WF_SEED]=seed*4+lane;
        }
        f->batch(rows);signal(SIGABRT,SIG_DFL);
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;WFView v;
            assert(!f->validate(r)&&!f->observe(r,&v));
            assert((task==1&&v.count==7)||(task==2&&v.count==4)||
                   (task>=3&&v.count==4));
        }
        if(task==1){
            for(unsigned lane=0;lane<4;lane++){
                uint32_t *r=rows+lane*f->row_words;
                WFAction a={.kind=WF_CLICK,.target=r[34]==1?2:1,.elapsed_ms=250};
                assert(!f->action(r,&a));
            }
            f->batch(rows);signal(SIGABRT,SIG_DFL);
            for(unsigned lane=0;lane<4;lane++){
                uint32_t *r=rows+lane*f->row_words;char text[64];goal(text,r,r[35]-1);
                WFAction a={.kind=WF_INSERT,.target=r[35]+3,.text=text,
                    .text_length=strlen(text),.elapsed_ms=500};
                assert(!f->action(r,&a));
            }
            f->batch(rows);signal(SIGABRT,SIG_DFL);
            apply(f,rows,task,WF_CLICK,7,0,NULL,750);
        }else if(task==2){
            for(unsigned lane=0;lane<4;lane++){
                uint32_t *r=rows+lane*f->row_words;
                WFAction a={.kind=WF_SELECT_OPTION,.target=1,.arg0=r[34],
                    .elapsed_ms=250};assert(!f->action(r,&a));
            }
            f->batch(rows);signal(SIGABRT,SIG_DFL);
            for(unsigned lane=0;lane<4;lane++){
                uint32_t *r=rows+lane*f->row_words;
                WFAction a={.kind=WF_CLICK,.target=r[35]+1,.elapsed_ms=500};
                assert(!f->action(r,&a));
            }
            f->batch(rows);
        }else{
            for(unsigned field=0;field<3;field++){
                for(unsigned lane=0;lane<4;lane++){
                    uint32_t *r=rows+lane*f->row_words;char text[64];
                    goal(text,r,field);
                    WFAction a={.kind=WF_INSERT,.target=field+1,.text=text,
                        .text_length=strlen(text),.elapsed_ms=250u*(field+1)};
                    assert(!f->action(r,&a));
                }
                f->batch(rows);signal(SIGABRT,SIG_DFL);
            }
            apply(f,rows,task,WF_CLICK,4,0,NULL,1000);
        }
        for(unsigned lane=0;lane<4;lane++){
            uint32_t *r=rows+lane*f->row_words;
            assert(r[WF_STATUS]==WF_TERMINAL&&real(r[WF_RAW_REWARD])==1.0f);
            assert(real(r[WF_TIMED_REWARD])>0.9f);
        }
    }
    free(rows);
    puts("PASS: 512 independent successful generated cases across four composite tasks");
}
