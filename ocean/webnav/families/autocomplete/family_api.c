#include "../common/family_api.h"
#include "../../miniwob/autocomplete/validation.h"
#include <string.h>

void family_autocomplete_batch(uint32_t *words);
static const char *tasks[]={"use-autocomplete-nodelay","use-autocomplete"};
static int ascii(const uint32_t *s,unsigned limit,char *out){
    for(unsigned i=0;i<limit;i++){unsigned c=s[i];out[i]=(char)c;if(!c)return (int)i;if(c<32||c>126)return -1;}return -1;
}
static int packed(const uint32_t *s,char *out){
    for(unsigned i=0;i<64;i++){unsigned c=(s[i/4]>>(8*(i%4)))&255;out[i]=(char)c;if(!c)return (int)i;if(c<32||c>126)return -1;}return -1;
}
static int valid(const uint32_t *r){
    if(!r||r[0]!=2||r[1]>1||r[2]>WF_STEP)return -1;
    if(r[2]==WF_RESET)return r[6]?-1:0;
    const uint32_t *b=r+32;char out[128];
    if(!webnav_autocomplete_valid(b)||r[9]!=b[3]||r[12]!=10000||ascii(r+320,128,out)<0)return -1;
    if(r[1]){
        if(r[288]>10300||r[289]>1||r[290]>64)return -1;
        for(unsigned i=0;i<r[290];i++)if(r[1536+i]<32||r[1536+i]>126)return -1;
    }
    for(unsigned i=0;i<b[20]&&i<64;i++)if(packed(r+512+16*i,out)<0)return -1;
    return 0;
}
static int add_units(WFView *v,const uint32_t *s,unsigned n,WFText *out){
    char text[129];if(n>128)return -1;for(unsigned i=0;i<n;i++)text[i]=(char)s[i];return wf_text_add(v,text,n,out);
}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[2]==WF_RESET)return -1;
    const uint32_t *b=r+32;char text[128];wf_view_init(v,r[8],r[12]);
    int n=ascii(r+320,128,text);if(n<0||wf_text_add(v,text,n,&v->instruction))return -1;
    WFNode *field=v->nodes+v->count++;
    field->ref=1;field->role=WF_INPUT;field->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|(b[2]==1?WF_FOCUSED:0);
    field->selection_start=b[17];field->selection_end=b[18];field->capacity=65;
    field->width=field->height=1;
    if(wf_text_add(v,"Tags:",5,&field->name)||add_units(v,b+32,b[16],&field->value))return -1;
    WFNode *submit=v->nodes+v->count++;submit->ref=2;submit->role=WF_BUTTON;
    submit->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|(b[2]==2?WF_FOCUSED:0);submit->y=1;submit->width=submit->height=1;
    if(wf_text_add(v,"Submit",6,&submit->name)||wf_text_add(v,"",0,&submit->value))return -1;
    v->omitted=b[20]>64?b[20]-64:0;
    for(unsigned i=0;i<b[20]&&i<64;i++){
        WFNode *node=v->nodes+v->count++;node->ref=i+3;node->parent=1;node->role=WF_OPTION;
        node->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|(b[21]==i+1?WF_SELECTED:0);
        node->y=(float)i+2;node->width=node->height=1;n=packed(r+512+16*i,text);
        if(n<0||wf_text_add(v,text,n,&node->name)||wf_text_add(v,"",0,&node->value))return -1;
    }
    return 0;
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[2]==WF_RESET||a->elapsed_ms<r[8]||a->elapsed_ms>10000||a->arg1)return -1;
    uint32_t *b=r+32;unsigned command=0,index=0;
    if(a->kind==WF_CLICK){
        if(a->arg0||!a->target||a->target>66)return -1;
        if(a->target==1)command=1;
        else if(a->target==2)command=10;
        else {index=a->target-3;if(index>=b[20])return -1;command=14;}
    }else{
        if(a->target)return -1;
        if(a->kind==WF_KEY_DOWN){if(a->arg0==40)command=12;else if(a->arg0==38)command=13;else return -1;}
        else {if(a->arg0)return -1;
            if(a->kind==WF_WAIT)command=0;
            else if(a->kind>=WF_INSERT&&a->kind<=WF_SELECT_ALL)command=a->kind;
            else if(a->kind==WF_ENTER)command=15;
            else return -1;
        }
        if(command&&b[2]!=1)return -1;
    }
    if(command==2){
        if(a->text_length>32||(!a->text&&a->text_length)||b[16]+a->text_length-(b[18]-b[17])>64)return -1;
        for(size_t i=0;i<a->text_length;i++)if((unsigned char)a->text[i]<32||(unsigned char)a->text[i]>126)return -1;
    }else if(a->text_length)return -1;
    r[2]=WF_STEP;r[4]=a->kind;r[5]=a->target;r[6]=a->arg0;r[8]=a->elapsed_ms;
    b[5]=a->elapsed_ms;b[7]=command;b[8]=index;b[11]=command==2?(unsigned)a->text_length:0;
    memset(b+224,0,32*sizeof *b);if(command==2)for(unsigned i=0;i<b[11];i++)b[224+i]=(unsigned char)a->text[i];
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,2048,4,2,"autocomplete",tasks,valid,family_autocomplete_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
