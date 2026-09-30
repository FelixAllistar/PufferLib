#ifndef WEBNAV_TRAVEL_PUBLIC_CONTROLLER_H
#define WEBNAV_TRAVEL_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const WFNode *travel_node(const WFView *v,unsigned ref){
    if(!v)return NULL;
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return &v->nodes[i];
    return NULL;
}
static const char *travel_name(const WFView *v,const WFNode *n){
    return n?wf_text_get(v,n->name):NULL;
}
static const char *travel_value(const WFView *v,const WFNode *n){
    return n?wf_text_get(v,n->value):NULL;
}
static int travel_query_span(const char *q,const char *before,
                             const char *after,const char **start,size_t *len){
    const char *a=strstr(q,before);if(!a)return -1;a+=strlen(before);
    const char *b=strstr(a,after);if(!b)return -1;
    *start=a;*len=(size_t)(b-a);return 0;
}
static int travel_find_airport(const WFView *v,const char *s,size_t len){
    for(unsigned i=1;i<=6;i++){
        const WFNode *n=travel_node(v,10+i);
        const char *name=travel_name(v,n);
        if(!name)continue;
        for(const char *p=name;*p;p++)if(!strncmp(p,s,len))return (int)i;
    }
    return -1;
}
static int travel_rank_action(const WFView *v,const char *q,
                              unsigned first,unsigned last,WFAction *a){
    int max=strstr(q,"longest")||strstr(q,"most expensive");
    int duration=strstr(q,"duration")||strstr(q,"shortest one-way");
    uint32_t best=max?0u:UINT32_MAX;unsigned ref=0;
    for(unsigned i=first;i<=last;i++){
        const WFNode *n=travel_node(v,i);
        if(!n||n->role!=WF_BUTTON||!(n->flags&WF_VISIBLE))continue;
        const char *name=travel_name(v,n),*value=travel_value(v,n);
        if(!name||!value)return -1;
        uint32_t score=0;
        if(duration){
            const char *ms=strstr(value,"data-duration=");
            if(!ms)return -1;score=(uint32_t)strtoul(ms+14,NULL,10);
        }else{
            const char *dollar=strchr(name,'$');if(!dollar)return -1;
            score=(uint32_t)strtoul(dollar+1,NULL,10);
        }
        if(!ref||(max?score>best:score<best)){best=score;ref=i;}
    }
    if(!ref)return -1;
    *a=(WFAction){.kind=WF_CLICK,.target=ref};return 0;
}
static int travel_public_action(const WFView *v,WFAction *a){
    if(!v||!a||v->count>WF_MAX_NODES)return -1;
    const char *q=wf_text_get(v,v->instruction);if(!q)return -1;
    if(!strncmp(q,"Buy the ticket with the ",24))
        return travel_rank_action(v,q,1,4,a);
    if(strncmp(q,"Book the ",9))return -1;
    const WFNode *back=travel_node(v,5);
    if(back&&(back->flags&WF_VISIBLE)){
        unsigned last=5;for(unsigned i=6;i<=9;i++){
            const WFNode *n=travel_node(v,i);
            if(n&&(n->flags&WF_VISIBLE))last=i;
        }
        return travel_rank_action(v,q,6,last,a);
    }
    const char *origin,*destination,*date;size_t n_origin,n_destination,n_date;
    if(travel_query_span(q,"from: "," to: ",&origin,&n_origin)||
       travel_query_span(q," to: "," on ",&destination,&n_destination)||
       travel_query_span(q," on ",".",&date,&n_date))return -1;
    int from=travel_find_airport(v,origin,n_origin);
    int to=travel_find_airport(v,destination,n_destination);
    if(from<1||to<1)return -1;
    const char *from_name=travel_name(v,travel_node(v,10+(unsigned)from));
    const char *to_name=travel_name(v,travel_node(v,10+(unsigned)to));
    const char *current=travel_value(v,travel_node(v,1));
    if(!current||strcmp(current,from_name)){
        *a=(WFAction){.kind=WF_SELECT_OPTION,.target=1,.arg0=(uint32_t)from};
        return 0;
    }
    current=travel_value(v,travel_node(v,2));
    if(!current||strcmp(current,to_name)){
        *a=(WFAction){.kind=WF_SELECT_OPTION,.target=2,.arg0=(uint32_t)to};
        return 0;
    }
    current=travel_value(v,travel_node(v,3));
    if(!current||strlen(current)!=n_date||memcmp(current,date,n_date)){
        for(unsigned day=1;day<=92;day++){
            const WFNode *option=travel_node(v,20+day);
            const char *name=travel_name(v,option);
            if(name&&strlen(name)==n_date&&!memcmp(name,date,n_date)){
                *a=(WFAction){.kind=WF_SELECT_OPTION,.target=3,.arg0=day};
                return 0;
            }
        }
        return -1;
    }
    *a=(WFAction){.kind=WF_CLICK,.target=4};return 0;
}
#endif
