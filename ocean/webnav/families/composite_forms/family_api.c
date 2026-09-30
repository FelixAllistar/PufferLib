#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 8192u
void family_composite_forms_batch(uint32_t *words);
static const char *tasks[]={"form-sequence","form-sequence-2","form-sequence-3",
    "multi-layouts","multi-orderings"};
static const char *options[]={"","5ft 9in","5ft 10in","5ft 11in",
    "6 ft","6ft 1in","6ft 2in"};
static const char *buttons[]={"","Yes","No","Maybe"};
static int ascii_len(const uint32_t *s,unsigned len,unsigned cap){
    if(len>cap||s[len])return 0;
    for(unsigned i=0;i<len;i++)if(s[i]<32u||s[i]>126u)return 0;
    return 1;
}

static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=5u||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    if(r[WF_STATUS]>WF_TIMEOUT||
       r[WF_DEADLINE]!=(r[WF_TASK]>=3u?20000u:10000u))return -1;
    if(r[WF_TASK]==0u){
        if(r[32]>20u||r[33]>7u||r[34]>20u||r[35]<1u||r[35]>3u||
           r[36]<50u||r[36]>114u)return -1;
    }else if(r[WF_TASK]==1u){
        if(r[32]>3u||r[34]<1u||r[34]>3u||r[35]<1u||r[35]>3u)return -1;
    }else if(r[WF_TASK]==2u){
        if(r[32]>6u||r[34]<1u||r[34]>5u||r[35]<1u||r[35]>3u)return -1;
    }else if(r[36]>4u||r[37]>5u||
             (r[WF_TASK]==4u&&r[36]!=2u))return -1;
    unsigned i=0;for(;i<255u&&r[128u+i];i++)
        if(r[128u+i]<32u||r[128u+i]>126u)return -1;
    if(i==255u)return -1;
    if(r[WF_TASK])for(unsigned field=0;field<3u;field++){
        if(!ascii_len(r+512u+field*128u,r[40u+field],63u)||
           !ascii_len(r+1024u+field*128u,r[44u+field],63u))return -1;
    }
    if(r[WF_OP]==WF_STEP&&r[WF_ACTION]==WF_INSERT&&
       !ascii_len(r+2048u,r[WF_ARG0],63u))return -1;
    return 0;
}

static int add_words(WFView *v,const uint32_t *s,unsigned len,WFText *out){
    char text[256];if(len>=sizeof text)return -1;
    for(unsigned i=0;i<len;i++)text[i]=(char)s[i];
    return wf_text_add(v,text,len,out);
}

static int add_node(WFView *v,unsigned ref,unsigned role,unsigned flags,
                    const char *name,const char *value,float x,float y){
    if(v->count>=WF_MAX_NODES)return -1;
    WFNode *n=&v->nodes[v->count++];n->ref=ref;n->role=role;n->flags=flags;
    n->x=x;n->y=y;n->width=1;n->height=1;
    return wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value)?-1:0;
}

static unsigned position(unsigned order,unsigned logical){
    static const unsigned permutation[6][3]={{0,1,2},{0,2,1},{1,0,2},
        {1,2,0},{2,0,1},{2,1,0}};
    for(unsigned i=0;i<3;i++)if(permutation[order][i]==logical)return i;
    return logical;
}

static const char *movie_label(unsigned layout,unsigned field){
    static const char *labels[5][3]={{"Genre:","Director:","Year:"},
        {"Genre","Director Name","Year"},{"Genre","Director","Year"},
        {"Movie Genre","Director Name","Released Date"},
        {"Genre","Director","Year"}};
    return labels[layout][field];
}

static const char *movie_button(unsigned layout){
    static const char *names[]={"Submit","Search","Submit","Go!","Search"};
    return names[layout];
}

