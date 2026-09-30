#ifndef WEBNAV_EMAIL_PUBLIC_CONTROLLER_H
#define WEBNAV_EMAIL_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* A bounded controller for the family's generated, canonical instructions.
 * It reads only WFView and keeps no access to the private row or seed. */
static const WFNode *email_public_ref(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static int email_public_source(const char *q,char *dst,size_t cap){
    const char *begin=NULL,*end=NULL;
    if((begin=strstr(q,"Find the email by "))){begin+=18;end=strstr(begin," and ");}
    else if((begin=strstr(q,"Reply to "))){begin+=9;end=strstr(begin,"'s email");}
    else if((begin=strstr(q,"Forward the email from "))){begin+=23;end=strstr(begin," to ");}
    else if((begin=strstr(q,"Delete the email from "))){begin+=22;end=strchr(begin,'.');}
    else if((begin=strstr(q,"Mark the email from "))){begin+=20;end=strstr(begin," as important");}
    if(!begin||!end||end<=begin||(size_t)(end-begin)>=cap)return -1;
    memcpy(dst,begin,(size_t)(end-begin));dst[end-begin]=0;return 0;
}
static unsigned email_public_goal(const char *q){
    if(strstr(q,"trash icon")||strstr(q,"Delete the email"))return 2u;
    if(strstr(q,"star icon")||strstr(q,"Mark the email"))return 3u;
    if(strstr(q,"reply")||strstr(q,"Reply to"))return 0u;
    return 1u;
}
static int email_public_copy(char *dst,size_t cap,const char *begin,const char *end){
    if(!begin||!end||end<=begin||(size_t)(end-begin)>=cap)return -1;
    memcpy(dst,begin,(size_t)(end-begin));dst[end-begin]=0;return 0;
}
static int email_public_reply(const char *q,char *dst,size_t cap){
    const char *begin=strchr(q,'"');if(!begin)return -1;
    return email_public_copy(dst,cap,begin+1,strchr(begin+1,'"'));
}
static int email_public_recipient(const char *q,char *dst,size_t cap){
    const char *p=strstr(q," to ");if(!p)return -1;p+=4;
    return email_public_copy(dst,cap,p,strchr(p,'.'));
}
static int email_public_next(const WFView *v,char *scratch,size_t cap,WFAction *a){
    if(!v||!scratch||!cap||!a)return -1;
    const char *q=wf_text_get(v,v->instruction);if(!q)return -1;
    *a=(WFAction){.kind=WF_WAIT,.elapsed_ms=v->elapsed_ms+100u};
    unsigned goal=email_public_goal(q),target=0;
    char wanted[64];if(email_public_source(q,wanted,sizeof wanted))return -1;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(n->ref<10u||n->ref>=106u||(n->ref-10u)%8u!=0u)continue;
        const char *name=wf_text_get(v,n->name);
        if(name&&!strcmp(wanted,name)){target=n->ref;break;}
    }
    if(email_public_ref(v,1u)){
        if(!target)return -1;
        a->kind=WF_CLICK;a->target=target+(goal==2u?2u:(goal==3u?1u:0u));return 0;
    }
    if(email_public_ref(v,112u)){
        if(goal==2u||goal==3u){
            for(unsigned i=0;i<v->count;i++){
                const WFNode *n=v->nodes+i;
                if(n->ref>=10u&&n->ref<106u&&(n->ref-10u)%8u==(goal==2u?7u:6u)){
                    a->kind=WF_CLICK;a->target=n->ref;return 0;
                }
            }
            return -1;
        }
        a->kind=WF_CLICK;a->target=goal==0u?113u:114u;return 0;
    }
    if(email_public_ref(v,115u)){
        const WFNode *field=email_public_ref(v,117u);if(!field)return -1;
        const char *current=wf_text_get(v,field->value);
        if(!current||!*current){
            if(!(field->flags&WF_FOCUSED)){a->kind=WF_CLICK;a->target=117u;return 0;}
            if(email_public_reply(q,scratch,cap))return -1;
            a->kind=WF_INSERT;a->text=scratch;a->text_length=strlen(scratch);return 0;
        }
        a->kind=WF_CLICK;a->target=116u;return 0;
    }
    if(email_public_ref(v,118u)){
        const WFNode *field=email_public_ref(v,120u);if(!field)return -1;
        const char *current=wf_text_get(v,field->value);
        if(!current||!*current){
            if(!(field->flags&WF_FOCUSED)){a->kind=WF_CLICK;a->target=120u;return 0;}
            if(email_public_recipient(q,scratch,cap))return -1;
            a->kind=WF_INSERT;a->text=scratch;a->text_length=strlen(scratch);return 0;
        }
        a->kind=WF_CLICK;a->target=119u;return 0;
    }
    return -1;
}
#endif
