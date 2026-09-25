#ifndef WEBNAV_NUMERIC_PUBLIC_CONTROLLER_H
#define WEBNAV_NUMERIC_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include "public_hotcold.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
/* Scripted single-lane oracle: memory contains only previously visible data. */
static int numeric_public_known[WF_MAX_NODES], numeric_public_values[WF_MAX_NODES];
static int numeric_public_low, numeric_public_high;
static char numeric_public_payload[32];
static const char *numeric_public_name(const WFView *v,const WFNode *n) {return wf_text_get(v,n->name);}
static const char *numeric_public_value(const WFView *v,const WFNode *n) {return wf_text_get(v,n->value);}
static const WFNode *numeric_public_find(const WFView *v,unsigned role,const char *name) {
    for(unsigned i=0;i<v->count;i++) if(v->nodes[i].role==role && !strcmp(numeric_public_name(v,&v->nodes[i]),name)) return &v->nodes[i];
    return NULL;
}
static int numeric_public_click(WFAction *a,const WFNode *n) {
    if(!n)return -1;
    *a=(WFAction){.kind=WF_CLICK,.target=n->ref};return 0;
}
static int numeric_public_fill(const WFView *v,WFAction *a,int answer) {
    const WFNode *input=NULL;
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WF_INPUT)input=&v->nodes[i];
    if(!input)return -1;
    snprintf(numeric_public_payload,sizeof numeric_public_payload,"%d",answer);
    const char *old=numeric_public_value(v,input);
    if(!strcmp(old,numeric_public_payload))return numeric_public_click(a,numeric_public_find(v,WF_BUTTON,"Submit"));
    if(*old && (input->selection_start || input->selection_end!=strlen(old))) {
        *a=(WFAction){.kind=WF_SELECT_ALL,.target=input->ref};return 0;
    }
    *a=(WFAction){.kind=WF_INSERT,.target=input->ref,.text=numeric_public_payload,.text_length=strlen(numeric_public_payload)};return 0;
}
static int numeric_public_action(const WFView *v,WFAction *a) {
    if(!v || v->count>WF_MAX_NODES)return -1;
    const char *q=wf_text_get(v,v->instruction);if(!q)return -1;
    if(!v->elapsed_ms){memset(numeric_public_known,0,sizeof numeric_public_known);numeric_public_low=0;numeric_public_high=9;}
    if(strstr(q,"HOT area"))return numeric_hotcold_public_action(v,a);
    if(strstr(q,"ascending order")) {
        const WFNode *best=NULL;int value=INT_MAX;
        for(unsigned i=0;i<v->count;i++) {
            const WFNode *n=&v->nodes[i];if(n->ref>=WF_MAX_NODES)return -1;
            const char *s=numeric_public_value(v,n);
            if(*s){numeric_public_known[n->ref]=1;numeric_public_values[n->ref]=atoi(s);}
            if(numeric_public_known[n->ref]&&numeric_public_values[n->ref]<value){best=n;value=numeric_public_values[n->ref];}
        }
        return numeric_public_click(a,best);
    }
    if(strstr(q,"greatest number")) {
        const WFNode *best=NULL,*unseen=NULL;int value=INT_MIN;
        for(unsigned i=0;i<v->count;i++) {
            const WFNode *n=&v->nodes[i];if(strncmp(numeric_public_name(v,n),"Card",4))continue;
            if(n->ref>=WF_MAX_NODES)return -1;
            const char *s=numeric_public_value(v,n);
            if(*s){numeric_public_known[n->ref]=1;numeric_public_values[n->ref]=atoi(s);}
            if(!numeric_public_known[n->ref])unseen=n;
            else if(numeric_public_values[n->ref]>value){best=n;value=numeric_public_values[n->ref];}
        }
        if(unseen)return numeric_public_click(a,unseen);
        if(best && (best->flags&WF_SELECTED))return numeric_public_click(a,numeric_public_find(v,WF_BUTTON,"Submit"));
        return numeric_public_click(a,best);
    }
    if(strstr(q,"Generate a")) {
        const WFNode *n=numeric_public_find(v,WF_TEXT,"Generated number");if(!n)return -1;
        const char *s=numeric_public_value(v,n);int x=atoi(s),ok=0;
        if(strcmp(s,"-")) {
            const char *p=strstr(q,"less than ");if(p)ok=x<atoi(p+10);
            else if((p=strstr(q,"greater than ")))ok=x>atoi(p+13);
            else if(strstr(q,"odd"))ok=x%2!=0;
            else if(strstr(q,"even"))ok=x%2==0;
        }
        return numeric_public_click(a,numeric_public_find(v,WF_BUTTON,ok?"Submit":"Generate"));
    }
    if(strstr(q,"Guess the number")) {
        const WFNode *n=numeric_public_find(v,WF_TEXT,"Feedback");if(!n)return -1;
        const char *s=numeric_public_value(v,n),*p=strstr(s,"higher than ");
        if(p && atoi(p+12)+1>numeric_public_low)numeric_public_low=atoi(p+12)+1;
        p=strstr(s,"lower than ");if(p && atoi(p+11)-1<numeric_public_high)numeric_public_high=atoi(p+11)-1;
        return numeric_public_fill(v,a,(numeric_public_low+numeric_public_high)/2);
    }
    if(strstr(q,"Draw the number")) {
        /* Fixed glyphs from the original public example images, not episode goals. */
        static const unsigned masks[]={110729622,71582820,252856726,110651542,71628113,110653727,110719382,35932319,110717334,110684566};
        const char *p=strchr(q,'"');if(!p||p[1]<'0'||p[1]>'9')return -1;
        unsigned mask=masks[p[1]-'0'];
        for(unsigned i=0;i<v->count;i++) {
            const WFNode *n=&v->nodes[i];if(n->role!=WF_CHECKBOX)continue;
            unsigned row,col;if(sscanf(numeric_public_name(v,n),"Row %u col %u",&row,&col)!=2||row<1||row>7||col<1||col>4)return -1;
            if(!!(n->flags&WF_CHECKED)!=((mask>>((row-1)*4+col-1))&1))return numeric_public_click(a,n);
        }
        return numeric_public_click(a,numeric_public_find(v,WF_BUTTON,"Submit"));
    }
    if(strstr(q,"odd or even")) {
        for(unsigned i=0;i+1<v->count;i++) {
            int x;if(sscanf(numeric_public_name(v,&v->nodes[i]),"Odd %d",&x)!=1)continue;
            const WFNode *n=&v->nodes[i+(x%2==0)];
            if(!(n->flags&WF_SELECTED))return numeric_public_click(a,n);
        }
        return numeric_public_click(a,numeric_public_find(v,WF_BUTTON,"Submit"));
    }
    const WFNode *problem=numeric_public_find(v,WF_TEXT,"Problem");
    if(problem) {
        const char *s=numeric_public_value(v,problem);int x,y,answer;char op;
        if(sscanf(s,"x %c %d = %d",&op,&x,&y)==3)answer=op=='+'?y-x:y+x;
        else if(sscanf(s,"%d %c x = %d",&x,&op,&y)==3)answer=op=='+'?y-x:x-y;
        else if(sscanf(s,"%d %c %d",&x,&op,&y)==3)answer=op=='+'?x+y:op=='-'?x-y:x*y;
        else return -1;
        return numeric_public_fill(v,a,answer);
    }
    return -1;
}
#endif
