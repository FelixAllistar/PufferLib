#ifndef WEBNAV_COMPOSITE_FORMS_PUBLIC_CONTROLLER_H
#define WEBNAV_COMPOSITE_FORMS_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char composite_payload[128];
static const char *composite_value(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)
        return wf_text_get(v,v->nodes[i].value);
    return NULL;
}
static const WFNode *composite_node(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return &v->nodes[i];
    return NULL;
}
static int composite_insert(WFAction *a,unsigned ref,const char *text,size_t len){
    if(len>63u)return -1;
    memcpy(composite_payload,text,len);composite_payload[len]=0;
    *a=(WFAction){.kind=WF_INSERT,.target=ref,.text=composite_payload,
        .text_length=len};return 0;
}
static int composite_target_text(WFAction *a,const WFView *v,unsigned ref,
                                 const char *text,size_t len){
    const char *current=composite_value(v,ref);
    if(!current)return -1;
    if(strlen(current)==len&&!memcmp(current,text,len))return 0;
    return composite_insert(a,ref,text,len)?-1:1;
}
static int composite_public_action(const WFView *v,WFAction *a){
    if(!v||!a||v->count>WF_MAX_NODES)return -1;
    const char *q=wf_text_get(v,v->instruction);if(!q)return -1;
    *a=(WFAction){.kind=WF_WAIT};
    if(composite_node(v,1)&&composite_node(v,1)->role==WF_SLIDER){
        int value=0;char ordinal[8]={0};
        if(sscanf(q,"Select %d with the slider, click the %7s checkbox",
                  &value,ordinal)!=2||value< -10||value>10)return -1;
        char wanted[24];snprintf(wanted,sizeof wanted,"%s checkbox",ordinal);
        char shown[16];snprintf(shown,sizeof shown,"%d",value);
        if(strcmp(composite_value(v,1),shown)){
            *a=(WFAction){.kind=WF_SELECT_OPTION,.target=1,.arg0=(uint32_t)(value+10)};
            return 0;
        }
        for(unsigned ref=2;ref<=4;ref++){
            const WFNode *n=composite_node(v,ref);const char *name=n?wf_text_get(v,n->name):NULL;
            if(!n||!name)return -1;
            if(((n->flags&WF_CHECKED)!=0)!=(strcmp(name,wanted)==0)){
                *a=(WFAction){.kind=WF_CLICK,.target=ref};return 0;
            }
        }
        *a=(WFAction){.kind=WF_CLICK,.target=5};return 0;
    }
    if(composite_node(v,1)&&composite_node(v,1)->role==WF_RADIO){
        const char *quoted=strchr(q,'"');const char *end=quoted?strchr(++quoted,'"'):NULL;
        const char *into=strstr(q,"into the ");
        if(!end||!into)return -1;
        unsigned box=(unsigned)strtoul(into+9,NULL,10);
        if(box<1||box>3)return -1;
        int selected=0;for(unsigned i=1;i<=3;i++)selected|=(composite_node(v,i)->flags&WF_CHECKED)!=0;
        if(!selected){*a=(WFAction){.kind=WF_CLICK,.target=1};return 0;}
        for(unsigned i=1;i<=3;i++){
            const char *goal=i==box?quoted:"";size_t len=i==box?(size_t)(end-quoted):0;
            int changed=composite_target_text(a,v,i+3,goal,len);
            if(changed<0)return -1;if(changed)return 0;
        }
        *a=(WFAction){.kind=WF_CLICK,.target=7};return 0;
    }
    if(composite_node(v,1)&&composite_node(v,1)->role==WF_SELECT){
        static const char *opts[]={"","5ft 9in","5ft 10in","5ft 11in",
            "6 ft","6ft 1in","6ft 2in"};
        const char *start=q+7,*end=strstr(q," from the dropdown");
        if(strncmp(q,"Choose ",7)||!end)return -1;
        unsigned option=0;for(unsigned i=1;i<7;i++)
            if(strlen(opts[i])==(size_t)(end-start)&&!memcmp(start,opts[i],end-start))option=i;
        if(!option)return -1;
        if(strcmp(composite_value(v,1),opts[option])){
            *a=(WFAction){.kind=WF_SELECT_OPTION,.target=1,.arg0=option};return 0;
        }
        const char *quoted=strchr(end,'"');const char *close=quoted?strchr(++quoted,'"'):NULL;
        if(!close)return -1;
        for(unsigned i=2;i<=4;i++){
            const WFNode *n=composite_node(v,i);const char *name=n?wf_text_get(v,n->name):NULL;
            if(name&&strlen(name)==(size_t)(close-quoted)&&
               !memcmp(name,quoted,close-quoted)){
                *a=(WFAction){.kind=WF_CLICK,.target=i};return 0;
            }
        }
        return -1;
    }
    if(!strncmp(q,"Search for ",11)){
        const char *genre=q+11,*director=strstr(genre," movies directed by ");
        const char *year=director?strstr(director+20," from year "):NULL;
        const char *end=year?strchr(year+11,'.'):NULL;
        if(!director||!year||!end)return -1;
        int changed=composite_target_text(a,v,1,genre,(size_t)(director-genre));
        if(changed<0)return -1;if(changed)return 0;
        changed=composite_target_text(a,v,2,director+20,(size_t)(year-(director+20)));
        if(changed<0)return -1;if(changed)return 0;
        changed=composite_target_text(a,v,3,year+11,(size_t)(end-(year+11)));
        if(changed<0)return -1;if(changed)return 0;
        *a=(WFAction){.kind=WF_CLICK,.target=4};return 0;
    }
    return -1;
}
#endif
