#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 1024u
#define LANES 4u
#define QUERY 256u
void family_board_batch(uint32_t *words);
static const char *const tasks[]={"tic-tac-toe"};

static int ascii(const uint32_t *p,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!p[i])return 1;
        if(p[i]<32||p[i]>126)return 0;
    }
    return 0;
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]!=0||r[WF_OP]>WF_STEP)
        return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=10000||
       r[WF_ELAPSED]>10000||r[32]>=10000||r[33]>511||r[34]>511||
       (r[33]&r[34])||!ascii(r+QUERY,96))return -1;
    return 0;
}
static int add_ascii(WFView *v,const uint32_t *p,unsigned cap,WFText *out){
    char s[97];unsigned n=0;
    while(n<cap&&p[n]){s[n]=(char)p[n];n++;}
    return wf_text_add(v,s,n,out);
}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET||!v)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(add_ascii(v,r+QUERY,96,&v->instruction))return -1;
    for(unsigned i=0;i<9;i++){
        if(v->count==WF_MAX_NODES){v->omitted++;return -1;}
        WFNode *n=&v->nodes[v->count++];
        n->ref=i+1;n->role=WF_CELL;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
        n->x=(float)(i%3)*42;n->y=(float)(i/3)*42;
        n->width=n->height=40;
        char name[16];snprintf(name,sizeof name,"Cell %u",i);
        const char *value=(r[33]&(1u<<i))?"X":(r[34]&(1u<<i))?"O":"";
        if(wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value))return -1;
    }
    return 0;
}
static int action(uint32_t *r,const WFAction *a){
    if(valid(r)||!a||r[WF_STATUS]!=WF_RUNNING||
       a->elapsed_ms<r[WF_ELAPSED]||a->elapsed_ms>10000||
       a->text_length)return -1;
    if(a->kind==WF_CLICK){if(a->target<1||a->target>9)return -1;}
    else if(a->kind!=WF_WAIT)return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->arg1;r[WF_ELAPSED]=a->elapsed_ms;
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,LANES,1,"board",tasks,
                           valid,family_board_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
