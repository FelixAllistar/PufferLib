#ifndef WEBNAV_SCROLL_PUBLIC_CONTROLLER_H
#define WEBNAV_SCROLL_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <string.h>

/* One episode per caller. Decisions inspect WFView only. */
static int scroll_public_action(const WFView *v,WFAction *a){
    const char *query=wf_text_get(v,v->instruction);
    if(!query||!a)return -1;
    const WFNode *area=NULL,*input=NULL;
    for(unsigned i=0;i<v->count;i++){
        if(v->nodes[i].ref==1)area=v->nodes+i;
        if(v->nodes[i].ref==2&&v->nodes[i].role==WF_INPUT)input=v->nodes+i;
    }
    if(!area)return -1;
    if(!strncmp(query,"Select ",7)){
        const char *end=strstr(query," from the scroll list");if(!end)return -1;
        const char *start=query+7;size_t len=(size_t)(end-start);
        if(len>=128)return -1;
        char names[128];memcpy(names,start,len);names[len]=0;
        char *second=strstr(names,", ");if(second){*second=0;second+=2;}
        const char *wanted[2]={names,second};
        for(unsigned k=0;k<(second?2u:1u);k++){
            const WFNode *choice=NULL;
            for(unsigned i=0;i<v->count;i++){
                const WFNode *n=v->nodes+i;
                if(n->role!=WF_OPTION)continue;
                const char *name=wf_text_get(v,n->name);
                if(name&&!strcmp(name,wanted[k])&&(n->flags&WF_SELECTED)){choice=NULL;break;}
                if(name&&!strcmp(name,wanted[k])&&!choice)choice=n;
            }
            if(!choice)continue;
            if(!(choice->flags&WF_VISIBLE)){
                float top=area->scroll_y+choice->y-5;
                a->kind=WF_SCROLL;a->target=1;
                a->arg0=top>0?(unsigned)top:0;return 0;
            }
            a->kind=WF_SELECT_OPTION;a->target=choice->ref;a->arg0=1;return 0;
        }
        a->kind=WF_CLICK;a->target=16;return 0;
    }
    if(!strncmp(query,"Find the last word",18)){
        if(!input)return -1;
        const char *content=wf_text_get(v,area->value);
        const char *value=wf_text_get(v,input->value);
        if(!content||!value)return -1;
        size_t n=strlen(content);while(n&&content[n-1]=='.')n--;
        size_t start=n;while(start&&content[start-1]!=' '&&content[start-1]!='\n')start--;
        static char answer[64];if(n-start>=sizeof answer)return -1;
        memcpy(answer,content+start,n-start);answer[n-start]=0;
        if(strcmp(value,answer)){
            a->kind=WF_INSERT;a->target=2;a->text=answer;
            a->text_length=n-start;return 0;
        }
        a->kind=WF_CLICK;a->target=3;return 0;
    }
    if(!strncmp(query,"Scroll the textarea",19)){
        int bottom=strstr(query,"bottom")!=NULL;
        if((bottom&&area->scroll_max_y-area->scroll_y>=10)||
           (!bottom&&area->scroll_y>=10)){
            a->kind=WF_SCROLL;a->target=1;
            a->arg0=bottom?(unsigned)area->scroll_max_y:0;return 0;
        }
        a->kind=WF_CLICK;a->target=2;return 0;
    }
    if(!strcmp(query,"Click the cancel button.")){
        a->kind=WF_CLICK;a->target=3;return 0;
    }
    if(!strncmp(query,"Scroll to the bottom",20)){
        if(!input)return -1;
        if(!(input->flags&WF_ENABLED)){
            a->kind=WF_SCROLL;a->target=1;
            a->arg0=(unsigned)area->scroll_max_y;return 0;
        }
        const char *quote=strchr(query,'"');if(!quote)return -1;quote++;
        const char *last=strchr(quote,'"');if(!last||last-quote>=64)return -1;
        static char name[64];memcpy(name,quote,(size_t)(last-quote));
        name[last-quote]=0;
        const char *value=wf_text_get(v,input->value);if(!value)return -1;
        if(strcmp(value,name)){
            a->kind=WF_INSERT;a->target=2;a->text=name;
            a->text_length=(size_t)(last-quote);return 0;
        }
        a->kind=WF_CLICK;a->target=strstr(last,"press \"Agree\"")?4:3;return 0;
    }
    return -1;
}
#endif
