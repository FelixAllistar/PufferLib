#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/prctl.h>

#define ROW 8192u
#define LANES 4u
static float real(uint32_t bits){float x;memcpy(&x,&bits,sizeof x);return x;}
static void reset(const WFFamily *f,uint32_t *rows,unsigned seed){
    memset(rows,0,ROW*LANES*sizeof *rows);
    for(unsigned lane=0;lane<LANES;lane++){
        uint32_t *r=rows+lane*ROW;
        r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=2;r[WF_OP]=WF_RESET;
        r[WF_SEED]=seed+lane;
    }
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    for(unsigned lane=0;lane<LANES;lane++)assert(!f->validate(rows+lane*ROW));
}
static void step(const WFFamily *f,uint32_t *rows,unsigned kind,
                 unsigned target,unsigned arg0,unsigned now){
    WFAction a={.kind=kind,.target=target,.arg0=arg0,.elapsed_ms=now};
    assert(!f->action(rows,&a));f->batch(rows);assert(!f->validate(rows));
}
static void insert(const WFFamily *f,uint32_t *rows,const char *text,unsigned now){
    WFAction a={.kind=WF_INSERT,.target=2,.elapsed_ms=now,
                .text=text,.text_length=strlen(text)};
    assert(!f->action(rows,&a));f->batch(rows);assert(!f->validate(rows));
}
static void public_last(const WFView *view,char out[64]){
    const char *content=wf_text_get(view,view->nodes[0].value);assert(content);
    size_t end=strlen(content);while(end&&content[end-1]=='.')end--;
    size_t start=end;while(start&&content[start-1]!=' ')start--;
    assert(end>start&&end-start<64);
    memcpy(out,content+start,end-start);out[end-start]=0;
}
static unsigned public_words(const WFView *view){
    const char *content=wf_text_get(view,view->nodes[0].value);assert(content&&*content);
    unsigned words=1;for(const char *p=content;*p;p++)if(*p==' ')words++;
    return words;
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();
    assert(f&&f->row_words==ROW&&f->batch_lanes==LANES&&f->task_count==4);
    uint32_t rows[ROW*LANES];WFView view;
    reset(f,rows,2);
    assert(rows[35]==1&&rows[32]==125&&rows[33]-rows[34]==200);
    assert(!f->observe(rows,&view)&&view.count==2);
    assert(public_words(&view)>=50&&public_words(&view)<=149);
    assert(view.nodes[0].scroll_y==125&&view.nodes[0].scroll_max_y==200);
    assert(wf_text_get(&view,view.nodes[0].name)&&
           wf_text_get(&view,view.nodes[0].value)&&
           wf_text_get(&view,view.nodes[1].value));
    step(f,rows,WF_SCROLL,1,300,100);
    assert(rows[32]==200&&rows[WF_STATUS]==WF_RUNNING);
    step(f,rows,WF_CLICK,2,0,200);
    assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==1.0f);
    assert(fabsf(real(rows[WF_TIMED_REWARD])-0.98f)<1e-5f);
    reset(f,rows,3);
    step(f,rows,WF_SCROLL,1,10,100);
    step(f,rows,WF_CLICK,2,0,200);
    assert(real(rows[WF_RAW_REWARD])==-1.0f);
    reset(f,rows,3);
    step(f,rows,WF_SCROLL,1,9,100);
    step(f,rows,WF_CLICK,2,0,200);
    assert(real(rows[WF_RAW_REWARD])==1.0f);
    reset(f,rows,2);
    step(f,rows,WF_WAIT,0,0,10000);
    assert(rows[WF_STATUS]==WF_TIMEOUT&&real(rows[WF_RAW_REWARD])==-1.0f);
    WFAction late={.kind=WF_WAIT,.elapsed_ms=10000};
    assert(f->action(rows,&late)<0);
    reset(f,rows,8);rows[WF_TASK]=0;rows[WF_OP]=WF_RESET;f->batch(rows);
    assert(!f->validate(rows)&&rows[36]>=8&&rows[36]<=11&&rows[39]<=2);
    for(unsigned i=0;i<rows[36];i++)if(rows[38]&(1u<<i)){
        step(f,rows,WF_SCROLL,1,i*18,100+i*100);
        step(f,rows,WF_SELECT_OPTION,i+2,1,150+i*100);
    }
    step(f,rows,WF_CLICK,16,0,1500);
    assert(real(rows[WF_RAW_REWARD])==1.0f);
    reset(f,rows,9);rows[WF_TASK]=0;rows[WF_OP]=WF_RESET;f->batch(rows);
    step(f,rows,WF_CLICK,16,0,100);
    assert(real(rows[WF_RAW_REWARD])==-1.0f);
    reset(f,rows,11);rows[WF_TASK]=0;rows[WF_OP]=WF_RESET;f->batch(rows);
    assert(rows[36]==11);
    rows[48]=17;rows[33]=187;rows[34]=88;rows[47]=90;
    assert(!f->validate(rows));
    step(f,rows,WF_SCROLL,1,54,100);
    step(f,rows,WF_SELECT_OPTION,5,1,200);
    assert(rows[32]==51&&(rows[37]&8));
    step(f,rows,WF_SELECT_OPTION,5,0,300);
    assert(rows[32]==51&&!(rows[37]&8));
    reset(f,rows,7);rows[WF_TASK]=1;rows[WF_OP]=WF_RESET;f->batch(rows);
    assert(!f->observe(rows,&view)&&view.count==3);
    assert(public_words(&view)>=20&&public_words(&view)<=149);
    assert(wf_text_get(&view,view.nodes[1].value));
    char answer[64],other[64],punctuated[68];public_last(&view,answer);
    reset(f,rows,8);rows[WF_TASK]=1;rows[WF_OP]=WF_RESET;f->batch(rows);
    assert(!f->observe(rows,&view));public_last(&view,other);
    assert(strcmp(answer,other));
    reset(f,rows,7);rows[WF_TASK]=1;rows[WF_OP]=WF_RESET;f->batch(rows);
    snprintf(punctuated,sizeof punctuated,"!%s.",answer);
    insert(f,rows,punctuated,100);
    step(f,rows,WF_CLICK,3,0,200);
    assert(real(rows[WF_RAW_REWARD])==1.0f);
    reset(f,rows,7);rows[WF_TASK]=1;rows[WF_OP]=WF_RESET;f->batch(rows);
    insert(f,rows,"END",100);
    step(f,rows,WF_CLICK,3,0,200);
    assert(real(rows[WF_RAW_REWARD])==-1.0f);
    reset(f,rows,3);rows[WF_TASK]=3;rows[WF_OP]=WF_RESET;f->batch(rows);
    assert(rows[40]==0&&rows[41]==0);
    step(f,rows,WF_CLICK,3,0,100);
    assert(real(rows[WF_RAW_REWARD])==1.0f);
    reset(f,rows,4);rows[WF_TASK]=3;rows[WF_OP]=WF_RESET;f->batch(rows);
    assert(!f->observe(rows,&view));
    assert(public_words(&view)>=150&&public_words(&view)<=299);
    WFAction disabled={.kind=WF_INSERT,.target=2,.text="x",.text_length=1,.elapsed_ms=100};
    assert(f->action(rows,&disabled)<0);
    step(f,rows,WF_SCROLL,1,1000,100);
    assert(rows[41]==1&&rows[32]==915);
    char name[64];unsigned j=0;while(j<rows[43]){name[j]=(char)rows[4736+j];j++;}name[j]=0;
    insert(f,rows,name,200);
    step(f,rows,WF_CLICK,4,0,300);
    assert(real(rows[WF_RAW_REWARD])==1.0f);
    reset(f,rows,5);rows[WF_TASK]=3;rows[WF_OP]=WF_RESET;f->batch(rows);
    step(f,rows,WF_CLICK,3,0,100);
    assert(real(rows[WF_RAW_REWARD])==-1.0f);
    reset(f,rows,4);rows[WF_TASK]=3;rows[WF_OP]=WF_RESET;f->batch(rows);
    WFView private_view;assert(!f->observe(rows,&view));
    rows[4736]='X';assert(!f->observe(rows,&private_view));
    assert(!memcmp(&view,&private_view,sizeof view));
    puts("scroll-text-2 source regression passed");return 0;
}
