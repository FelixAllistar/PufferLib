#ifndef WEBNAV_MARKET_PUBLIC_CONTROLLER_H
#define WEBNAV_MARKET_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const WFNode *market_public_ref(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static int market_public_cents(const char *s,uint32_t *out){
    if(!s||!out||*s!='$')return -1;
    char *after;unsigned long dollars=strtoul(s+1,&after,10);
    if(after==s+1||*after!='.'||!isdigit((unsigned char)after[1])||
       !isdigit((unsigned char)after[2])||dollars>100000u)return -1;
    *out=(uint32_t)(dollars*100u+
        (unsigned)(after[1]-'0')*10u+(unsigned)(after[2]-'0'));
    return 0;
}
static int market_public_next(const WFView *v,WFAction *a){
    if(!v||!a)return -1;
    const char *q=wf_text_get(v,v->instruction);
    const WFNode *buy=market_public_ref(v,1u),*price=market_public_ref(v,2u);
    if(!q||!buy||!price||!(buy->flags&WF_CLICKABLE))return -1;
    const char *threshold_text=strchr(q,'$');uint32_t threshold=0,current=0;
    if(market_public_cents(threshold_text,&threshold))return -1;
    const char *current_text=wf_text_get(v,price->value);
    if(current_text&&*current_text){
        if(market_public_cents(current_text,&current))return -1;
        if(current<=threshold){
            *a=(WFAction){.kind=WF_CLICK,.target=1u,
                .elapsed_ms=v->elapsed_ms};
            return 0;
        }
    }
    uint32_t next=(v->elapsed_ms/100u+1u)*100u;
    if(next>=v->deadline_ms)return -1;
    *a=(WFAction){.kind=WF_WAIT,.elapsed_ms=next};return 0;
}
#endif
