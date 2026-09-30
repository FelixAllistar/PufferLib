#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 1024u
#define LANES 4u
#define QUERY 256u
#define INCOMING 128u
void family_typed_inputs_batch(uint32_t *words);

static const char *const tasks[]={"enter-date","enter-time","unicode-test"};

static int ascii(const uint32_t *p,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!p[i])return 1;
        if(p[i]<32||p[i]>126)return 0;
    }
    return 0;
}

static int scalar(uint32_t c){
    return c>=32&&c<=0x10ffff&&!(c>=0xd800&&c<=0xdfff);
}

static int unicode(const uint32_t *p,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!p[i])return 1;
        if(!scalar(p[i]))return 0;
    }
    return 0;
}

static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=3||r[WF_OP]>WF_STEP)
        return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    unsigned task=r[WF_TASK],deadline=task==2?10000u:20000u;
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=deadline||
       r[WF_ELAPSED]>deadline)return -1;
    if(task==0){
        if(r[32]<2010||r[32]>2019||r[33]<1||r[33]>12||
           r[34]<1||r[34]>31||r[38]>1||!ascii(r+QUERY,128))return -1;
        if(r[38]&&(r[35]<1||r[35]>9999||r[36]<1||r[36]>12||
                   r[37]<1||r[37]>31))return -1;
    }else if(task==1){
        if(r[32]>23||r[33]>59||r[38]>1||!ascii(r+QUERY,128))return -1;
        if(r[38]&&(r[35]>23||r[36]>59))return -1;
    }else{
        if(r[32]>=6||!unicode(r+QUERY,128))return -1;
        unsigned buttons=0;
        for(unsigned i=0;i<6;i++){
            if(r[40+i]>2||r[48+i]>=6||!unicode(r+512+i*64,64))return -1;
            if(r[40+i]==2)buttons++;
        }
        if(!buttons)return -1;
    }
    return 0;
}

static int add_ascii(WFView *v,const uint32_t *p,unsigned cap,WFText *out){
    char s[129];unsigned n=0;
    while(n<cap&&p[n]){s[n]=(char)p[n];n++;}
    return wf_text_add(v,s,n,out);
}

static int add_unicode(WFView *v,const uint32_t *p,unsigned cap,WFText *out){
    char s[513];unsigned n=0;
    for(unsigned i=0;i<cap&&p[i];i++){
        uint32_t c=p[i];if(!scalar(c))return -1;
        if(c<0x80){if(n+1>=sizeof s)return -1;s[n++]=(char)c;}
        else if(c<0x800){
            if(n+2>=sizeof s)return -1;
            s[n++]=(char)(0xc0|(c>>6));s[n++]=(char)(0x80|(c&63));
        }else if(c<0x10000){
            if(n+3>=sizeof s)return -1;
            s[n++]=(char)(0xe0|(c>>12));s[n++]=(char)(0x80|((c>>6)&63));
            s[n++]=(char)(0x80|(c&63));
        }else{
            if(n+4>=sizeof s)return -1;
            s[n++]=(char)(0xf0|(c>>18));s[n++]=(char)(0x80|((c>>12)&63));
            s[n++]=(char)(0x80|((c>>6)&63));s[n++]=(char)(0x80|(c&63));
        }
    }
    return wf_text_add(v,s,n,out);
}

static WFNode *node(WFView *v,unsigned ref,unsigned role,unsigned flags,
                    const char *name,const char *value,float x,float y){
    if(v->count==WF_MAX_NODES){v->omitted++;return NULL;}
    WFNode *n=&v->nodes[v->count++];
    n->ref=ref;n->role=role;n->flags=flags;
    n->x=x;n->y=y;n->width=150;n->height=22;
    if(wf_text_add(v,name,strlen(name),&n->name)||
       wf_text_add(v,value,strlen(value),&n->value))return NULL;
    return n;
}

static int typed_view(const uint32_t *r,WFView *v){
    char value[16]="";
    if(r[38]){
        if(r[WF_TASK]==0)
            snprintf(value,sizeof value,"%04u-%02u-%02u",r[35],r[36],r[37]);
        else snprintf(value,sizeof value,"%02u:%02u",r[35],r[36]);
    }
    WFNode *input=node(v,1,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                       r[WF_TASK]==0?"Date":"Time",value,0,0);
    if(!input)return -1;
    input->capacity=r[WF_TASK]==0?10:5;
    input->selection_start=input->selection_end=(uint32_t)strlen(value);
    return node(v,2,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                "Submit","",0,35)?0:-1;
}

static int unicode_view(const uint32_t *r,WFView *v){
    for(unsigned i=0;i<6;i++){
        unsigned kind=r[40+i];
        unsigned role=kind==2?WF_BUTTON:kind==1?WF_INPUT:WF_TEXT;
        unsigned flags=WF_VISIBLE|(kind?WF_ENABLED|WF_CLICKABLE:0);
        WFNode *n=node(v,i+3,role,flags,"","",0,10.0f+i*30.0f);
        if(!n)return -1;
        if(add_unicode(v,r+512+i*64,64,&n->name))return -1;
        if(kind==1)n->capacity=64;
    }
    return 0;
}

static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET||!v)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(r[WF_TASK]==2){
        if(add_unicode(v,r+QUERY,128,&v->instruction))return -1;
        return unicode_view(r,v);
    }
    if(add_ascii(v,r+QUERY,128,&v->instruction))return -1;
    return typed_view(r,v);
}

static int action(uint32_t *r,const WFAction *a){
    if(valid(r)||!a||r[WF_STATUS]!=WF_RUNNING||
       a->elapsed_ms<r[WF_ELAPSED]||a->elapsed_ms>r[WF_DEADLINE]||
       (a->kind!=WF_INSERT&&a->text_length))return -1;
    if(r[WF_TASK]==2){
        if(a->kind==WF_CLICK){
            if(a->target<3||a->target>8||r[40+a->target-3]!=2)return -1;
        }else if(a->kind!=WF_WAIT)return -1;
    }else if(a->kind==WF_INSERT){
        if(a->target!=1||!a->text||
           a->text_length>(r[WF_TASK]==0?10u:5u))return -1;
        for(size_t i=0;i<a->text_length;i++){
            unsigned char ch=(unsigned char)a->text[i];
            if(ch<32||ch>126)return -1;
        }
        for(unsigned i=0;i<10;i++)r[INCOMING+i]=
            i<a->text_length?(unsigned char)a->text[i]:0;
    }else if(a->kind==WF_CLICK){
        if(a->target!=1&&a->target!=2)return -1;
    }else if(a->kind!=WF_WAIT)return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->arg1;r[WF_ELAPSED]=a->elapsed_ms;
    return 0;
}

static const WFFamily api={WF_ABI_VERSION,ROW,LANES,3,"typed_inputs",tasks,
                           valid,family_typed_inputs_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
