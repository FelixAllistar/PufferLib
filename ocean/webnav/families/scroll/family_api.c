#include "../common/family_api.h"
#include <string.h>

#define ROW 8192u
#define LANES 4u
#define QUERY 256u
#define OPTIONS 512u
#define CONTENT 1536u
#define INPUT 4608u
#define TARGET_TEXT 4736u
#define INCOMING 4864u
void family_scroll_batch(uint32_t *words);

static const char *const tasks[]={
    "click-scroll-list","scroll-text","scroll-text-2","sign-agreement"};

static int ascii(const uint32_t *p,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!p[i])return 1;
        if(p[i]<32||p[i]>126)return 0;
    }
    return 0;
}

static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=4||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    unsigned task=r[WF_TASK],deadline=task==0?15000u:10000u;
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=deadline||
       r[WF_ELAPSED]>deadline||r[33]<r[34]||r[33]>6000||
       r[34]<1||r[34]>500||r[32]>r[33]-r[34]||
       !ascii(r+QUERY,256))return -1;
    if(task==0){
        if(r[36]<8||r[36]>11||r[39]<1||r[39]>2||r[48]<12||r[48]>32||r[49]>r[36]||
           r[37]>=(1u<<r[36])||r[38]>=(1u<<r[36]))return -1;
        for(unsigned i=0;i<r[36];i++)if(!ascii(r+OPTIONS+i*64,64))return -1;
    }else{
        if(!ascii(r+CONTENT,3072))return -1;
        if(task==2){if(r[35]>1||r[47]<r[34]||r[47]>r[34]+10)return -1;}
        else if(r[42]>63||r[43]>63||r[44]>r[45]||
                r[45]>r[42]||r[46]>63||
                !ascii(r+INPUT,64)||!ascii(r+TARGET_TEXT,64))return -1;
        if(task==3&&(r[40]>2||r[41]>1))return -1;
    }
    return 0;
}

static int add_units(WFView *v,const uint32_t *p,unsigned cap,WFText *out){
    char s[3073];unsigned n=0;
    while(n<cap&&p[n]){s[n]=(char)p[n];n++;}
    return wf_text_add(v,s,n,out);
}

static WFNode *node(WFView *v,unsigned ref,unsigned role,unsigned flags,
                    float x,float y,float width,float height){
    if(v->count==WF_MAX_NODES){v->omitted++;return NULL;}
    WFNode *n=v->nodes+v->count++;
    n->ref=ref;n->role=role;n->flags=flags;
    n->x=x;n->y=y;n->width=width;n->height=height;
    return n;
}

static int label(WFView *v,WFNode *n,const char *name){
    return wf_text_add(v,name,strlen(name),&n->name);
}

static int empty(WFView *v,WFNode *n){return wf_text_add(v,"",0,&n->value);}

static int scroll_area(const uint32_t *r,WFView *v,unsigned role,
                       unsigned flags,unsigned height){
    WFNode *area=node(v,1,role,flags,0,5,150,(float)height);
    if(!area||label(v,area,role==WF_SELECT?"Scroll list":"Text area"))return -1;
    area->scroll_y=(float)r[32];area->scroll_max_y=(float)(r[33]-r[34]);
    if(role==WF_SELECT)return empty(v,area);
    return add_units(v,r+CONTENT,3072,&area->value);
}

static int button(WFView *v,unsigned ref,unsigned flags,const char *name,
                  float x,float y){
    WFNode *n=node(v,ref,WF_BUTTON,flags,x,y,70,24);
    return !n||label(v,n,name)||empty(v,n)?-1:0;
}

static int input(const uint32_t *r,WFView *v,unsigned flags,
                 const char *name,float y){
    WFNode *n=node(v,2,WF_INPUT,flags,0,y,150,20);
    if(!n||label(v,n,name)||add_units(v,r+INPUT,64,&n->value))return -1;
    n->selection_start=r[44];n->selection_end=r[45];n->capacity=63;
    return 0;
}

