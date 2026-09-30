#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 2048u
void family_geometry_batch(uint32_t *words);
static const char *const tasks[]={"bisect-angle","circle-center",
    "find-midpoint","grid-coordinate","right-angle"};

static int ascii_z(const uint32_t *s,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!s[i])return 1;
        if(s[i]<32u||s[i]>126u)return 0;
    }
    return 0;
}
static int add_words(WFView *v,const uint32_t *src,unsigned cap,WFText *out){
    char buf[256];unsigned i=0;
    while(i<cap&&src[i]){if(i+1u>=sizeof buf)return -1;buf[i]=(char)src[i];i++;}
    if(i==cap)return -1;
    return wf_text_add(v,buf,i,out);
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=2u||r[WF_TASK]>=5u||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=10000u||
       !ascii_z(r+512,256u))return -1;
    if(r[WF_TASK]==3u){
        if(r[32]>=25u||r[33]>25u)return -1;
    }else{
        if(r[40]>150u||r[41]>130u||r[42]>150u||r[43]>130u||
           r[44]>150u||r[45]>130u||r[46]>39u||
           r[47]>150u*256u||r[48]>130u*256u||r[49]>1u)return -1;
        if(r[WF_TASK]==1u&&(r[46]<25u||r[40]<r[46]||r[41]<r[46]))return -1;
    }
    if(r[WF_OP]==WF_STEP&&r[WF_ACTION]!=WF_WAIT&&r[WF_ACTION]!=WF_CLICK)return -1;
    return 0;
}
static int named(WFView *v,WFNode *n,const char *name,const char *value){
    return wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value)?-1:0;
}
static int grid_view(const uint32_t *r,WFView *v){
    for(unsigned i=0;i<25u;i++){
        WFNode *n=&v->nodes[v->count++];
        unsigned col=i/5u,row=i%5u;
        n->ref=i+1u;n->role=WF_BUTTON;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        n->x=(float)(col*30u+11u);n->y=(float)(row*30u+11u);
        n->width=n->height=8.0f;
        char label[24];
        snprintf(label,sizeof label,"(%d,%d)",(int)col-2,2-(int)row);
        if(named(v,n,label,""))return -1;
    }
    return 0;
}
static int shape(WFView *v,unsigned ref,const char *name,
                 float cx,float cy,float radius){
    WFNode *n=&v->nodes[v->count++];
    n->ref=ref;n->parent=1u;n->role=WF_OTHER;n->flags=WF_VISIBLE;
    n->x=cx-radius;n->y=cy-radius;
    n->width=n->height=radius*2.0f;
    return named(v,n,name,"");
}
static int drawing_view(const uint32_t *r,WFView *v){
    WFNode *n=&v->nodes[v->count++];
    n->ref=1u;n->role=WF_CANVAS;n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    n->width=150.0f;n->height=130.0f;
    if(named(v,n,"Drawing surface",""))return -1;
    n=&v->nodes[v->count++];
    n->ref=2u;n->role=WF_BUTTON;n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    n->x=30.0f;n->y=135.0f;n->width=80.0f;n->height=22.0f;
    if(named(v,n,"Submit",""))return -1;
    if(r[WF_TASK]==1u){
        if(shape(v,10u,"Circle",(float)r[40],(float)r[41],(float)r[46]))return -1;
    }else{
        const char *first=r[WF_TASK]==4u?"Black point":"Black point A";
        const char *second=r[WF_TASK]==4u?"Blue vertex":"Black point B";
        if(shape(v,10u,first,(float)r[40],(float)r[41],3.5f)||
           shape(v,11u,second,(float)r[42],(float)r[43],3.5f))return -1;
        if(r[WF_TASK]==0u&&
           shape(v,12u,"Blue vertex",(float)r[44],(float)r[45],3.5f))return -1;
    }
    if(r[49]&&shape(v,20u,"Your point",r[47]/256.0f,r[48]/256.0f,
                    r[WF_TASK]==4u?3.0f:3.5f))return -1;
    return 0;
}
static int observe(const uint32_t *r,WFView *v){
    if(!v||valid(r)||r[WF_OP]==WF_RESET)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(add_words(v,r+512,256u,&v->instruction))return -1;
    return r[WF_TASK]==3u?grid_view(r,v):drawing_view(r,v);
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[WF_OP]==WF_RESET||
       a->elapsed_ms<r[WF_ELAPSED]||a->text_length)return -1;
    if(a->kind==WF_WAIT){
        if(a->target||a->arg0||a->arg1)return -1;
    }else if(a->kind==WF_CLICK){
        if(r[WF_TASK]==3u){
            if(a->target<1u||a->target>25u||a->arg0||a->arg1)return -1;
        }else if(a->target==1u){
            if(a->arg0>150u*256u||a->arg1>130u*256u)return -1;
        }else if(a->target==2u){
            if(a->arg0||a->arg1)return -1;
        }else return -1;
    }else return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->arg1;
    r[WF_ELAPSED]=a->elapsed_ms;return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,4,5,"geometry",tasks,
    valid,family_geometry_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
