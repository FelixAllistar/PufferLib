#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#define ROW 4096u
#define LANES 8u
#define NB 64u
#define NS 48u

static float f32(uint32_t bits){float x;memcpy(&x,&bits,sizeof x);return x;}
static void run(const WFFamily *f,uint32_t *rows){f->batch(rows);}
static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    memset(rows,0,LANES*ROW*sizeof *rows);
    for(unsigned lane=0;lane<LANES;lane++){
        uint32_t *r=rows+lane*ROW;r[WF_VERSION]=2;r[WF_TASK]=task;r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
    }
    run(f,rows);
    for(unsigned lane=0;lane<LANES;lane++)assert(!f->validate(rows+lane*ROW));
}
static void act(const WFFamily *f,uint32_t *rows,unsigned kind,unsigned ref,
                unsigned arg0,unsigned arg1,const char *text,unsigned elapsed){
    WFAction a={.kind=kind,.target=ref,.arg0=arg0,.arg1=arg1,.elapsed_ms=elapsed,
                .text=text,.text_length=text?strlen(text):0};
    assert(!f->action(rows,&a));run(f,rows);assert(!f->validate(rows));
}
static int private_noninterference(const WFFamily *f,const uint32_t *row){
    uint32_t copy[ROW];WFView before,after;memcpy(copy,row,sizeof copy);
    if(f->observe(row,&before))return 0;
    copy[36]^=0x13579u;copy[37]^=0x2468u;copy[38]^=1;copy[39]^=0x55u;copy[41]^=0xaa55u;
    for(unsigned i=0;i<copy[33];i++)copy[NB+i*NS+2]^=1;
    if(f->observe(copy,&after))return 0;
    return !memcmp(&before,&after,sizeof before);
}
static void terminal(const uint32_t *r,float raw){
    assert(r[WF_STATUS]==WF_TERMINAL);
    assert(fabsf(f32(r[WF_RAW_REWARD])-raw)<1e-6f);
}

