#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 8192u
void family_travel_batch(uint32_t *words);
static const char *tasks[]={"book-flight-nodelay","book-flight","buy-ticket"};
static void words_text(char *out,const uint32_t *words,unsigned len){
    for(unsigned i=0;i<len;i++)out[i]=(char)words[i];out[len]=0;
}
static int catalog_name(const uint32_t *r,unsigned index,char *out){
    const uint32_t *slot=r+1024u+index*128u;unsigned len=slot[0];
    if(!len||len>96u)return -1;
    for(unsigned i=0;i<len;i++)if(slot[i+1]<32u||slot[i+1]>126u)return -1;
    if(slot[len+1])return -1;
    words_text(out,slot+1,len);return 0;
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=2u||r[WF_TASK]>=3u||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    if(r[WF_TASK]==2u){
        if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=10000u||
           r[32]>3u||r[33]<1u||r[33]>150u||r[40]!=4u)return -1;
        unsigned i=0;for(;i<255u&&r[512u+i];i++)
            if(r[512u+i]<32u||r[512u+i]>126u)return -1;
        if(i==255u)return -1;
        for(i=0;i<4;i++){
            const uint32_t *f=r+64u+i*4u;
            if(f[0]<50u||f[0]>1999u||f[1]>129600000u)return -1;
        }
        return 0;
    }
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=30000u||
       r[32]>7u||r[33]>7u||r[34]>92u||r[35]<1u||r[35]>6u||
       r[36]<1u||r[36]>6u||r[37]<1u||r[37]>91u||r[38]>1u||
       r[39]>1u||r[40]<3u||r[40]>4u||r[41]>7u||r[42]>1u||
       r[43]!=6u||r[44]<3u||r[44]>4u)return -1;
    unsigned i=0;for(;i<255u&&r[512+i];i++)
        if(r[512+i]<32u||r[512+i]>126u)return -1;
    if(i==255u)return -1;
    for(unsigned stream=0;stream<2;stream++)
        for(i=0;i<(stream?r[44]:r[40]);i++){
            const uint32_t *f=r+(stream?128u:64u)+4*i;
            if(f[0]<50u||f[0]>1199u||f[1]>129600000u)return -1;
        }
    for(i=1;i<=6;i++){char name[128];if(catalog_name(r,i,name))return -1;}
    return 0;
}
static int add(WFView *v,unsigned ref,unsigned role,unsigned flags,
               const char *name,const char *value,float y){
    if(v->count>=WF_MAX_NODES)return -1;
    WFNode *n=&v->nodes[v->count++];n->ref=ref;n->role=role;
    n->flags=flags;n->x=0;n->y=y;n->width=1;n->height=1;
    return wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value)?-1:0;
}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    unsigned len=0;while(len<255u&&r[512u+len])len++;
    char q[256];for(unsigned i=0;i<len;i++)q[i]=(char)r[512u+i];
    if(wf_text_add(v,q,len,&v->instruction))return -1;
    if(r[WF_TASK]==2u){
        for(unsigned i=0;i<4;i++){
            const uint32_t *flight=r+64u+4u*i;
            char label[64],duration[80];
            snprintf(label,sizeof label,"Book for $%u",flight[0]);
            snprintf(duration,sizeof duration,"%uh %um; data-duration=%ums",
                     flight[1]/3600000u,(flight[1]/60000u)%60u,flight[1]);
            if(add(v,i+1,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                   label,duration,(float)i))return -1;
        }
        return 0;
    }
    if(r[39]==0u){
        char date[16]="";
        if(r[34]){
            unsigned d=r[34],month=d<=31?10:d<=61?11:12;
            unsigned day=d<=31?d:d<=61?d-31:d-61;
            snprintf(date,sizeof date,"%02u/%02u/2016",month,day);
        }
        char from[128]="",to[128]="";
        if(r[32]&&r[32]<=6u&&catalog_name(r,r[32],from))return -1;
        if(r[33]&&r[33]<=6u&&catalog_name(r,r[33],to))return -1;
        if(r[32]==7u)strcpy(from,"Invalid airport");
        if(r[33]==7u)strcpy(to,"Invalid airport");
        if(add(v,1,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
            "From",from,0)||
           add(v,2,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
               "To",to,1)||
           add(v,3,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|WF_READONLY,
               "Departure Date",date,2)||
           add(v,4,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
               "Search","",3))return -1;
        for(unsigned i=1;i<=6;i++){
            char name[128];if(catalog_name(r,i,name))return -1;
            if(add(v,10+i,WF_OPTION,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                   name,"",(float)(4+i)))return -1;
        }
        for(unsigned i=1;i<=92;i++){
            unsigned month=i<=31?10:i<=61?11:12;
            unsigned day=i<=31?i:i<=61?i-31:i-61;
            char label[16];snprintf(label,sizeof label,"%02u/%02u/2016",month,day);
            if(add(v,20+i,WF_OPTION,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                   label,"",(float)(10+i)))return -1;
        }
    }else{
        if(add(v,5,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
               "Back","",0))return -1;
        for(unsigned i=0;i<(r[42]?r[44]:r[40]);i++){
            const uint32_t *flight=r+(r[42]?128u:64u)+4*i;
            char label[64],duration[64];
            snprintf(label,sizeof label,"Book flight for $%u",flight[0]);
            snprintf(duration,sizeof duration,"%uh %um; data-duration=%ums",
                     flight[1]/3600000u,(flight[1]/60000u)%60u,flight[1]);
            if(add(v,i+6,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                   label,duration,(float)(i+1)))return -1;
        }
    }
    return 0;
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[WF_OP]==WF_RESET||
       a->elapsed_ms<r[WF_ELAPSED]||a->text_length||a->arg1)return -1;
    if(r[WF_TASK]==2u){
        if((a->kind==WF_CLICK&&(a->target<1u||a->target>4u||a->arg0))||
           (a->kind==WF_WAIT&&(a->target||a->arg0))||
           (a->kind!=WF_CLICK&&a->kind!=WF_WAIT))return -1;
        r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
        r[WF_ARG0]=0;r[WF_ARG1]=0;r[WF_ELAPSED]=a->elapsed_ms;
        return 0;
    }
    if(a->kind==WF_SELECT_OPTION){
        if(r[39]||a->target<1u||a->target>3u||
           a->arg0>(a->target==3u?92u:7u))return -1;
    }else if(a->kind==WF_CLICK){
        if(a->arg0)return -1;
        if((!r[39]&&a->target!=4u)||
           (r[39]&&(a->target<5u||a->target>(r[42]?r[44]:r[40])+5u)))return -1;
    }else if(a->kind==WF_WAIT){
        if(a->target||a->arg0)return -1;
    }else return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ARG0]=a->arg0;r[WF_ARG1]=0;r[WF_ELAPSED]=a->elapsed_ms;
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,4,3,"travel",tasks,
    valid,family_travel_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
