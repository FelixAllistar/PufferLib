#include "../common/family_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW 8192u
#define FIELD_BASE 128u
#define FIELD_STRIDE 544u

static uint32_t *lane(uint32_t *rows,unsigned i){return rows+(size_t)i*ROW;}
static const uint32_t *field(const uint32_t *r,unsigned i){return r+FIELD_BASE+i*FIELD_STRIDE;}
static unsigned fields(unsigned task){return task==0||task==1||task==3?1:task==5?4:2;}
static unsigned deadline(unsigned task){return task==6?20000u:(task==2||task==3||task==7?15000u:10000u);}
static int units_equal_ascii(const uint32_t *units,const char *text,size_t n){
    for(size_t i=0;i<n;i++)if(units[i]!=(unsigned char)text[i])return 0;
    return 1;
}
static void run_batch(const WFFamily *f,uint32_t *rows,unsigned active){
    for(unsigned i=0;i<4;i++)if(i!=active)lane(rows,i)[2]=WF_OBSERVE;
    f->batch(rows);
    for(unsigned i=0;i<4;i++)assert(!f->validate(lane(rows,i)));
}
static void send(const WFFamily *f,uint32_t *rows,unsigned target,unsigned kind,
    const char *text,unsigned arg0,unsigned arg1,unsigned now){
    uint32_t *r=lane(rows,0);
    WFAction a={.kind=kind,.target=target,.arg0=arg0,.arg1=arg1,.elapsed_ms=now,
        .text=text,.text_length=text?strlen(text):0};
    assert(!f->action(r,&a));run_batch(f,rows,0);
}
static void reset(const WFFamily *f,uint32_t *rows,unsigned task,unsigned seed){
    for(unsigned i=0;i<4;i++){
        uint32_t *r=lane(rows,i);memset(r,0,ROW*sizeof *r);
        r[0]=WF_ABI_VERSION;r[1]=task;r[2]=WF_RESET;r[3]=seed+i;
    }
    f->batch(rows);
    for(unsigned i=0;i<4;i++)assert(!f->validate(lane(rows,i)));
}
static unsigned submit_ref(unsigned task){return task==3?3u:task==6?15u:fields(task)+1u;}
static void private_goal(const uint32_t *r,unsigned i,char *out){
    const uint32_t *f=field(r,i);unsigned n=f[4];assert(n<256u);
    for(unsigned j=0;j<n;j++){assert(f[264+j]>=32&&f[264+j]<=126);out[j]=(char)f[264+j];}
    out[n]=0;
}
static void fill(const WFFamily *f,uint32_t *rows,unsigned ref,unsigned i,
    unsigned *now){
    char text[256];private_goal(lane(rows,0),i,text);
    send(f,rows,ref,WF_CLICK,NULL,0,0,++*now);
    send(f,rows,0,WF_SELECT_ALL,NULL,0,0,++*now);
    send(f,rows,0,WF_INSERT,text,0,0,++*now);
}
static void assert_view_goal_blind(const WFFamily *f,uint32_t *r){
    WFView a,b;assert(!f->observe(r,&a));unsigned selected=0;
    while(selected<r[32]&&!field(r,selected)[4])selected++;
    assert(selected<r[32]);uint32_t *goal=(uint32_t *)field(r,selected)+264;uint32_t old=goal[0];
    goal[0]=old=='x'?'y':'x';assert(!f->observe(r,&b));assert(!memcmp(&a,&b,sizeof a));goal[0]=old;
}

int main(void){
    const WFFamily *f=webnav_family_v2();assert(f&&f->task_count==8&&f->row_words==ROW&&f->batch_lanes==4);
    static const char *names[]={"enter-text-dynamic","enter-text-2","enter-password","text-transform",
        "copy-paste","copy-paste-2","read-table-2","login-user-popup"};
    uint32_t *rows=calloc(ROW*4u,sizeof *rows);assert(rows);
    unsigned cases=0;
    for(unsigned task=0;task<8;task++){
        assert(!strcmp(f->task_names[task],names[task]));reset(f,rows,task,0x51u+task);
        uint32_t *r=lane(rows,0);assert(r[12]==deadline(task));assert(!f->validate(r));
        WFView v;assert(!f->observe(r,&v));assert(v.version==WF_ABI_VERSION&&v.deadline_ms==deadline(task));
        assert(v.count>=fields(task)+1u);assert_view_goal_blind(f,r);
        if(task==4||task==5){
            unsigned source=0,answer=task==4?1u:3u;
            char goal[256];private_goal(r,answer,goal);
            for(unsigned i=0;i<answer;i++)if(field(r,i)[1]==strlen(goal)&&units_equal_ascii(field(r,i)+8,goal,strlen(goal)))source=i;
            unsigned now=100;send(f,rows,source+1u,WF_CLICK,NULL,0,0,++now);
            send(f,rows,0,WF_SELECT_ALL,NULL,0,0,++now);send(f,rows,0,WF_COPY,NULL,0,0,++now);
            send(f,rows,answer+1u,WF_CLICK,NULL,0,0,++now);send(f,rows,0,WF_PASTE,NULL,0,0,++now);
            assert(field(r,answer)[1]==strlen(goal)&&units_equal_ascii(field(r,answer)+8,goal,strlen(goal)));
            send(f,rows,submit_ref(task),WF_CLICK,NULL,0,0,++now);
        }else{
            unsigned now=100;
            if(task==7&&r[35]){
                unsigned popup_field=r[35]-1u;send(f,rows,popup_field+1u,WF_CLICK,NULL,0,0,++now);
                assert(r[34]==1u);assert(!f->observe(r,&v));
                for(unsigned n=0;n<v.count;n++)if(v.nodes[n].role==WF_INPUT&&n<2)assert(!(v.nodes[n].flags&WF_ENABLED));
                send(f,rows,fields(task)+5u,WF_CLICK,NULL,0,0,++now);assert(r[34]==0u);
            }
            for(unsigned i=0;i<fields(task);i++)fill(f,rows,i+1u,i,&now);
            send(f,rows,submit_ref(task),WF_CLICK,NULL,0,0,++now);
        }
        assert(r[9]==WF_TERMINAL&&r[10]==1065353216u);assert(!f->validate(r));
        unsigned terminal_time=r[8];send(f,rows,submit_ref(task),WF_CLICK,NULL,0,0,terminal_time+1u);
        assert(r[9]==WF_TERMINAL&&r[10]==1065353216u);cases++;

        reset(f,rows,task,0x100u+task);r=lane(rows,0);
        send(f,rows,0,WF_WAIT,NULL,0,0,deadline(task));
        assert(r[9]==WF_TIMEOUT&&r[10]==3212836864u);cases++;
    }

    reset(f,rows,7,0x777u);uint32_t *r=lane(rows,0);
    if(!r[35]){
        for(unsigned seed=1;seed<100&&!r[35];seed++){reset(f,rows,7,seed);r=lane(rows,0);}
    }
    assert(r[35]);send(f,rows,r[35],WF_CLICK,NULL,0,0,100);
    assert(r[34]);send(f,rows,fields(7)+4u,WF_CLICK,NULL,0,0,200);
    assert(r[9]==WF_TERMINAL&&r[10]==3212836864u);cases++;
    free(rows);printf("PASS: %u forms transitions; 8 tasks, exact input, case transform, clipboard, table fields, popup cancel/OK, deadlines, terminal absorption and goal-blind views\n",cases);
    return 0;
}
