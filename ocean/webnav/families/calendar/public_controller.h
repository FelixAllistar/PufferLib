#ifndef WEBNAV_CALENDAR_PUBLIC_CONTROLLER_H
#define WEBNAV_CALENDAR_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

/* Stateless datepicker decisions, plus a local drag phase for the daily task.
   Every goal and control comes from WFView. No row, seed or private field is read. */
static int calendar_public_action(const WFView *v,WFAction *a){
    const char *q=wf_text_get(v,v->instruction);
    if(!q)return -1;
    if(!strncmp(q,"Select ",7)){
        unsigned month=0,day=0;
        if(sscanf(q,"Select %u/%u/2016",&month,&day)!=2||month<1||month>12||day<1||day>31)return -1;
        const WFNode *input=NULL,*prev=NULL,*next=NULL,*title=NULL,*chosen=NULL;
        for(unsigned i=0;i<v->count;i++){
            const WFNode *n=v->nodes+i;
            if(n->ref==1)input=n;else if(n->ref==3)prev=n;
            else if(n->ref==4)next=n;else if(n->ref==36)title=n;
            else if(n->ref==day+4)chosen=n;
        }
        if(!input)return -1;
        char wanted[32];snprintf(wanted,sizeof wanted,"%02u/%02u/2016",month,day);
        const char *current=wf_text_get(v,input->value);
        if(current&&!strcmp(current,wanted)){a->kind=WF_CLICK;a->target=2;return 0;}
        if(!title){a->kind=WF_CLICK;a->target=1;return 0;}
        static const char *const names[]={"January","February","March","April","May","June",
          "July","August","September","October","November","December"};
        unsigned shown=0;const char *caption=wf_text_get(v,title->name);
        for(unsigned i=0;i<12;i++)if(caption&&!strncmp(caption,names[i],strlen(names[i])))shown=i+1;
        if(!shown)return -1;
        a->kind=WF_CLICK;
        if(shown>month){if(!prev||!(prev->flags&WF_ENABLED))return -1;a->target=3;}
        else if(shown<month){if(!next||!(next->flags&WF_ENABLED))return -1;a->target=4;}
        else {if(!chosen)return -1;a->target=day+4;}
        return 0;
    }
    if(!strstr(q," event named \""))return -1;
    static unsigned phase,start,duration;
    static char name[32];
    if(v->elapsed_ms==0){
        phase=0;start=strstr(q,"8AM and 12PM")?16u:strstr(q,"12PM and 4PM")?24u:32u;
        duration=strstr(q,"0.5 hours")||strstr(q,"30 mins")?1u:
                 strstr(q,"1.5 hours")||strstr(q,"90 mins")?3u:2u;
        const char *p=strstr(q," event named \"");p+=14;
        const char *end=strchr(p,'"');if(!end||end-p>31)return -1;
        memcpy(name,p,(size_t)(end-p));name[end-p]=0;
    }
    a->kind=WF_WAIT;a->target=0;
    switch(phase++){
        case 0:a->kind=WF_SCROLL;a->target=56;a->arg0=start;break;
        case 1:{
            const WFNode *area=NULL;for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==56)area=v->nodes+i;
            if(!area)return -1;
            unsigned top=(unsigned)(area->scroll_y/20),limit=start+8;
            int found=0;
            for(unsigned candidate=start;candidate+duration<=limit;candidate++){
                int blocked=0;
                for(unsigned i=0;i<v->count;i++){
                    const WFNode *n=v->nodes+i;
                    if(n->ref<49||n->ref>51)continue;
                    int event_start=(int)top+(int)((n->y-4)/20);
                    int event_end=event_start+(int)(n->height/20);
                    blocked|=(int)candidate<event_end&&(int)(candidate+duration)>event_start;
                }
                if(!blocked){start=candidate;found=1;break;}
            }
            if(!found)return -1;
            a->kind=WF_POINTER_DOWN;a->target=start+1;break;
        }
        case 2:a->kind=WF_POINTER_MOVE;a->target=start+duration;break;
        case 3:a->kind=WF_POINTER_UP;a->target=52;break;
        case 4:a->kind=WF_INSERT;a->target=53;a->text=name;a->text_length=strlen(name);break;
        case 5:a->kind=WF_CLICK;a->target=55;break;
        default:return -1;
    }
    return 0;
}
#endif
