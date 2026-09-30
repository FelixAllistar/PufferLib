#include "../common/family_api.h"
#include <string.h>

#define ROW 8192u
#define LANES 4u
#define POST_BASE 64u
#define POST_STRIDE 256u
#define QUERY 3000u

void family_social_batch(uint32_t *words);
static const char *const tasks[]={"social-media","social-media-all","social-media-some"};
static const char *const controls[]={"Reply","Retweet","Like","More",
    "Share via DM","Copy link to Tweet","Embed Tweet","Mute","Block","Report"};

static int ascii_z(const uint32_t *s,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!s[i])return 1;
        if(s[i]<32u||s[i]>126u)return 0;
    }
    return 0;
}
static unsigned zlen(const uint32_t *s,unsigned cap){
    unsigned i=0;while(i<cap&&s[i])i++;return i;
}
static const uint32_t *post(const uint32_t *r,unsigned index){
    return r+POST_BASE+(index-1u)*POST_STRIDE;
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=3u||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    unsigned single=r[WF_TASK]==0u,count=r[32];
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=(single?15000u:20000u)||
       count<(single?5u:6u)||count>(single?9u:11u)||r[33]>count||
       (!single&&r[33])||r[34]<1u||r[34]>count||
       r[35]>(single?8u:3u)||r[36]<1u||r[36]>count||
       !ascii_z(r+QUERY,512u))return -1;
    for(unsigned i=1;i<=r[32];i++){
        const uint32_t *p=post(r,i);
        if(!ascii_z(p,64u)||!ascii_z(p+64,64u)||
           !ascii_z(p+128,96u)||!ascii_z(p+224,28u)||p[252]>15u||
           (single&&p[252]))return -1;
    }
    if(r[WF_OP]==WF_STEP){
        if(r[WF_ACTION]!=WF_WAIT&&r[WF_ACTION]!=WF_CLICK)return -1;
        if(r[WF_ACTION]==WF_WAIT&&r[WF_TARGET])return -1;
    }
    return 0;
}
static int units(WFView *v,const uint32_t *s,unsigned cap,WFText *out){
    char buf[513];unsigned n=zlen(s,cap);if(n>=cap||n>512u)return -1;
    for(unsigned i=0;i<n;i++)buf[i]=(char)s[i];
    return wf_text_add(v,buf,n,out);
}
static WFNode *node(WFView *v,unsigned ref,unsigned role,unsigned flags,unsigned parent){
    if(v->count==WF_MAX_NODES){v->omitted++;return NULL;}
    WFNode *n=v->nodes+v->count++;*n=(WFNode){0};
    n->ref=ref;n->role=role;n->flags=flags|WF_VISIBLE;n->parent=parent;
    n->y=(float)(v->count-1u);n->width=n->height=1.0f;
    return n;
}
static int label(WFView *v,WFNode *n,const char *name){
    return n?wf_text_add(v,name,strlen(name),&n->name):-1;
}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET||!v)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    /* All unfilled WFText fields now refer to the empty string. */
    WFText empty;if(wf_text_add(v,"",0,&empty))return -1;
    if(units(v,r+QUERY,512u,&v->instruction))return -1;
    for(unsigned i=1;i<=r[32];i++){
        const uint32_t *p=post(r,i);unsigned parent=16u*i+10u;
        WFNode *n=node(v,parent,WF_TEXT,0,0);
        if(!n||units(v,p,64u,&n->name)||units(v,p+64,64u,&n->value))return -1;
        n=node(v,parent+1u,WF_TEXT,0,parent);
        if(label(v,n,"Body")||units(v,p+128,96u,&n->value))return -1;
        if(r[WF_TASK]==0u){
            n=node(v,parent+2u,WF_TEXT,0,parent);
            if(label(v,n,"Time")||units(v,p+224,28u,&n->value))return -1;
        }
        for(unsigned slot=0;slot<4;slot++){
            n=node(v,16u*i+slot,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,parent);
            if(label(v,n,slot==3u&&r[WF_TASK]!=0u?"Share":controls[slot]))return -1;
            if(r[WF_TASK]==0u){if(slot==3u&&r[33]==i)n->flags|=WF_EXPANDED;}
            else if(p[252]&(1u<<slot))n->flags|=WF_CHECKED;
        }
        if(r[WF_TASK]==0u&&r[33]==i)for(unsigned slot=4;slot<10;slot++){
            n=node(v,16u*i+slot,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,parent);
            if(label(v,n,controls[slot]))return -1;
        }
    }
    if(r[WF_TASK]!=0u){
        WFNode *n=node(v,1u,WF_BUTTON,WF_ENABLED|WF_CLICKABLE,0);
        if(label(v,n,"Submit"))return -1;
    }
    return v->omitted?-1:0;
}
static int visible_target(const uint32_t *r,unsigned ref){
    if(r[WF_TASK]!=0u&&ref==1u)return 1;
    unsigned i=ref/16u,slot=ref%16u;
    if(i<1u||i>r[32]||slot>9u)return 0;
    if(r[WF_TASK]!=0u)return slot<4u;
    return slot<4u||r[33]==i;
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[WF_OP]==WF_RESET||a->elapsed_ms<r[WF_ELAPSED]||
       a->arg0||a->arg1||a->text_length||
       (a->kind!=WF_WAIT&&a->kind!=WF_CLICK))return -1;
    if(a->kind==WF_CLICK){if(!visible_target(r,a->target))return -1;}
    else if(a->target)return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ELAPSED]=a->elapsed_ms;
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,LANES,3,"social",tasks,
    valid,family_social_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
