#ifndef WEBNAV_EDITING_PUBLIC_CONTROLLER_H
#define WEBNAV_EDITING_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const WFNode *editing_ref(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static int editing_copy(char *out,size_t cap,const char *a,const char *b){
    if(!a||!b||b<a||(size_t)(b-a)>=cap)return -1;
    memcpy(out,a,(size_t)(b-a));out[b-a]=0;return 0;
}
static int editing_find(const WFView *v,const char *query,
                        char *scratch,size_t cap,WFAction *a){
    unsigned position=0u;
    if(sscanf(query,"Find the %u",&position)!=1||position<1u)return -1;
    const WFNode *paragraph=editing_ref(v,3u),*input=editing_ref(v,1u);
    if(!paragraph||!input)return -1;
    const char *text=wf_text_get(v,paragraph->value),
               *value=wf_text_get(v,input->value);
    if(!text||!value)return -1;
    const char *p=text,*begin=NULL,*end=NULL;
    for(unsigned i=0;i<position;i++){
        while(*p&&isspace((unsigned char)*p))p++;
        if(!*p)return -1;
        begin=p;while(*p&&!isspace((unsigned char)*p))p++;end=p;
    }
    unsigned n=0u;
    for(const char *x=begin;x<end;x++)
        if(isalpha((unsigned char)*x)){
            if(n+1u>=cap)return -1;scratch[n++]=*x;
        }
    scratch[n]=0;if(!n)return -1;
    if(!strcmp(value,scratch)){
        if(!editing_ref(v,2u))return -1;
        a->kind=WF_CLICK;a->target=2u;return 0;
    }
    if(!(input->flags&WF_FOCUSED)){
        a->kind=WF_CLICK;a->target=1u;return 0;
    }
    if(*value)return -1;
    a->kind=WF_INSERT;a->text=scratch;a->text_length=n;return 0;
}
static int editing_highlight(const WFView *v,const char *query,
                             WFAction *a){
    unsigned target=0u;
    if(strstr(query,"2nd paragraph"))target=1u;
    else if(strstr(query,"3rd paragraph"))target=2u;
    unsigned count=editing_ref(v,12u)?3u:1u,offset=0u;
    int exact=1;
    for(unsigned i=0;i<count;i++){
        const WFNode *n=editing_ref(v,10u+i);if(!n)return -1;
        const char *value=wf_text_get(v,n->value);if(!value)return -1;
        unsigned len=(unsigned)strlen(value);
        if(i==target){
            if(n->selection_start!=0u||n->selection_end!=len)exact=0;
        }else if(n->selection_start!=n->selection_end)exact=0;
        if(i<target)offset+=len+1u;
    }
    if(exact){
        if(!editing_ref(v,1u))return -1;
        a->kind=WF_CLICK;a->target=1u;return 0;
    }
    const WFNode *chosen=editing_ref(v,10u+target);
    const char *value=chosen?wf_text_get(v,chosen->value):NULL;
    if(!value)return -1;
    a->kind=WF_SELECT_RANGE;a->arg0=offset;
    a->arg1=offset+(unsigned)strlen(value);return 0;
}
static int editing_editor(const WFView *v,const char *query,
                          char *scratch,size_t cap,WFAction *a){
    const WFNode *editor=editing_ref(v,10u);if(!editor)return -1;
    const char *body=wf_text_get(v,editor->value);if(!body)return -1;
    unsigned start=0u,end=(unsigned)strlen(body);
    if(!strstr(query,"everything")){
        const char *p=strstr(query,"give the text ");
        if(!p)return -1;p+=14;
        const char *q=strstr(p," the style ");
        if(!q)q=strstr(p," the color ");
        if(!q||editing_copy(scratch,cap,p,q))return -1;
        const char *found=strstr(body,scratch);if(!found)return -1;
        start=(unsigned)(found-body);end=start+(unsigned)strlen(scratch);
    }
    if(editor->selection_start!=start||editor->selection_end!=end){
        a->kind=WF_SELECT_RANGE;a->arg0=start;a->arg1=end;return 0;
    }
    unsigned ref=0u,code=0u;
    if(strstr(query," the color ")||strstr(query,"everything the color ")){
        static const char *const colors[]={"red","orange","yellow",
            "green","blue","purple"};
        unsigned family=0u;for(;family<6u;family++)
            if(strstr(query,colors[family]))break;
        if(family==6u)return -1;
        ref=20u+5u*family;code=(family*5u+1u)<<3u;
    }else if(strstr(query,"bold")){ref=2u;code=1u;}
    else if(strstr(query,"italics")){ref=3u;code=2u;}
    else if(strstr(query,"underlined")){ref=4u;code=4u;}
    else return -1;
    int styled=1;
    for(unsigned i=0;i<strlen(body);i++){
        const WFNode *n=editing_ref(v,100u+i);
        const char *style=n?wf_text_get(v,n->value):NULL;
        if(!style)return -1;
        unsigned value=(unsigned)strtoul(style,NULL,10);
        if(i>=start&&i<end){
            if(ref>=20u){
                if((value&7u)||!value||
                   ((value>>3u)-1u)/5u!=(ref-20u)/5u)styled=0;
            }else if(value!=code)styled=0;
        }else if(value)styled=0;
    }
    if(styled){
        if(!editing_ref(v,1u))return -1;
        a->kind=WF_CLICK;a->target=1u;return 0;
    }
    if(ref>=20u&&!editing_ref(v,ref)){
        if(!editing_ref(v,5u))return -1;
        a->kind=WF_CLICK;a->target=5u;return 0;
    }
    a->kind=WF_CLICK;a->target=ref;return 0;
}
static int editing_terminal(const WFView *v,const char *query,
                            char *scratch,size_t cap,WFAction *a){
    const WFNode *panel=editing_ref(v,1u),
                 *command=editing_ref(v,2u),*out=editing_ref(v,3u);
    if(!panel||!command||!out)return -1;
    const char *typed=wf_text_get(v,command->value),
               *output=wf_text_get(v,out->value);
    if(!typed||!output)return -1;
    if(!(panel->flags&WF_FOCUSED)){
        a->kind=WF_CLICK;a->target=1u;return 0;
    }
    if(*typed){a->kind=WF_ENTER;return 0;}
    if(!*output){
        a->kind=WF_INSERT;a->text="ls";a->text_length=2u;return 0;
    }
    const char *p=output;
    while(*p){
        while(*p==' ')p++;
        if(!*p)break;
        const char *end=strchr(p,' ');if(!end)end=p+strlen(p);
        char name[128];if(editing_copy(name,sizeof name,p,end))return -1;
        const char *wanted=strstr(query,"extension .");
        const char *dot=strchr(name,'.');
        const char *extension=wanted?wanted+11:NULL;
        size_t nl=strlen(name),el=extension?strlen(extension):0u;
        int match=wanted?nl>el&&name[nl-el-1u]=='.'&&
            !strcmp(name+nl-el,extension):dot==NULL;
        if(match){
            int n=snprintf(scratch,cap,"rm %s",name);
            if(n<0||(size_t)n>=cap)return -1;
            a->kind=WF_INSERT;a->text=scratch;a->text_length=(size_t)n;
            return 0;
        }
        p=end;
    }
    return -1;
}
static int editing_public_next(const WFView *v,char *scratch,size_t cap,
                               WFAction *a){
    if(!v||!scratch||!cap||!a)return -1;
    const char *query=wf_text_get(v,v->instruction);if(!query)return -1;
    *a=(WFAction){.kind=WF_WAIT,.elapsed_ms=v->elapsed_ms+100u};
    if(!strncmp(query,"Find the ",9u))
        return editing_find(v,query,scratch,cap,a);
    if(!strncmp(query,"Highlight the text",18u))
        return editing_highlight(v,query,a);
    if(!strncmp(query,"Using the text editor",21u))
        return editing_editor(v,query,scratch,cap,a);
    return editing_terminal(v,query,scratch,cap,a);
}
#endif
