#ifndef WEBNAV_SOCIAL_PUBLIC_CONTROLLER_H
#define WEBNAV_SOCIAL_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <stdlib.h>
#include <string.h>

/* Reads only WFView. It does not inspect row words, seed, or private target. */
static const WFNode *social_public_ref(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static int social_public_copy(char *out,size_t cap,const char *begin,const char *end){
    if(!out||!begin||!end||end<=begin||(size_t)(end-begin)>=cap)return -1;
    memcpy(out,begin,(size_t)(end-begin));out[end-begin]=0;return 0;
}
static int social_public_parse(const char *q,char *user,size_t user_cap,
                               unsigned *slot,unsigned *amount,int *single){
    if(!q||!user||!slot||!amount||!single)return -1;
    const char *left=strchr(q,'"');if(!left)return -1;
    const char *right=strchr(left+1,'"');if(!right)return -1;
    char name[32];if(social_public_copy(name,sizeof name,left+1,right))return -1;
    *single=!strncmp(q,"For the user ",13);
    if(!strcmp(name,"Reply"))*slot=0u;
    else if(!strcmp(name,"Retweet"))*slot=1u;
    else if(!strcmp(name,"Like"))*slot=2u;
    else if(!strcmp(name,"Share"))*slot=3u;
    else if(!strcmp(name,"Share via DM"))*slot=4u;
    else if(!strcmp(name,"Copy link to Tweet"))*slot=5u;
    else if(!strcmp(name,"Embed Tweet"))*slot=6u;
    else if(!strcmp(name,"Mute"))*slot=7u;
    else if(!strcmp(name,"Block"))*slot=8u;
    else if(!strcmp(name,"Report"))*slot=9u;
    else return -1;
    if(*single){
        const char *start=q+13;const char *end=strstr(start,", click on the");
        *amount=1u;return social_public_copy(user,user_cap,start,end);
    }
    const char *start=strstr(q," posts by ");
    if(start)start+=10;
    else if((start=strstr(q," post by ")))start+=9;
    else return -1;
    const char *end=strstr(start," and then click Submit.");
    if(social_public_copy(user,user_cap,start,end))return -1;
    if(strstr(q," all posts by ")){*amount=0u;return 0;}
    const char *n=strstr(q," button on ");if(!n)return -1;n+=11;
    char *tail;unsigned long value=strtoul(n,&tail,10);
    if(tail==n||value<1u||value>11u)return -1;
    *amount=(unsigned)value;return 0;
}
static int social_public_next(const WFView *v,char *scratch,size_t cap,WFAction *a){
    if(!v||!scratch||!cap||!a)return -1;
    const char *q=wf_text_get(v,v->instruction);if(!q)return -1;
    unsigned slot,amount;int single;
    if(social_public_parse(q,scratch,cap,&slot,&amount,&single))return -1;
    *a=(WFAction){.kind=WF_WAIT,.elapsed_ms=v->elapsed_ms+100u};
    if(single){
        for(unsigned post=1;post<=11;post++){
            const WFNode *identity=social_public_ref(v,post*16u+10u);
            if(!identity)continue;
            const char *user=wf_text_get(v,identity->value);
            if(!user||strcmp(user,scratch))continue;
            unsigned ref=post*16u+slot;
            if(slot>=4u&&!social_public_ref(v,ref))ref=post*16u+3u;
            if(!social_public_ref(v,ref))return -1;
            a->kind=WF_CLICK;a->target=ref;return 0;
        }
        return -1;
    }
    unsigned seen=0;
    for(unsigned post=1;post<=11;post++){
        const WFNode *identity=social_public_ref(v,post*16u+10u);
        if(!identity)continue;
        const char *user=wf_text_get(v,identity->value);
        if(!user)return -1;
        int same=!strcmp(user,scratch);
        int selected=same&&(!amount||seen<amount);
        for(unsigned control=0;control<4;control++){
            const WFNode *button=social_public_ref(v,post*16u+control);
            if(!button)return -1;
            int should=selected&&control==slot;
            int checked=!!(button->flags&WF_CHECKED);
            if(should!=checked){a->kind=WF_CLICK;a->target=button->ref;return 0;}
        }
        if(same)seen++;
    }
    if(!social_public_ref(v,1u)||(!amount&&seen==0u)||amount>seen)return -1;
    a->kind=WF_CLICK;a->target=1u;return 0;
}
#endif
