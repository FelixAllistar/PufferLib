#ifndef WEBNAV_VISUAL_PUBLIC_CONTROLLER_H
#define WEBNAV_VISUAL_PUBLIC_CONTROLLER_H

#include "../common/family_api.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char visual_public_text[8];

static const char *visual_public_name(const WFView *v,const WFNode *n){
    return wf_text_get(v,n->name);
}
static const char *visual_public_value(const WFView *v,const WFNode *n){
    return wf_text_get(v,n->value);
}
static int visual_public_click(WFAction *a,uint32_t ref){
    *a=(WFAction){.kind=WF_CLICK,.target=ref};return 0;
}
static const WFNode *visual_public_find(const WFView *v,const char *name){
    for(unsigned i=0;i<v->count;i++){
        const char *s=visual_public_name(v,&v->nodes[i]);
        if(s&&(v->nodes[i].flags&WF_VISIBLE)&&!strcmp(s,name))return &v->nodes[i];
    }
    return NULL;
}
static int visual_public_color(const WFView *v,const char *query,WFAction *a){
    const WFNode *swatch=visual_public_find(v,"Query swatch");
    const char *wanted=swatch?visual_public_value(v,swatch):NULL;
    char parsed[32];
    if(!wanted){
        if(sscanf(query,"Click on the %31s colored box.",parsed)!=1)return -1;
        wanted=parsed;
    }
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=&v->nodes[i];
        const char *name=visual_public_name(v,n),*value=visual_public_value(v,n);
        if(name&&value&&!strcmp(name,"Color box")&&!strcmp(value,wanted))
            return visual_public_click(a,n->ref);
    }
    return -1;
}
static int visual_public_pie(const WFView *v,const char *query,WFAction *a){
    const WFNode *spreader=visual_public_find(v,"Pie spreader");
    if(!spreader)return -1;
    const char *state=visual_public_value(v,spreader);
    if(state&&!strcmp(state,"closed"))return visual_public_click(a,spreader->ref);
    const char *start=strchr(query,'"');
    if(!start||!start[1]||start[2]!='"')return -1;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=&v->nodes[i];
        const char *name=visual_public_name(v,n);
        if((n->flags&WF_VISIBLE)&&name&&name[0]==start[1]&&!name[1])
            return visual_public_click(a,n->ref);
    }
    *a=(WFAction){.kind=WF_WAIT};return 0;
}
static int visual_public_shades(const WFView *v,const char *query,WFAction *a){
    const char *color=strstr(query,"shades of ");
    if(!color)return -1;color+=10;
    unsigned hue=!strncmp(color,"red",3)?0u:
        !strncmp(color,"green",5)?120u:
        !strncmp(color,"blue",4)?240u:360u;
    if(hue==360u)return -1;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=&v->nodes[i];
        const char *name=visual_public_name(v,n),*value=visual_public_value(v,n);
        if(!name||strcmp(name,"Shade")||!value)continue;
        unsigned actual=360u;
        if(sscanf(value,"hsl(%u,",&actual)!=1)return -1;
        if(((actual==hue)?1:0)!=((n->flags&WF_SELECTED)?1:0))
            return visual_public_click(a,n->ref);
    }
    const WFNode *submit=visual_public_find(v,"Submit");
    return submit?visual_public_click(a,submit->ref):-1;
}
static int visual_public_descriptor(const char *query,char *size,
                                    char *color,char *token){
    char phrase[96];size_t len;
    if(!strncmp(query,"Click on a ",11)){
        snprintf(phrase,sizeof phrase,"%s",query+11);
    }else if(!strncmp(query,"How many ",9)){
        const char *end=strstr(query," are there?");
        if(!end)return -1;
        len=(size_t)(end-(query+9));
        if(!len||len>=sizeof phrase)return -1;
        memcpy(phrase,query+9,len);phrase[len]=0;
        if(phrase[len-1]=='s')phrase[len-1]=0;
    }else return -1;
    *size=*color=*token=0;
    static const char *const palette[]={"red","green","blue","aqua",
        "black","magenta","yellow"};
    char *word=strtok(phrase," ");
    while(word){
        if(!strcmp(word,"small"))strcpy(size,"small");
        else if(!strcmp(word,"large"))strcpy(size,"large");
        else {
            int found=0;
            for(unsigned i=0;i<7u;i++)if(!strcmp(word,palette[i])){
                strcpy(color,word);found=1;break;
            }
            if(!found)strcpy(token,word);
        }
        word=strtok(NULL," ");
    }
    return *token?0:-1;
}
static int visual_public_shape_matches(const WFView *v,const WFNode *n,
                                       const char *size,const char *color,
                                       const char *token){
    const char *tag=visual_public_name(v,n),*value=visual_public_value(v,n);
    if(!tag||!value||!strcmp(tag,"SVG background")||
       !(n->flags&WF_CLICKABLE)||n->ref<10u||n->ref>=40u)return 0;
    if(*size){
        const char *wanted=!strcmp(size,"large")?"20px":"10px";
        if(!strstr(value,wanted))return 0;
    }
    if(*color){
        char fill[40];snprintf(fill,sizeof fill,"fill=%s;",color);
        if(!strstr(value,fill))return 0;
    }
    if(!strcmp(token,"item"))return 1;
    if(!strcmp(token,"shape"))return strcmp(tag,"text")!=0;
    if(!strcmp(token,"circle"))return !strcmp(tag,"circle");
    if(!strcmp(token,"rectangle"))return !strcmp(tag,"rect");
    if(!strcmp(token,"triangle"))return !strcmp(tag,"polygon");
    if(strcmp(tag,"text"))return 0;
    if(strncmp(value,"text=",5)||!value[5])return 0;
    if(!strcmp(token,"letter"))return isalpha((unsigned char)value[5])!=0;
    if(!strcmp(token,"digit"))return isdigit((unsigned char)value[5])!=0;
    return token[0]==value[5]&&!token[1];
}
static int visual_public_shape(const WFView *v,const char *query,
                               int counting,WFAction *a){
    char size[16],color[16],token[32];unsigned count=0;
    if(visual_public_descriptor(query,size,color,token))return -1;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=&v->nodes[i];
        if(visual_public_shape_matches(v,n,size,color,token)){
            if(!counting)return visual_public_click(a,n->ref);
            count++;
        }
    }
    if(!counting)return -1;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=&v->nodes[i];
        const char *name=visual_public_name(v,n);char *end;
        if(n->role!=WF_BUTTON||!name)continue;
        unsigned long value=strtoul(name,&end,10);
        if(*name&&!*end&&value==count)return visual_public_click(a,n->ref);
    }
    return -1;
}
static int visual_public_sides(const WFView *v,WFAction *a){
    const WFNode *canvas=visual_public_find(v,"Canvas polygon outline");
    if(!canvas)return -1;
    const char *path=visual_public_value(v,canvas);unsigned count=0;
    if(!path)return -1;for(const char *p=path;*p;p++)count+=*p==',';
    if(count<3u||count>7u)return -1;
    char label[2]={(char)('0'+count),0};
    const WFNode *button=visual_public_find(v,label);
    return button?visual_public_click(a,button->ref):-1;
}
static int visual_public_identify(const WFView *v,WFAction *a){
    const WFNode *item=NULL;
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].role==WF_OTHER){
        item=&v->nodes[i];break;
    }
    if(!item)return -1;
    const char *tag=visual_public_name(v,item),*value=visual_public_value(v,item);
    if(!tag||!value)return -1;
    const char *label=!strcmp(tag,"rect")?"Rectangle":
        !strcmp(tag,"circle")?"Circle":
        !strcmp(tag,"polygon")?"Triangle":
        !strncmp(value,"text=",5)&&isdigit((unsigned char)value[5])?"Number":"Letter";
    const WFNode *button=visual_public_find(v,label);
    return button?visual_public_click(a,button->ref):-1;
}
static int visual_public_addition(const WFView *v,WFAction *a){
    unsigned sum=0;
    for(unsigned i=0;i<v->count;i++){
        const char *name=visual_public_name(v,&v->nodes[i]);
        if(name&&(!strcmp(name,"Left blue block")||
                  !strcmp(name,"Right blue block")))sum++;
    }
    if(sum<2u||sum>20u)return -1;
    snprintf(visual_public_text,sizeof visual_public_text,"%u",sum);
    const WFNode *input=visual_public_find(v,"math-answer");
    const WFNode *submit=visual_public_find(v,"Submit");
    if(!input||!submit)return -1;
    const char *current=visual_public_value(v,input);
    if(!current)return -1;
    if(!strcmp(current,visual_public_text))return visual_public_click(a,submit->ref);
    if(!(input->flags&WF_FOCUSED))return visual_public_click(a,input->ref);
    if(*current&&(input->selection_start||
                  input->selection_end!=strlen(current))){
        *a=(WFAction){.kind=WF_SELECT_ALL,.target=input->ref};return 0;
    }
    *a=(WFAction){.kind=WF_INSERT,.target=input->ref,
        .text=visual_public_text,.text_length=strlen(visual_public_text)};
    return 0;
}
/* Uses the public query and scene only. The runner supplies elapsed_ms. */
static int visual_public_action(const WFView *v,WFAction *a){
    if(!v||!a||v->count>WF_MAX_NODES)return -1;
    const char *query=wf_text_get(v,v->instruction);
    if(!query)return -1;
    if(!strncmp(query,"Click on the ",13))return visual_public_color(v,query,a);
    if(!strncmp(query,"Expand the pie menu",19))return visual_public_pie(v,query,a);
    if(!strncmp(query,"Select all the shades",21))return visual_public_shades(v,query,a);
    if(!strncmp(query,"Click on a ",11))return visual_public_shape(v,query,0,a);
    if(!strncmp(query,"How many ",9))return visual_public_shape(v,query,1,a);
    if(!strncmp(query,"Press the button",16))return visual_public_sides(v,a);
    if(!strncmp(query,"Click the button",16))return visual_public_identify(v,a);
    if(!strncmp(query,"Type the total number",21))return visual_public_addition(v,a);
    return -1;
}

#endif
