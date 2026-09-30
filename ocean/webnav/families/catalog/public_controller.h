#ifndef WEBNAV_CATALOG_PUBLIC_CONTROLLER_H
#define WEBNAV_CATALOG_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const WFNode *catalog_public_ref(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static int catalog_public_copy(char *out,size_t cap,const char *begin,const char *end){
    if(!out||!begin||!end||end<=begin||(size_t)(end-begin)>=cap)return -1;
    memcpy(out,begin,(size_t)(end-begin));out[end-begin]=0;return 0;
}
static int catalog_public_phone(const WFView *v,const char *q,WFAction *a){
    const char *start=strstr(q,"Find "),*end=strstr(q," in the contact book");
    if(!start||!end)return -1;start+=5;
    char wanted[128];if(catalog_public_copy(wanted,sizeof wanted,start,end))return -1;
    unsigned property=strstr(q,"phone number")?0u:strstr(q,"email")?1u:2u;
    const WFNode *n=catalog_public_ref(v,99u);if(!n)return -1;
    const char *name=wf_text_get(v,n->name);if(!name)return -1;
    if(!strcmp(name,wanted)){a->kind=WF_CLICK;a->target=100u+property;return 0;}
    unsigned page=0u;
    for(unsigned i=1;i<=5u;i++){
        n=catalog_public_ref(v,i);
        if(n&&(n->flags&WF_SELECTED)){page=i;break;}
    }
    if(!page)return -1;
    if(page>=5u)return -1;
    a->kind=WF_CLICK;a->target=page+1u;return 0;
}
static unsigned catalog_public_qty(const WFView *v,const WFNode *n){
    const char *s=wf_text_get(v,n->value);return s?(unsigned)strtoul(s,NULL,10):0u;
}
static int catalog_public_food(const WFView *v,const char *q,WFAction *a){
    int named=!strncmp(q,"Order one of each item: ",24);
    unsigned required=0u,total=0u,eligible=0u,removable=0u;
    const char *type_name=NULL;
    if(!named){
        if(strncmp(q,"Order ",6))return -1;
        char *tail;required=(unsigned)strtoul(q+6,&tail,10);
        type_name=strstr(q," items that are ");if(!type_name||!required)return -1;
        type_name+=16;
    }
    for(unsigned i=0;i<12u;i++){
        unsigned parent=16u*(i+1u)+2u;
        const WFNode *n=catalog_public_ref(v,parent);if(!n)return -1;
        const char *name=wf_text_get(v,n->name);if(!name)return -1;
        unsigned quantity=catalog_public_qty(v,n);
        if(named){
            unsigned desired=strstr(q,name)!=NULL;
            if(quantity!=desired){
                a->kind=WF_CLICK;a->target=16u*(i+1u)+(quantity>desired?0u:1u);return 0;
            }
        }else{
            int can=0;
            for(unsigned j=0;j<v->count;j++){
                const WFNode *label=v->nodes+j;
                const char *text=wf_text_get(v,label->name);
                if(label->parent==parent&&text&&!strcmp(text,type_name)){can=1;break;}
            }
            if(!can&&quantity){a->kind=WF_CLICK;a->target=16u*(i+1u);return 0;}
            if(can){
                total+=quantity;
                if(!eligible)eligible=i+1u;
                if(quantity&&!removable)removable=i+1u;
            }
        }
    }
    if(!named){
        if(!eligible)return -1;
        if(total!=required){
            a->kind=WF_CLICK;
            if(total>required&&!removable)return -1;
            a->target=16u*(total>required?removable:eligible)+
                      (total>required?0u:1u);return 0;
        }
    }
    if(!catalog_public_ref(v,1u))return -1;
    a->kind=WF_CLICK;a->target=1u;return 0;
}
static int catalog_public_caseeq(const char *a,const char *b){
    for(;*a&&*b;a++,b++)if(tolower((unsigned char)*a)!=tolower((unsigned char)*b))return 0;
    return *a==*b;
}
static int catalog_public_search(const WFView *v,const char *q,
                                 char *scratch,size_t cap,WFAction *a){
    const char *first=strchr(q,'"');if(!first)return -1;
    const char *last=strchr(first+1,'"');if(!last)return -1;
    if(catalog_public_copy(scratch,cap,first+1,last))return -1;
    const char *ordinal=strstr(q,"click the ");if(!ordinal)return -1;
    char *tail;unsigned position=(unsigned)strtoul(ordinal+10,&tail,10);
    if(position<1u||position>9u)return -1;
    unsigned index=position-1u,page=index/3u+1u;
    const WFNode *input=catalog_public_ref(v,1u);if(!input)return -1;
    const char *value=wf_text_get(v,input->value);if(!value)return -1;
    if(!catalog_public_caseeq(value,scratch)){
        if(!(input->flags&WF_FOCUSED)){a->kind=WF_CLICK;a->target=1u;return 0;}
        if(*value&&(input->selection_start!=0u||
                     input->selection_end!=strlen(value))){
            a->kind=WF_SELECT_ALL;return 0;
        }
        a->kind=WF_INSERT;a->text=scratch;a->text_length=strlen(scratch);return 0;
    }
    if(!catalog_public_ref(v,10u)||catalog_public_ref(v,200u)){
        a->kind=WF_CLICK;a->target=2u;return 0;
    }
    const WFNode *tab=catalog_public_ref(v,9u+page);
    if(!tab)return -1;
    if(!(tab->flags&WF_SELECTED)){a->kind=WF_CLICK;a->target=9u+page;return 0;}
    if(!catalog_public_ref(v,100u+index))return -1;
    a->kind=WF_CLICK;a->target=100u+index;return 0;
}
static int catalog_public_next(const WFView *v,char *scratch,size_t cap,
                               WFAction *a){
    if(!v||!scratch||!cap||!a)return -1;
    const char *q=wf_text_get(v,v->instruction);if(!q)return -1;
    *a=(WFAction){.kind=WF_WAIT,.elapsed_ms=v->elapsed_ms+100u};
    if(!strncmp(q,"Find ",5))return catalog_public_phone(v,q,a);
    if(!strncmp(q,"Order ",6))return catalog_public_food(v,q,a);
    return catalog_public_search(v,q,scratch,cap,a);
}
#endif
