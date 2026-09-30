#include "../common/family_api.h"
#include "public_controller.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#define ROW 1024u
#define LANES 4u
static float real(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static void batch(const WFFamily *f,uint32_t *rows){
    f->batch(rows);signal(SIGABRT,SIG_DFL);
    for(unsigned lane=0;lane<LANES;lane++)assert(!f->validate(rows+lane*ROW));
}
static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    /* Change tasks on all four dirty lanes. Reset must clear the entire row. */
    memset(rows,0xa5,LANES*ROW*sizeof *rows);
    for(unsigned lane=0;lane<LANES;lane++){
        uint32_t *r=rows+lane*ROW;
        r[WF_VERSION]=WF_ABI_VERSION;r[WF_TASK]=task;r[WF_OP]=WF_RESET;
        r[WF_SEED]=seed+lane;
    }
    batch(f,rows);
    for(unsigned lane=0;lane<LANES;lane++){
        const uint32_t *r=rows+lane*ROW;
        assert(r[WF_STATUS]==WF_RUNNING&&r[WF_ELAPSED]==0);
        assert(r[WF_RAW_REWARD]==0&&r[WF_TIMED_REWARD]==0);
        assert(r[WF_ACTION]==0&&r[WF_TARGET]==0&&r[WF_ARG0]==0&&r[WF_ARG1]==0);
    }
}
static void action(const WFFamily *f,uint32_t *rows,unsigned kind,
                   unsigned target,unsigned x,unsigned y,unsigned now){
    WFAction a={.kind=kind,.target=target,.arg0=x,.arg1=y,.elapsed_ms=now};
    assert(!f->action(rows,&a));batch(f,rows);
}
static unsigned submit_ref(unsigned task){
    return task==0?3:task==2||task==5||task==6?8:task==8?5:2;
}
static unsigned down_ref(unsigned task){return task==2?7:1;}
static void view_check(const WFFamily *f,uint32_t *r,unsigned task){
    WFView v;assert(!f->observe(r,&v));
    const char *q=wf_text_get(&v,v.instruction);
    assert(q&&q[0]&&v.version==WF_ABI_VERSION);
    assert(v.deadline_ms==r[WF_DEADLINE]);
    assert(v.count==(task==0?3:task==2?8:task==3?5:task==4?9:
                     task==5?6:task==6?8:task==8?5:2));
    for(unsigned i=0;i<v.count;i++){
        assert(wf_text_get(&v,v.nodes[i].name));
        assert(wf_text_get(&v,v.nodes[i].value));
        assert(v.nodes[i].flags&WF_VISIBLE);
    }
    if(task==0)assert(!strcmp(q,
        "Drag the smaller box so that it is completely inside the larger box."));
    if(task==8)assert(!strcmp(q,
        "Sort the numbers in increasing order, starting with the lowest number at the top of the list."));
}
static void private_view_check(const WFFamily *f,const uint32_t *r,unsigned task){
    uint32_t copy[ROW];memcpy(copy,r,sizeof copy);
    WFView before,after;assert(!f->observe(copy,&before));
    /* The rendered query is held fixed while only private answer fields vary. */
    switch(task){
      case 1:case 7:copy[36]=(copy[36]+1)%4;break;
      case 2:copy[32]=copy[32]%6+1;break;
      case 3:case 4:copy[35]=copy[35]%copy[32]+1;copy[36]=copy[36]%copy[32]+1;break;
      case 5:case 6:copy[35]=(copy[35]+1)%(copy[34]?7:3);break;
      case 9:copy[38]^=1;copy[39]^=1;break;
      default:return;
    }
    assert(!f->validate(copy)&&!f->observe(copy,&after));
    assert(before.count==after.count&&before.text_bytes==after.text_bytes);
    assert(!memcmp(before.text,after.text,before.text_bytes));
    for(unsigned i=0;i<before.count;i++){
        const WFNode *a=before.nodes+i,*b=after.nodes+i;
        assert(a->ref==b->ref&&a->role==b->role&&a->flags==b->flags);
        assert(a->x==b->x&&a->y==b->y&&a->width==b->width&&a->height==b->height);
    }
}
static unsigned fingerprint(const uint32_t *r,unsigned task){
    unsigned h=2166136261u;
    unsigned n=task==3||task==4?37+r[32]:task==5||task==6?84:
               task==8?68:task==9?41:task==2?35:41;
    for(unsigned i=32;i<n;i++)h=(h^r[i])*16777619u;
    return h;
}
static void generated(const WFFamily *f,uint32_t *rows){
    for(unsigned task=0;task<10;task++){
        unsigned first=0,variation=0;
        for(unsigned seed=1;seed<=8;seed++){
            reset(f,rows,task,seed*173+task*37);
            uint32_t *r=rows;
            view_check(f,r,task);private_view_check(f,r,task);
            if(seed==1)first=fingerprint(r,task);
            else variation|=fingerprint(r,task)!=first;
            if(task==0){
                assert(r[32]>=256&&r[32]<=384&&r[33]>=256&&r[33]<=330);
                assert(r[34]>=256&&r[34]<=354&&r[35]>=256&&r[35]<=300);
            }
            if(task==2)assert(r[33]==6&&r[32]>=1&&r[32]<=6);
            if(task==3||task==4){
                assert(r[35]>=1&&r[35]<=r[32]&&r[36]>=1&&r[36]<=r[32]);
                assert(r[35]!=r[36]);
            }
            if(task==5||task==6)assert(r[37]==(task==5?4u:5u));
            if(task==9)assert(r[32]==r[34]&&r[33]==r[35]);
        }
        assert(variation);
    }
}
static void box_edges(const WFFamily *f,uint32_t *rows){
    for(unsigned edge=0;edge<3;edge++){
        reset(f,rows,0,100+edge);
        uint32_t *r=rows;unsigned old_x=r[32],old_y=r[33];
        unsigned x=r[34]+(edge==0?0:edge==1?1:30),y=r[35]+1;
        action(f,rows,WF_POINTER_MOVE,0,x,y,100);
        assert(r[32]==old_x&&r[33]==old_y); /* movement requires a held node */
        action(f,rows,WF_POINTER_DOWN,1,0,0,200);
        action(f,rows,WF_POINTER_MOVE,0,x,y,300);
        action(f,rows,WF_POINTER_UP,0,0,0,400);
        assert(r[32]==x&&r[33]==y&&r[36]==0);
        action(f,rows,WF_CLICK,3,0,0,500);
        assert(r[WF_STATUS]==WF_TERMINAL);
        assert(real(r[WF_RAW_REWARD])==(edge==1?1.0f:-1.0f));
        if(edge==1)assert(fabsf(real(r[WF_TIMED_REWARD])-0.95f)<1e-5f);
    }
}
static void direction_and_resize(const WFFamily *f,uint32_t *rows,unsigned task){
    reset(f,rows,task,543+task);uint32_t *r=rows;
    unsigned submit=submit_ref(task);
    action(f,rows,WF_CLICK,submit,0,0,100);
    assert(r[WF_STATUS]==WF_TERMINAL&&real(r[WF_RAW_REWARD])==-1.0f);
    reset(f,rows,task,543+task);r=rows;
    unsigned x=task==9?r[32]:r[34],y=task==9?r[33]:r[35];
    if(task==9){if(r[38]==0)x+=r[39]?10:-10;else y+=r[39]?10:-10;}
    else{if(r[36]==0)x-=100;else if(r[36]==1)x+=100;
         else if(r[36]==2)y-=100;else y+=100;}
    unsigned old_x=task==9?r[32]:r[34],old_y=task==9?r[33]:r[35];
    action(f,rows,WF_POINTER_MOVE,0,x,y,100);
    assert((task==9?r[32]:r[34])==old_x&&(task==9?r[33]:r[35])==old_y);
    action(f,rows,WF_POINTER_DOWN,down_ref(task),0,0,200);
    action(f,rows,WF_POINTER_MOVE,0,x,y,300);
    action(f,rows,WF_POINTER_UP,0,0,0,400);
    action(f,rows,WF_CLICK,submit,0,0,500);
    assert(r[WF_STATUS]==WF_TERMINAL&&real(r[WF_RAW_REWARD])==1.0f);
}
static void cube(const WFFamily *f,uint32_t *rows){
    reset(f,rows,2,219);uint32_t *r=rows;
    unsigned wrong=r[32]==6?1:6;
    action(f,rows,WF_POINTER_MOVE,0,wrong,0,100);assert(r[33]==6);
    action(f,rows,WF_POINTER_DOWN,7,0,0,200);
    action(f,rows,WF_POINTER_MOVE,0,wrong,0,300);
    action(f,rows,WF_POINTER_UP,0,0,0,400);
    action(f,rows,WF_CLICK,8,0,0,500);
    assert(r[WF_STATUS]==WF_TERMINAL&&real(r[WF_RAW_REWARD])==-1.0f);
}
static unsigned position(const uint32_t *r,unsigned item){
    for(unsigned i=0;i<r[32];i++)if(r[37+i]==item)return i+1;
    return 0;
}
static void sortable(const WFFamily *f,uint32_t *rows,unsigned task){
    reset(f,rows,task,829+task);uint32_t *r=rows;
    if(task==3||task==4){
        action(f,rows,WF_POINTER_DOWN,1,0,0,100);
        action(f,rows,WF_POINTER_UP,0,0,0,200);
        assert(r[WF_STATUS]==WF_RUNNING&&r[33]==0&&r[34]==0);
        reset(f,rows,task,829+task);
    }
    if(task==8){
        action(f,rows,WF_POINTER_DOWN,1,0,0,100);
        action(f,rows,WF_POINTER_MOVE,0,4,0,200);
        action(f,rows,WF_POINTER_UP,0,0,0,300);
        assert(r[WF_STATUS]==WF_RUNNING&&position(r,1)==4);
        return;
    }
    unsigned selected=r[35],wrong=selected==1?2:1,goal=r[36];
    action(f,rows,WF_POINTER_DOWN,wrong,0,0,100);
    action(f,rows,WF_POINTER_MOVE,0,goal,0,200);
    action(f,rows,WF_POINTER_UP,0,0,0,300);
    assert(r[WF_STATUS]==WF_TERMINAL&&real(r[WF_RAW_REWARD])==-1.0f);
}
static int inside(unsigned x,unsigned y,unsigned bx,unsigned by,unsigned w,unsigned h){
    return x>2560+bx*10&&x<2560+(bx+w)*10&&
           y>2560+by*10&&y<2560+(by+h)*10;
}
static void shapes(const WFFamily *f,uint32_t *rows,unsigned task){
    reset(f,rows,task,911+task);uint32_t *r=rows;
    unsigned correct=0;
    for(unsigned i=0;i<r[37];i++){
        const uint32_t *s=r+64+i*4;
        int target=(task==5||r[34]==0)?s[0]==r[35]:s[1]==r[35];
        int ok=task==5?inside(s[2],s[3],r[32],r[33],80,45)==target:
            target?inside(s[2],s[3],2,5,70,50):inside(s[2],s[3],84,5,70,50);
        correct+=(unsigned)ok;
    }
    action(f,rows,WF_CLICK,8,0,0,100);
    assert(r[WF_STATUS]==WF_TERMINAL);
    float expected=task==5?(correct==4?1.0f:-1.0f):
        fminf(1.0f,fmaxf(-1.0f,correct*0.26f-(5-correct)*0.20f));
    assert(fabsf(real(r[WF_RAW_REWARD])-expected)<2e-5f);
    if(expected>0)assert(fabsf(real(r[WF_TIMED_REWARD])-
        expected*(1.0f-100.0f/(float)r[WF_DEADLINE]))<2e-5f);
    else assert(fabsf(real(r[WF_TIMED_REWARD])-expected)<2e-5f);
}
static unsigned snapped(unsigned coordinate,unsigned offset){
    return ((coordinate+offset+5)/10)*10-offset;
}
static void svg_quantization(const WFFamily *f,uint32_t *rows){
    reset(f,rows,1,1013);uint32_t *r=rows;
    unsigned offset=r[39]*12u,x=r[34]+100,y=r[35]+100;
    action(f,rows,WF_POINTER_DOWN,1,0,0,100);
    WFAction invalid={.kind=WF_POINTER_MOVE,.arg0=5119,.arg1=5119,
                      .elapsed_ms=150};
    if(((5119u+offset+5u)/10u)*10u-offset>5119u)
        assert(f->action(r,&invalid)<0);
    action(f,rows,WF_POINTER_MOVE,0,x,y,200);
    assert(r[34]==snapped(x,offset)&&r[35]==snapped(y,offset));
    assert(r[34]>=x-5&&r[34]<=x+5&&r[35]>=y-5&&r[35]<=y+5);

    reset(f,rows,5,1017);r=rows;
    unsigned kind=r[64],shape_offset=kind==0?102u:kind==1?85u:68u;
    x=2560+1000;y=2560+700;
    action(f,rows,WF_POINTER_DOWN,1,0,0,100);
    action(f,rows,WF_POINTER_MOVE,0,x,y,200);
    assert(r[66]==snapped(x,shape_offset)&&r[67]==snapped(y,shape_offset));
    assert(r[66]>=x-5&&r[66]<=x+5&&r[67]>=y-5&&r[67]<=y+5);
}
static void public_solve(const WFFamily *f,uint32_t *rows){
    for(unsigned task=0;task<10;task++)for(unsigned seed=1;seed<=5;seed++){
        reset(f,rows,task,seed*107+task*101);
        DragPublic ctl={.task=task};uint32_t *r=rows;
        for(unsigned step=0;step<45&&r[WF_STATUS]==WF_RUNNING;step++){
            WFView v;assert(!f->observe(r,&v));
            WFAction a={0};unsigned now=(step+1)*100;
            assert(!drag_public_next(&ctl,&v,now,&a));
            assert(!f->action(r,&a));batch(f,rows);
        }
        if(r[WF_STATUS]!=WF_TERMINAL||real(r[WF_RAW_REWARD])!=1.0f){
            fprintf(stderr,"public solve task=%u seed=%u status=%u raw=%g\n",
                    task,seed,r[WF_STATUS],real(r[WF_RAW_REWARD]));abort();
        }
    }
}
static void validation_and_timeout(const WFFamily *f,uint32_t *rows){
    for(unsigned task=0;task<10;task++){
        reset(f,rows,task,73+task);uint32_t *r=rows;
        WFAction bad={.kind=WF_POINTER_DOWN,.target=127,.elapsed_ms=100};
        assert(f->action(r,&bad)<0);
        bad=(WFAction){.kind=WF_WAIT,.elapsed_ms=r[WF_DEADLINE]+1};
        assert(f->action(r,&bad)<0);
        bad=(WFAction){.kind=WF_WAIT,.elapsed_ms=100,.text="x",.text_length=1};
        assert(f->action(r,&bad)<0);
        action(f,rows,WF_WAIT,0,0,0,r[WF_DEADLINE]);
        assert(r[WF_STATUS]==WF_TIMEOUT&&real(r[WF_RAW_REWARD])==-1.0f&&
               real(r[WF_TIMED_REWARD])==-1.0f);
        bad=(WFAction){.kind=WF_CLICK,.target=submit_ref(task),.elapsed_ms=r[WF_DEADLINE]};
        assert(f->action(r,&bad)<0);
    }
}
int main(void){
    prctl(PR_SET_DUMPABLE,0,0,0,0);
    const WFFamily *f=webnav_family_v2();
    assert(f&&f->task_count==10&&f->row_words==ROW&&f->batch_lanes==LANES);
    uint32_t *rows=calloc(LANES*ROW,sizeof *rows);assert(rows);
    generated(f,rows);box_edges(f,rows);
    direction_and_resize(f,rows,1);direction_and_resize(f,rows,7);
    direction_and_resize(f,rows,9);cube(f,rows);
    sortable(f,rows,3);sortable(f,rows,4);sortable(f,rows,8);
    shapes(f,rows,5);shapes(f,rows,6);
    svg_quantization(f,rows);
    validation_and_timeout(f,rows);public_solve(f,rows);
    free(rows);puts("drag ten-task native coverage passed");return 0;
}