static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    char value[16];unsigned len=0;while(len<255u&&r[128u+len])len++;
    char query[256];for(unsigned i=0;i<len;i++)query[i]=(char)r[128u+i];
    if(wf_text_add(v,query,len,&v->instruction))return -1;
    if(r[WF_TASK]==1u){
        static const char *radios[]={"1st radio","2nd radio","3rd radio"};
        static const char *inputs[]={"1st textbox","2nd textbox","3rd textbox"};
        for(unsigned i=0;i<3;i++)if(add_node(v,i+1,WF_RADIO,
            WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|(r[32]==i+1?WF_CHECKED:0),
            radios[i],"",(float)i,0))return -1;
        for(unsigned i=0;i<3;i++){
            if(add_node(v,i+4,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                inputs[i],"",(float)i,1))return -1;
            WFNode *n=&v->nodes[v->count-1];
            v->text_bytes=n->value.offset;
            if(add_words(v,r+512u+i*128u,r[40u+i],&n->value))return -1;
            n->capacity=64;
        }
        return add_node(v,7,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                        "Submit","",0,2);
    }
    if(r[WF_TASK]==2u){
        if(add_node(v,1,WF_SELECT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                    "Height",options[r[32]],0,0))return -1;
        for(unsigned i=1;i<=3;i++)if(add_node(v,i+1,WF_BUTTON,
            WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,buttons[i],"",(float)i,1))return -1;
        return 0;
    }
    if(r[WF_TASK]>=3u){
        for(unsigned i=0;i<3;i++){
            if(add_node(v,i+1,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                movie_label(r[36],i),"",0,(float)position(r[37],i)))return -1;
            WFNode *n=&v->nodes[v->count-1];
            v->text_bytes=n->value.offset;
            if(add_words(v,r+512u+i*128u,r[40u+i],&n->value))return -1;
            n->capacity=64;
        }
        return add_node(v,4,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                        movie_button(r[36]),"",0,3);
    }
    WFNode *n=v->nodes;
    v->count=5;
    n[0].ref=1;n[0].role=WF_SLIDER;n[0].flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    n[0].x=0;n[0].y=0;n[0].width=(float)r[36];n[0].height=1;n[0].capacity=21;
    snprintf(value,sizeof value,"%d",(int)r[32]-10);
    if(wf_text_add(v,"Slider",6,&n[0].name)||
       wf_text_add(v,value,strlen(value),&n[0].value))return -1;
    static const char *names[]={"1st checkbox","2nd checkbox","3rd checkbox"};
    for(unsigned i=0;i<3;i++){
        n[i+1].ref=i+2;n[i+1].role=WF_CHECKBOX;
        n[i+1].flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|((r[33]&(1u<<i))?WF_CHECKED:0u);
        n[i+1].x=(float)i;n[i+1].y=1;n[i+1].width=1;n[i+1].height=1;
        if(wf_text_add(v,names[i],strlen(names[i]),&n[i+1].name)||
           wf_text_add(v,"",0,&n[i+1].value))return -1;
    }
    n[4].ref=5;n[4].role=WF_BUTTON;
    n[4].flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE;
    n[4].x=0;n[4].y=2;n[4].width=1;n[4].height=1;
    return wf_text_add(v,"Submit",6,&n[4].name)||
           wf_text_add(v,"",0,&n[4].value)?-1:0;
}

static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[WF_OP]==WF_RESET||a->elapsed_ms<r[WF_ELAPSED]||
       (a->kind!=WF_INSERT&&a->text_length))return -1;
    if(a->kind==WF_SELECT_OPTION){
        if(a->target!=1u||a->arg0>(r[WF_TASK]==0u?20u:6u)||a->arg1||
           (r[WF_TASK]!=0u&&r[WF_TASK]!=2u))return -1;
    }else if(a->kind==WF_CLICK){
        if(a->arg0||a->arg1)return -1;
        if(r[WF_TASK]==0u&&(a->target<2u||a->target>5u))return -1;
        if(r[WF_TASK]==1u&&!(a->target>=1u&&a->target<=3u)&&a->target!=7u)return -1;
        if(r[WF_TASK]==2u&&(a->target<2u||a->target>4u))return -1;
        if(r[WF_TASK]>=3u&&a->target!=4u)return -1;
    }else if(a->kind==WF_INSERT){
        if(r[WF_TASK]==0u||r[WF_TASK]==2u||a->text_length>63u||
           (!a->text&&a->text_length)||a->arg0||a->arg1)return -1;
        if(r[WF_TASK]==1u&&(a->target<4u||a->target>6u))return -1;
        if(r[WF_TASK]>=3u&&(a->target<1u||a->target>3u))return -1;
        for(size_t i=0;i<a->text_length;i++)if((unsigned char)a->text[i]<32u||
                                              (unsigned char)a->text[i]>126u)return -1;
    }else if(a->kind==WF_WAIT){
        if(a->target||a->arg0||a->arg1)return -1;
    }else return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ARG0]=a->kind==WF_INSERT?(uint32_t)a->text_length:a->arg0;
    r[WF_ARG1]=0;r[WF_ELAPSED]=a->elapsed_ms;
    memset(r+2048u,0,64u*sizeof *r);
    if(a->kind==WF_INSERT)for(size_t i=0;i<a->text_length;i++)
        r[2048u+i]=(unsigned char)a->text[i];
    return 0;
}

static const WFFamily api={WF_ABI_VERSION,ROW,4,5,"composite_forms",tasks,
    valid,family_composite_forms_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
