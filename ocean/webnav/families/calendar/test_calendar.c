#include "../common/family_api.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 1024u
#define LANES 4u
static float real(uint32_t bits){float x;memcpy(&x,&bits,4);return x;}
static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    memset(rows,0,LANES*ROW*sizeof *rows);
    for(unsigned lane=0;lane<LANES;lane++){
        uint32_t *r=rows+lane*ROW;r[WF_VERSION]=2;r[WF_TASK]=task;
        r[WF_OP]=WF_RESET;r[WF_SEED]=seed+lane;
    }
    f->batch(rows);for(unsigned lane=0;lane<LANES;lane++)assert(!f->validate(rows+lane*ROW));
}
static void action(const WFFamily *f,uint32_t *rows,unsigned kind,unsigned target,
                   unsigned arg0,const char *text,unsigned now){
    WFAction a={.kind=kind,.target=target,.arg0=arg0,.elapsed_ms=now,
                .text=text,.text_length=text?strlen(text):0};
    assert(!f->action(rows,&a));f->batch(rows);assert(!f->validate(rows));
}
static void date_success(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    reset(f,rows,task,seed);
    unsigned goal=rows[35],month=1,day=goal;
    static const unsigned days[]={31,29,31,30,31,30,31,31,30,31,30,31};
    while(day>days[month-1])day-=days[month++-1];
    action(f,rows,WF_CLICK,1,0,NULL,100);
    for(unsigned m=12;m>month;m--)action(f,rows,WF_CLICK,3,0,NULL,200+(12-m)*100);
    action(f,rows,WF_CLICK,day+4,0,NULL,1500);
    assert(rows[33]==goal&&rows[34]==0&&rows[WF_STATUS]==WF_RUNNING);
    action(f,rows,WF_CLICK,2,0,NULL,1600);
    assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==1.0f);
    assert(fabsf(real(rows[WF_TIMED_REWARD])-0.92f)<1e-4f);
}
static void daily_success(const WFFamily *f,uint32_t *rows,unsigned seed){
    static const char *const names[]={"Phonecall","Food","Party","Meeting","Gym"};
    reset(f,rows,4,seed);
    unsigned start=16+rows[36]*8,duration=rows[37];
    action(f,rows,WF_SCROLL,56,start,NULL,100);
    action(f,rows,WF_POINTER_DOWN,start+1,0,NULL,200);
    action(f,rows,WF_POINTER_MOVE,start+duration,0,NULL,300);
    action(f,rows,WF_POINTER_UP,52,0,NULL,400);
    assert(rows[41]==1);
    action(f,rows,WF_INSERT,53,0,names[rows[38]],500);
    action(f,rows,WF_CLICK,55,0,NULL,600);
    assert(rows[WF_STATUS]==WF_TERMINAL&&real(rows[WF_RAW_REWARD])==1.0f);
}
int main(void){
    prctl(PR_SET_DUMPABLE,0);
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==5&&
      f->row_words==ROW&&f->batch_lanes==LANES);
    uint32_t *rows=calloc(LANES*ROW,sizeof *rows);assert(rows);
    WFView a,b;
    for(unsigned task=0;task<5;task++)for(unsigned seed=1;seed<100;seed+=13){
        reset(f,rows,task,seed);assert(!f->observe(rows,&a));
        assert(a.instruction.length>0&&a.deadline_ms==20000);
        uint32_t copy[ROW];memcpy(copy,rows,ROW*sizeof *copy);
        if(task<4)copy[35]=copy[35]==366?365:copy[35]+1;
        else copy[38]=(copy[38]+1)%5;
        assert(!f->observe(copy,&b)&&!memcmp(&a,&b,sizeof a));
        for(unsigned lane=1;lane<LANES;lane++)assert(rows[lane*ROW+WF_SEED]==seed+lane);
    }
    for(unsigned task=0;task<4;task++)date_success(f,rows,task,121+task);
    reset(f,rows,1,77);action(f,rows,WF_CLICK,2,0,NULL,100);
    assert(real(rows[WF_RAW_REWARD])==-1.0f);
    uint32_t terminal_copy[ROW];memcpy(terminal_copy,rows,sizeof terminal_copy);
    WFAction after={.kind=WF_WAIT,.elapsed_ms=500};assert(f->action(rows,&after)<0);
    assert(!memcmp(terminal_copy,rows,sizeof terminal_copy));
    reset(f,rows,0,10);action(f,rows,WF_CLICK,1,0,NULL,100);
    assert(rows[ROW+32]==12&&rows[ROW+34]==0&&rows[ROW+WF_STATUS]==WF_RUNNING);
    for(unsigned i=0;i<11;i++)action(f,rows,WF_CLICK,3,0,NULL,200+i*100);
    assert(rows[32]==1);
    WFAction invalid={.kind=WF_CLICK,.target=3,.elapsed_ms=1500};assert(f->action(rows,&invalid)<0);
    reset(f,rows,2,10);action(f,rows,WF_WAIT,0,0,NULL,20000);
    assert(rows[WF_STATUS]==WF_TIMEOUT&&real(rows[WF_RAW_REWARD])==-1.0f);
    after.elapsed_ms=20000;
    assert(f->action(rows,&after)<0);
    for(unsigned seed=1;seed<30;seed+=5)daily_success(f,rows,seed);
    reset(f,rows,4,17);unsigned start=16+rows[36]*8;
    action(f,rows,WF_SCROLL,56,start,NULL,100);
    action(f,rows,WF_POINTER_DOWN,start+1,0,NULL,200);
    action(f,rows,WF_POINTER_MOVE,start+2,0,NULL,300);
    action(f,rows,WF_POINTER_UP,52,0,NULL,400);
    action(f,rows,WF_CLICK,54,0,NULL,500);assert(rows[41]==0&&rows[39]==48);
    unsigned wanted=16+rows[36]*8;
    action(f,rows,WF_SCROLL,56,wanted,NULL,600);
    action(f,rows,WF_POINTER_DOWN,wanted+1,0,NULL,700);
    action(f,rows,WF_POINTER_MOVE,wanted+rows[37],0,NULL,800);
    action(f,rows,WF_POINTER_UP,52,0,NULL,900);
    static const char *const retry_names[]={"Phonecall","Food","Party","Meeting","Gym"};
    action(f,rows,WF_INSERT,53,0,retry_names[rows[38]],1000);
    action(f,rows,WF_CLICK,55,0,NULL,1100);
    assert(real(rows[WF_RAW_REWARD])==1.0f);
    /* The original end-only window check accepts a drag begun before 8AM. */
    reset(f,rows,4,21);rows[36]=0;rows[37]=2;rows[38]=0;
    action(f,rows,WF_SCROLL,56,14,NULL,100);
    action(f,rows,WF_POINTER_DOWN,15,0,NULL,200);
    action(f,rows,WF_POINTER_MOVE,16,0,NULL,300);
    action(f,rows,WF_POINTER_UP,52,0,NULL,400);
    action(f,rows,WF_INSERT,53,0,"pHoNeCaLl",500);
    action(f,rows,WF_CLICK,55,0,NULL,600);
    assert(real(rows[WF_RAW_REWARD])==1.0f);
    /* Generated events have no data-start attribute in the source page,
       therefore overlap detection remains inert even on an occupied slot. */
    reset(f,rows,4,22);rows[36]=0;rows[37]=2;rows[38]=0;
    rows[48]=16;rows[51]=18;
    action(f,rows,WF_SCROLL,56,16,NULL,100);
    action(f,rows,WF_POINTER_DOWN,17,0,NULL,200);
    action(f,rows,WF_POINTER_MOVE,18,0,NULL,300);
    action(f,rows,WF_POINTER_UP,52,0,NULL,400);
    action(f,rows,WF_INSERT,53,0,"Phonecall",500);
    action(f,rows,WF_CLICK,55,0,NULL,600);
    assert(real(rows[WF_RAW_REWARD])==1.0f);
    reset(f,rows,4,23);rows[36]=0;rows[37]=2;rows[38]=0;
    action(f,rows,WF_SCROLL,56,16,NULL,100);
    action(f,rows,WF_POINTER_DOWN,17,0,NULL,200);
    action(f,rows,WF_POINTER_MOVE,18,0,NULL,300);
    action(f,rows,WF_POINTER_UP,52,0,NULL,400);
    action(f,rows,WF_INSERT,53,0,"Wrong",500);
    action(f,rows,WF_CLICK,55,0,NULL,600);
    assert(real(rows[WF_RAW_REWARD])==-1.0f);
    free(rows);puts("calendar source regression passed");return 0;
}
