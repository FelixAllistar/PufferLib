#ifndef WEBNAV_TYPED_INPUTS_PUBLIC_CONTROLLER_H
#define WEBNAV_TYPED_INPUTS_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

/* All decisions use WFView alone. The caller owns scratch until wf_apply.
   elapsed_ms remains the caller's chosen action time. */
static int typed_inputs_public_action(const WFView *v,WFAction *a,
                                      char *scratch,size_t cap){
    if(!v||!a||!scratch||!cap)return -1;
    const char *query=wf_text_get(v,v->instruction);
    if(!query)return -1;
    if(!strncmp(query,"Click on the \"",14)){
        const char *begin=query+14,*end=strchr(begin,'"');
        if(!end||strcmp(end,"\" button."))return -1;
        size_t n=(size_t)(end-begin);
        for(unsigned i=0;i<v->count;i++){
            const WFNode *button=v->nodes+i;
            if(button->role!=WF_BUTTON||!(button->flags&WF_CLICKABLE))continue;
            const char *name=wf_text_get(v,button->name);
            if(name&&strlen(name)==n&&!memcmp(name,begin,n)){
                a->kind=WF_CLICK;a->target=button->ref;
                a->text=NULL;a->text_length=0;return 0;
            }
        }
        return -1;
    }
    unsigned hour=0,minute=0,year=0;
    if(strstr(query," as the date and hit submit.")){
        if(sscanf(query,"Enter %u/%u/%u as the date",&hour,&minute,&year)!=3||
           hour<1||hour>12||minute<1||minute>31||year<1||year>9999||cap<11)
            return -1;
        snprintf(scratch,cap,"%04u-%02u-%02u",year,hour,minute);
    }else if(strstr(query," as the time and press submit.")){
        char meridian[3]={0};
        if(sscanf(query,"Enter %u:%u %2s as the time",&hour,&minute,meridian)!=3||
           hour<1||hour>12||minute>59||cap<6)return -1;
        hour%=12;
        if(!strcmp(meridian,"PM"))hour+=12;
        else if(strcmp(meridian,"AM"))return -1;
        snprintf(scratch,cap,"%02u:%02u",hour,minute);
    }else return -1;
    const WFNode *input=NULL,*submit=NULL;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(n->ref==1&&n->role==WF_INPUT)input=n;
        if(n->ref==2&&n->role==WF_BUTTON)submit=n;
    }
    if(!input||!submit)return -1;
    const char *current=wf_text_get(v,input->value);
    if(!current)return -1;
    if(strcmp(current,scratch)){
        a->kind=WF_INSERT;a->target=1;
        a->text=scratch;a->text_length=strlen(scratch);
    }else{
        a->kind=WF_CLICK;a->target=2;
        a->text=NULL;a->text_length=0;
    }
    return 0;
}
#endif
