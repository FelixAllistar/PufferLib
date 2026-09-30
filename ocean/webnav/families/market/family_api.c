#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 8192u
void family_market_batch(uint32_t *words);
static const char *const tasks[]={"stock-market"};

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
static int add_words(WFView *v,const uint32_t *src,unsigned cap,WFText *out){
    unsigned len=zlen(src,cap);if(len==cap)return -1;
    char buf[512];if(len>=sizeof buf)return -1;
    for(unsigned i=0;i<len;i++)buf[i]=(char)src[i];
    return wf_text_add(v,buf,len,out);
}
static void money(char *buf,size_t cap,unsigned cents){
    unsigned dollars=cents/100u,fraction=cents%100u;
    if(fraction%10u)snprintf(buf,cap,"$%u.%02u0",dollars,fraction);
    else snprintf(buf,cap,"$%u.%02u",dollars,fraction);
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=2u||r[WF_TASK]!=0u||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=10000u||
       r[32]>99u||r[33]>21000u||r[34]<1u||r[34]>21000u||
       r[35]!=100u||!ascii_z(r+512,256u)||!ascii_z(r+1000,16u))return -1;
    for(unsigned i=0;i<100;i++)if(r[64+i]<1u||r[64+i]>21000u)return -1;
    if(r[WF_OP]==WF_STEP&&r[WF_ACTION]!=WF_WAIT&&r[WF_ACTION]!=WF_CLICK)return -1;
    return 0;
}
static int observe(const uint32_t *r,WFView *v){
    if(!v||valid(r)||r[WF_OP]==WF_RESET)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(add_words(v,r+512,256u,&v->instruction))return -1;
    WFNode *n=&v->nodes[v->count++];
    n->ref=1u;n->role=WF_BUTTON;n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    n->width=n->height=1.0f;
    if(wf_text_add(v,"Buy",3u,&n->name)||
       wf_text_add(v,"",0u,&n->value))return -1;
    n=&v->nodes[v->count++];
    n->ref=2u;n->role=WF_TEXT;n->flags=WF_VISIBLE;n->width=n->height=1.0f;
    if(wf_text_add(v,"Stock price",11u,&n->name))return -1;
    char label[64]="";if(r[33])money(label,sizeof label,r[33]);
    if(wf_text_add(v,label,strlen(label),&n->value))return -1;
    n=&v->nodes[v->count++];
    n->ref=3u;n->role=WF_TEXT;n->flags=WF_VISIBLE;n->width=n->height=1.0f;
    if(wf_text_add(v,"Company",7u,&n->name)||
       add_words(v,r+1000,16u,&n->value))return -1;
    return 0;
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[WF_OP]==WF_RESET||
       a->elapsed_ms<r[WF_ELAPSED]||a->arg0||a->arg1||a->text_length)return -1;
    if((a->kind==WF_CLICK&&a->target!=1u)||
       (a->kind==WF_WAIT&&a->target)||
       (a->kind!=WF_CLICK&&a->kind!=WF_WAIT))return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ELAPSED]=a->elapsed_ms;return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,4,1,"market",tasks,
    valid,family_market_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
