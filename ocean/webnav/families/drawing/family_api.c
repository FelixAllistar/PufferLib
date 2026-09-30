#include "../common/family_api.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
void family_drawing_batch(uint32_t *words);
static const char *tasks[]={"draw-circle","draw-line"};
static int valid(const uint32_t *r){
    if(!r||r[0]!=2||r[1]>1||r[2]>2)return -1;
    if(r[2]==WF_RESET)return r[6]?-1:0;
    if(r[9]>2||r[12]!=10000||r[8]>10000||r[34]>1||r[36]>256||r[37]>1)return -1;
    if(r[32]>150u*65536||r[33]>110u*65536)return -1;
    for(unsigned i=0;i<r[36];i++)if(r[128+2*i]>150*256||r[129+2*i]>110*256)return -1;
    for(unsigned i=0;i<256;i++){if(!r[768+i])return 0;if(r[768+i]<32||r[768+i]>126)return -1;}return -1;
}
static int text(WFView *v,const char *s,WFText *t){return wf_text_add(v,s,strlen(s),t);}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[2]==WF_RESET)return -1;
    wf_view_init(v,r[8],r[12]);char query[256];unsigned n=0;
    while(r[768+n]){query[n]=(char)r[768+n];n++;}query[n]=0;
    if(text(v,query,&v->instruction))return -1;
    v->count=3;
    WFNode *canvas=v->nodes;*canvas=(WFNode){.ref=1,.role=WF_CANVAS,.flags=WF_VISIBLE|WF_ENABLED,.width=150,.height=110};
    char path[10000];int used=snprintf(path,sizeof path,"active=%u samples=%u;",r[37],r[36]);
    for(unsigned i=r[36];i>0;i--){int k=snprintf(path+used,sizeof path-(size_t)used," %.4f,%.4f",r[128+2*(i-1)]/256.0,r[129+2*(i-1)]/256.0);if(k<0||(size_t)k>=sizeof path-(size_t)used)return -1;used+=k;}
    if(text(v,"Drawing surface",&canvas->name)||text(v,path,&canvas->value))return -1;
    WFNode *submit=v->nodes+1;*submit=(WFNode){.ref=2,.role=WF_BUTTON,.flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,.y=112,.width=70,.height=20};
    if(text(v,"Submit",&submit->name)||text(v,"",&submit->value))return -1;
    WFNode *dot=v->nodes+2;*dot=(WFNode){.ref=3,.parent=1,.role=WF_OTHER,.flags=WF_VISIBLE,.x=r[32]/65536.0f-4,.y=r[33]/65536.0f-4,.width=8,.height=8};
    if(text(v,r[1]?"purple dot":"green dot",&dot->name)||text(v,"",&dot->value))return -1;
    return 0;
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[2]==WF_RESET||a->elapsed_ms<r[8]||a->elapsed_ms>10000||a->text_length)return -1;
    if(a->kind>=WF_POINTER_DOWN&&a->kind<=WF_POINTER_UP){
        if(a->target!=1||a->arg0>150*256||a->arg1>110*256)return -1;
        if(a->kind==WF_POINTER_DOWN&&r[37])return -1;
        if(a->kind==WF_POINTER_MOVE&&r[37]&&r[36]>=256)return -1;
    }else if(a->kind==WF_CLICK){if(a->target!=2||a->arg0||a->arg1||r[37])return -1;}
    else if(a->kind==WF_WAIT){if(a->target||a->arg0||a->arg1)return -1;}
    else return -1;
    r[2]=WF_STEP;r[4]=a->kind;r[5]=a->target;r[6]=a->arg0;r[7]=a->arg1;r[8]=a->elapsed_ms;return 0;
}
static const WFFamily api={2,2048,4,2,"drawing",tasks,valid,family_drawing_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