int main(void){
    struct rlimit no_core={0,0};setrlimit(RLIMIT_CORE,&no_core);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==9&&f->row_words==ROW&&f->batch_lanes==LANES);
    uint32_t *rows=calloc(LANES*ROW,sizeof *rows);assert(rows);WFView view;
    for(unsigned task=0;task<9;task++)for(unsigned seed=1;seed<=64;seed+=7){
        reset(f,rows,task,seed);
        for(unsigned lane=0;lane<LANES;lane++){
            const uint32_t *r=rows+lane*ROW;assert(!f->observe(r,&view));
            assert(view.count==r[33]&&view.instruction.length>0&&private_noninterference(f,r));
        }
    }

    /* Ascending sequence: visible 1 reveals the five shapes, then each next
       number removes one remaining overlay. The last click earns the scaled win. */
    reset(f,rows,0,11);
    act(f,rows,WF_CLICK,1,0,0,NULL,100);
    assert(!f->observe(rows,&view)&&view.count==4&&view.nodes[0].ref==2);
    WFAction stale={.kind=WF_CLICK,.target=1,.elapsed_ms=150};assert(f->action(rows,&stale)<0);
    for(unsigned ref=2;ref<=5;ref++)act(f,rows,WF_CLICK,ref,0,0,NULL,ref*100);
    terminal(rows,1.0f);assert(f32(rows[WF_TIMED_REWARD])>0.9f);

    /* Greatest uses a hidden correct index; selecting a card exposes its value. */
    reset(f,rows,1,22);unsigned greatest=0;
    for(unsigned i=0;i<3;i++)if(rows[NB+i*NS+2])greatest=i+1;
    assert(greatest);act(f,rows,WF_CLICK,greatest,0,0,NULL,100);
    assert(!f->observe(rows,&view));assert(view.nodes[greatest-1].value.length>0);
    act(f,rows,WF_CLICK,4,0,0,NULL,200);terminal(rows,1.0f);
    reset(f,rows,1,23);greatest=0;
    for(unsigned i=0;i<3;i++)if(!rows[NB+i*NS+2]){greatest=i+1;break;}
    assert(greatest);act(f,rows,WF_CLICK,greatest,0,0,NULL,100);
    act(f,rows,WF_CLICK,4,0,0,NULL,200);terminal(rows,0.1f);

    /* Generated candidates are produced by the Bend transition. Keep asking
       until the visible value meets its public request. */
    unsigned got=0;
    for(unsigned seed=1;seed<400&&!got;seed++){
        reset(f,rows,2,seed);unsigned mode=rows[38],limit=rows[37];
        for(unsigned n=1;n<=24&&!got;n++){
            act(f,rows,WF_CLICK,1,0,0,NULL,n*100);
            unsigned candidate=rows[41];
            got=(mode==0?candidate<limit:mode==1?candidate>limit:mode==2?(candidate%2)==1:(candidate%2)==0);
        }
        if(got)act(f,rows,WF_CLICK,2,0,0,NULL,3000);
    }
    assert(got);terminal(rows,1.0f);

    /* The pinned HTML accepts an ungenerated value in even mode because its
       empty display reaches the original even-number predicate. Odd mode
       still rejects the absent value. */
    unsigned empty_even=0,empty_odd=0;
    for(unsigned seed=1;seed<400&&(!empty_even||!empty_odd);seed++){
        reset(f,rows,2,seed);
        if(rows[38]==3&&!empty_even){
            act(f,rows,WF_CLICK,2,0,0,NULL,100);terminal(rows,1.0f);empty_even=1;
        }else if(rows[38]==2&&!empty_odd){
            act(f,rows,WF_CLICK,2,0,0,NULL,100);terminal(rows,-1.0f);empty_odd=1;
        }
    }
    assert(empty_even&&empty_odd);

    reset(f,rows,3,33);char answer[32];snprintf(answer,sizeof answer,"%u",rows[36]);
    act(f,rows,WF_INSERT,1,0,0,answer,100);act(f,rows,WF_CLICK,2,0,0,NULL,200);terminal(rows,1.0f);

    reset(f,rows,4,44);act(f,rows,WF_CLICK,1,rows[36],rows[37],NULL,100);terminal(rows,1.0f);

    reset(f,rows,5,55);
    for(unsigned i=0;i<28;i++)if(rows[NB+i*NS+2])act(f,rows,WF_CLICK,i+1,0,0,NULL,100+i*10);
    act(f,rows,WF_CLICK,29,0,0,NULL,500);terminal(rows,1.0f);

    reset(f,rows,6,66);
    for(unsigned row=0;row<3;row++){
        unsigned ref=rows[NB+(2*row)*NS+2]?2*row+1:2*row+2;
        act(f,rows,WF_CLICK,ref,0,0,NULL,100+row*100);
    }
    act(f,rows,WF_CLICK,7,0,0,NULL,500);terminal(rows,1.0f);

    /* With one wrong choice and two empty rows, the original's truthy empty
       selection object charges -1/3 for every row. */
    reset(f,rows,6,67);
    unsigned wrong_row_zero=rows[NB+2]?2:1;
    act(f,rows,WF_CLICK,wrong_row_zero,0,0,NULL,100);
    act(f,rows,WF_CLICK,7,0,0,NULL,200);terminal(rows,-1.0f);

    for(unsigned task=7;task<=8;task++){
        reset(f,rows,task,70+task);snprintf(answer,sizeof answer,"%d",(int32_t)rows[39]);
        act(f,rows,WF_INSERT,1,0,0,answer,100);act(f,rows,WF_CLICK,2,0,0,NULL,200);terminal(rows,1.0f);
    }

    /* Feedback remains running on an incorrect guess, and terminal reward is
       bounded by the original success-only time scaling. */
    reset(f,rows,3,81);unsigned wrong=rows[36]?0:1;snprintf(answer,sizeof answer,"%u",wrong);
    act(f,rows,WF_INSERT,1,0,0,answer,100);act(f,rows,WF_CLICK,2,0,0,NULL,200);
    assert(rows[WF_STATUS]==WF_RUNNING&&rows[40]==(wrong<rows[36]?1u:2u)&&rows[34]==wrong);
    act(f,rows,WF_SELECT_ALL,1,0,0,NULL,300);act(f,rows,WF_INSERT,1,0,0,"9",400);
    assert(!f->observe(rows,&view));
    char shown[64];snprintf(shown,sizeof shown,"The number is %s than %u.",wrong<rows[36]?"higher":"lower",wrong);
    assert(!strcmp(wf_text_get(&view,view.nodes[2].value),shown));
    reset(f,rows,3,82);act(f,rows,WF_CLICK,2,0,0,NULL,100);
    assert(rows[WF_STATUS]==WF_RUNNING&&rows[40]==4);
    assert(!f->observe(rows,&view)&&view.nodes[2].value.length==0);

    for(unsigned task=0;task<9;task++){
        reset(f,rows,task,900+task);
        act(f,rows,WF_WAIT,0,0,0,NULL,rows[WF_DEADLINE]);
        assert(rows[WF_STATUS]==WF_TIMEOUT&&f32(rows[WF_RAW_REWARD])==-1.0f);
        assert(f32(rows[WF_TIMED_REWARD])==-1.0f);
    }

    free(rows);
    puts("PASS: nine numeric tasks, eight independent lanes, secret noninterference, generated success paths, feedback, and reward deadlines");
    return 0;
}