static int list_view(const uint32_t *r,WFView *v){
    if(scroll_area(r,v,WF_SELECT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,r[47]))return -1;
    for(unsigned i=0;i<r[36];i++){
        unsigned top=i*r[48],visible=top+r[48]>r[32]&&top<r[32]+r[34];
        unsigned flags=WF_ENABLED|WF_CLICKABLE|(visible?WF_VISIBLE:0)|
                       ((r[37]&(1u<<i))?WF_SELECTED:0);
        WFNode *n=node(v,i+2,WF_OPTION,flags,0,(float)((int)top-(int)r[32]+5),150,(float)r[48]);
        if(!n||add_units(v,r+OPTIONS+i*64,64,&n->name)||empty(v,n))return -1;
        n->parent=1;
    }
    return button(v,16,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,"Submit",0,105);
}

static int text_view(const uint32_t *r,WFView *v){
    if(scroll_area(r,v,WF_TEXTAREA,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,r[34])||
       input(r,v,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,"Answer",105))return -1;
    return button(v,3,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,"Submit",0,130);
}

static int direction_view(const uint32_t *r,WFView *v){
    if(scroll_area(r,v,WF_TEXTAREA,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,r[47]))return -1;
    return button(v,2,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,"Submit",0,110);
}

static int agreement_view(const uint32_t *r,WFView *v){
    unsigned enabled=r[41]?WF_ENABLED|WF_CLICKABLE:0;
    if(scroll_area(r,v,WF_TEXTAREA,WF_VISIBLE|WF_READONLY,r[34])||
       input(r,v,WF_VISIBLE|enabled,"Name",80)||
       button(v,3,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,"Cancel",0,105))return -1;
    return button(v,4,WF_VISIBLE|enabled,"Agree",75,105);
}

static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET||!v)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(add_units(v,r+QUERY,256,&v->instruction))return -1;
    switch(r[WF_TASK]){
        case 0:return list_view(r,v);
        case 1:return text_view(r,v);
        case 2:return direction_view(r,v);
        default:return agreement_view(r,v);
    }
}

static int eligible(const uint32_t *r,const WFAction *a){
    unsigned task=r[WF_TASK],kind=a->kind,target=a->target;
    if(kind==WF_WAIT)return 0;
    if(kind==WF_SCROLL)return target==1&&a->arg0<=r[33]?0:-1;
    if(task==0){
        if(kind==WF_CLICK)return target==16?0:-1;
        if(kind!=WF_SELECT_OPTION||target<2||target>r[36]+1||a->arg0>1)return -1;
        unsigned top=(target-2)*r[48];
        return top+r[48]>r[32]&&top<r[32]+r[34]?0:-1;
    }
    if(kind==WF_CLICK){
        if(task==1)return target==2||target==3?0:-1;
        if(task==2)return target==2?0:-1;
        return target==3||(target==4&&r[41])||(target==2&&r[41])?0:-1;
    }
    if(task==2||target!=2||(task==3&&!r[41]))return -1;
    if(kind==WF_INSERT)return a->text&&a->text_length<=63&&
        r[42]-(r[45]-r[44])+a->text_length<=63?0:-1;
    return kind>=WF_BACKSPACE&&kind<=WF_SELECT_ALL?0:-1;
}

static int action(uint32_t *r,const WFAction *a){
    if(valid(r)||!a||r[WF_STATUS]!=WF_RUNNING||
       a->elapsed_ms<r[WF_ELAPSED]||a->elapsed_ms>r[WF_DEADLINE]||
       (a->kind!=WF_INSERT&&a->text_length)||eligible(r,a))return -1;
    if(a->kind==WF_INSERT){
        for(size_t i=0;i<a->text_length;i++){
            unsigned char ch=(unsigned char)a->text[i];
            if(ch<32||ch>126)return -1;
        }
        for(unsigned i=0;i<64;i++)r[INCOMING+i]=i<a->text_length?(unsigned char)a->text[i]:0;
        r[46]=(uint32_t)a->text_length;
    }else r[46]=0;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->arg1;r[WF_ELAPSED]=a->elapsed_ms;
    return 0;
}

static const WFFamily api={WF_ABI_VERSION,ROW,LANES,4,"scroll",tasks,
                            valid,family_scroll_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
