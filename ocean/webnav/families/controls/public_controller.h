#ifndef WEBNAV_CONTROLS_PUBLIC_CONTROLLER_H
#define WEBNAV_CONTROLS_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* phase is caller-owned and advances only after its action is applied. */
static const WFNode *controls_node(const WFView *v,unsigned ref) {
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static int controls_named_hex(const char *name,char hex[7]) {
    static const struct {const char *name,*hex;} map[]={
        {"black","000000"},{"white","FFFFFF"},{"aqua","00FFFF"},
        {"blue","0000FF"},{"gray","808080"},{"green","008000"},
        {"lime","00FF00"},{"maroon","800000"},{"navy","000080"},
        {"olive","808000"},{"purple","800080"},{"red","FF0000"},
        {"silver","C0C0C0"},{"teal","008080"},{"yellow","FFFF00"},
        {"pink","FFC0CB"},{"magenta","FF00FF"},{"gold","FFD700"},
        {"orange","FFA500"}
    };
    for(unsigned i=0;i<sizeof map/sizeof map[0];i++)if(!strcmp(name,map[i].name)){
        memcpy(hex,map[i].hex,7);return 0;
    }
    return -1;
}
static int controls_public_next(const WFView *v,unsigned task,unsigned phase,
                                char text[7],WFAction *a) {
    if(!v||!a||!text||task>=6||v->count>WF_MAX_NODES)return -1;
    const char *q=wf_text_get(v,v->instruction);if(!q)return -1;
    *a=(WFAction){0};
    if(task==0){
        char wanted[64];const char *begin=q+7,*end=strstr(q," from the list");
        if(strncmp(q,"Select ",7)||!end||end<=begin||(size_t)(end-begin)>=sizeof wanted)return -1;
        memcpy(wanted,begin,(size_t)(end-begin));wanted[end-begin]=0;
        unsigned selected=~0u,target=~0u;
        for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WF_OPTION){
            const char *name=wf_text_get(v,v->nodes[i].name);
            if(name&&!strcmp(name,wanted))target=v->nodes[i].ref-10;
            if(v->nodes[i].flags&WF_SELECTED)selected=v->nodes[i].ref-10;
        }
        if(target==~0u)return -1;
        *a=selected==target?(WFAction){.kind=WF_CLICK,.target=2}:
            (WFAction){.kind=WF_SELECT_OPTION,.target=1,.arg0=target};
        return 0;
    }
    if(task==1||task==2){
        int targets[3]={0},count=task==1?1:3;
        if(task==1){if(sscanf(q,"Select %d with the slider",targets)!=1)return -1;}
        else if(sscanf(q,"Set the sliders to the combination [%d,%d,%d]",
                       &targets[0],&targets[1],&targets[2])!=3)return -1;
        for(int i=0;i<count;i++){
            const WFNode *n=controls_node(v,(unsigned)i+1);if(!n)return -1;
            const char *name=wf_text_get(v,n->name),*value=wf_text_get(v,n->value);
            int lo,hi;char ori[32];
            if(!name||!value||sscanf(name,"Slider %*u (%d to %d, %31[^)])",&lo,&hi,ori)!=3||hi<=lo)return -1;
            if(atoi(value)!=targets[i]){
                unsigned fraction=(unsigned)((targets[i]-lo)*1000/(hi-lo));
                if(fraction>1000)return -1;
                if(!strcmp(ori,"vertical"))fraction=1000-fraction;
                *a=(WFAction){.kind=WF_CLICK,.target=(unsigned)i+1,.arg0=fraction};return 0;
            }
        }
        *a=(WFAction){.kind=WF_CLICK,.target=(unsigned)count+1};return 0;
    }
    if(task==3){
        int goal;if(sscanf(q,"Select %d with the spinner",&goal)!=1)return -1;
        const WFNode *n=controls_node(v,4);if(!n)return -1;
        const char *value=wf_text_get(v,n->value);if(!value)return -1;
        int current=atoi(value);
        *a=(WFAction){.kind=WF_CLICK,.target=current==goal?3:(current<goal?1:2)};
        return 0;
    }
    const WFNode *input=controls_node(v,1);if(!input)return -1;
    if(task==4){
        char name[64];if(sscanf(q,"Select %63s with the color picker",name)!=1||
                         controls_named_hex(name,text))return -1;
    }else{
        const WFNode *swatch=controls_node(v,3);if(!swatch)return -1;
        const char *value=wf_text_get(v,swatch->value);
        if(!value||value[0]!='#'||strlen(value)!=7)return -1;
        memcpy(text,value+1,6);text[6]=0;
    }
    if(phase==0)*a=(WFAction){.kind=WF_SELECT_ALL,.target=1};
    else if(phase==1)*a=(WFAction){.kind=WF_INSERT,.target=1,.text=text,.text_length=6};
    else *a=(WFAction){.kind=WF_CLICK,.target=2};
    return 0;
}
#endif
