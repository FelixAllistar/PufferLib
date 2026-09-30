#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 1024u
#define LANES 4u
#define QUERY 256u
void family_calendar_batch(uint32_t *words);

static const char *const tasks[]={"choose-date","choose-date-easy",
    "choose-date-medium","choose-date-nodelay","daily-calendar"};
static const char *const event_names[]={"Phonecall","Food","Party","Meeting","Gym"};
static const char *const month_names[]={"January","February","March","April","May","June",
    "July","August","September","October","November","December"};
static const unsigned month_days[]={31,29,31,30,31,30,31,31,30,31,30,31};
static unsigned first_month(unsigned task){return task==1?12:task==2?11:1;}

static int ascii(const uint32_t *p,unsigned cap){
    for(unsigned i=0;i<cap;i++){if(!p[i])return 1;if(p[i]<32||p[i]>126)return 0;}
    return 0;
}
static unsigned before(unsigned month){unsigned n=0;for(unsigned i=1;i<month;i++)n+=month_days[i-1];return n;}
static void date_text(unsigned ordinal,char *out,size_t cap){
    unsigned m=1;while(m<12&&ordinal>before(m)+month_days[m-1])m++;
    snprintf(out,cap,"%02u/%02u/2016",m,ordinal-before(m));
}
static void time_text(unsigned half,char *out,size_t cap){
    unsigned hour=(half/2)%24,display=hour%12;
    if(!display)display=12;
    snprintf(out,cap,half%2?"%u:30%s":"%u%s",display,hour<12?"am":"pm");
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=5||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=20000||r[WF_ELAPSED]>20000||
       !ascii(r+QUERY,192)||r[42]>32||r[57]>1||r[58]>32)return -1;
    if(r[WF_TASK]<4){
        if(r[32]<first_month(r[WF_TASK])||r[32]>12||r[33]>366||r[34]>1||r[35]<1||r[35]>366)return -1;
    }else if(r[36]>2||r[37]<1||r[37]>3||r[38]>4||r[39]>48||
             r[40]>48||r[41]>1||r[43]>40)return -1;
    return 0;
}
static int add_ascii(WFView *v,const uint32_t *p,unsigned cap,WFText *t){
    char s[256];unsigned n=0;while(n<cap&&p[n]){s[n]=(char)p[n];n++;}
    return wf_text_add(v,s,n,t);
}
static WFNode *node(WFView *v,unsigned ref,unsigned role,unsigned flags,
                    const char *name,const char *value,float x,float y,float w,float h){
    if(v->count==WF_MAX_NODES){v->omitted++;return NULL;}
    WFNode *n=&v->nodes[v->count++];n->ref=ref;n->role=role;n->flags=flags;
    n->x=x;n->y=y;n->width=w;n->height=h;
    if(wf_text_add(v,name,strlen(name),&n->name)||
       wf_text_add(v,value,strlen(value),&n->value))return NULL;
    return n;
}
static int date_view(const uint32_t *r,WFView *v){
    char value[32]="",label[64];
    if(r[33])date_text(r[33],value,sizeof value);
    if(!node(v,1,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|WF_READONLY,
             "Date",value,0,0,100,20)||
       !node(v,2,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
             "Submit","",125,30,70,24))return -1;
    if(!r[34])return 0;
    unsigned m=r[32],prev=m>first_month(r[WF_TASK]),next=m<12;
    if(!node(v,3,WF_BUTTON,WF_VISIBLE|(prev?WF_ENABLED|WF_CLICKABLE:0),
             "Previous month","",0,54,20,20)||
       !node(v,4,WF_BUTTON,WF_VISIBLE|(next?WF_ENABLED|WF_CLICKABLE:0),
             "Next month","",180,54,20,20))return -1;
    snprintf(label,sizeof label,"%s 2016",month_names[m-1]);
    if(!node(v,36,WF_TEXT,WF_VISIBLE,label,"",25,54,150,20))return -1;
    for(unsigned d=1;d<=month_days[m-1];d++){
        snprintf(label,sizeof label,"%u",d);
        unsigned flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        if(r[33]==before(m)+d)flags|=WF_SELECTED;
        if(!node(v,d+4,WF_BUTTON,flags,label,"",(float)((d-1)%7)*25,
                 76+(float)((d-1)/7)*19,24,18))return -1;
    }
    return 0;
}
static int daily_view(const uint32_t *r,WFView *v){
    char name[64],value[64];
    WFNode *area=node(v,56,WF_PANEL,WF_VISIBLE|WF_ENABLED,"Daily calendar","",0,0,140,156);
    if(!area)return -1;area->scroll_y=(float)r[43]*20;area->scroll_max_y=828;
    for(unsigned h=0;h<48;h++){
        if(h<r[43]||h>r[43]+8)continue;
        time_text(h,name,sizeof name);
        WFNode *cell=node(v,h+1,WF_CELL,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                          name,"",30,4+(float)(h-r[43])*20,108,20);
        if(!cell)return -1;cell->parent=56;
    }
    for(unsigned i=0;i<3;i++){
        unsigned start=r[48+i],end=r[51+i];
        if(start>48||end>49||start>=end)return -1;
        if(end<=r[43]||start>r[43]+8)continue;
        snprintf(name,sizeof name,"Existing event %u",i+1);
        WFNode *event=node(v,49+i,WF_TEXT,WF_VISIBLE,name,event_names[r[54+i]%5],
                           30,4+(float)((int)start-(int)r[43])*20,100,(float)(end-start)*20);
        if(!event)return -1;event->parent=56;
    }
    if(r[39]<48&&r[40]<48){
        unsigned a=r[39],b=r[40]+1;
        if(a!=b){
            unsigned top=a<b?a:b,height=a<b?b-a:a-b;
            WFNode *draft=node(v,52,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                               "New event","",30,4+(float)((int)top-(int)r[43])*20,
                               100,(float)height*20);
            if(!draft)return -1;draft->parent=56;
        }
    }
    if(r[41]){
        unsigned len=r[42];for(unsigned i=0;i<len;i++)value[i]=(char)r[64+i];value[len]=0;
        WFNode *input=node(v,53,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                           "Event name",value,5,20,120,20);
        if(!input||!node(v,54,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                         "Cancel","",5,70,55,20)||
           !node(v,55,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                         "Create","",65,70,55,20))return -1;
        input->parent=56;input->selection_start=r[57]?0:len;
        input->selection_end=len;input->capacity=32;
    }
    return 0;
}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET||!v)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(add_ascii(v,r+QUERY,192,&v->instruction))return -1;
    return r[WF_TASK]==4?daily_view(r,v):date_view(r,v);
}
static int date_action(const uint32_t *r,const WFAction *a){
    if(a->kind==WF_WAIT)return 0;
    if(a->kind!=WF_CLICK)return -1;
    unsigned t=a->target,m=r[32];
    if(t==1||t==2)return 0;
    if(!r[34])return -1;
    if(t==3)return m>first_month(r[WF_TASK])?0:-1;
    if(t==4)return m<12?0:-1;
    return t>=5&&t<=month_days[m-1]+4?0:-1;
}
static int daily_action(const uint32_t *r,const WFAction *a){
    unsigned t=a->target,k=a->kind;
    if(k==WF_WAIT)return 0;
    if(k==WF_SCROLL)return t==56&&a->arg0<=40?0:-1;
    if(k==WF_POINTER_DOWN||k==WF_POINTER_MOVE)
        return t>=1&&t<=48&&t>=r[43]+1&&t<=r[43]+9?0:-1;
    if(k==WF_POINTER_UP)return t==52&&r[39]<48&&r[40]<48?0:-1;
    if(k==WF_CLICK)return (t==54||t==55)&&r[41]?0:-1;
    if(k==WF_INSERT)return t==53&&r[41]&&a->text&&a->text_length<=32?0:-1;
    if(k==WF_BACKSPACE||k==WF_SELECT_ALL)return t==53&&r[41]?0:-1;
    return -1;
}
static int action(uint32_t *r,const WFAction *a){
    if(valid(r)||!a||r[WF_STATUS]!=WF_RUNNING||
       a->elapsed_ms<r[WF_ELAPSED]||a->elapsed_ms>20000)return -1;
    if(a->kind!=WF_INSERT&&a->text_length)return -1;
    if((r[WF_TASK]==4?daily_action(r,a):date_action(r,a)))return -1;
    if(a->kind==WF_INSERT){
        for(size_t i=0;i<a->text_length;i++){
            unsigned char ch=(unsigned char)a->text[i];if(ch<32||ch>126)return -1;
        }
        for(unsigned i=0;i<32;i++)r[128+i]=i<a->text_length?(unsigned char)a->text[i]:0;
        r[58]=(uint32_t)a->text_length;
    }
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->arg1;r[WF_ELAPSED]=a->elapsed_ms;
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,LANES,5,"calendar",tasks,
                            valid,family_calendar_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
