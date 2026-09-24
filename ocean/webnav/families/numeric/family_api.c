#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 4096u
#define LANES 8u
#define NODE_BASE 64u
#define NODE_STRIDE 48u
#define NODE_CAP 32u
#define QUERY 1600u
#define ACTION_TEXT 1856u
#define ACTION_TEXT_CAP 24u

void family_numeric_batch(uint32_t *words);

static const char *tasks[]={
    "ascending-numbers","find-greatest","generate-number","guess-number",
    "hot-cold","number-checkboxes","odd-or-even","simple-algebra",
    "simple-arithmetic"
};

static int ascii_words(const uint32_t *s,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!s[i])return 1;
        if(s[i]<32||s[i]>126)return 0;
    }
    return 0;
}

static int deadline(unsigned task){
    static const unsigned values[]={10000,10000,15000,15000,15000,30000,15000,10000,10000};
    return values[task];
}

static unsigned expected_nodes(unsigned task){
    static const unsigned values[]={5,4,3,3,2,29,7,3,3};
    return values[task];
}

static unsigned input_words(const uint32_t *s,unsigned cap){
    unsigned n=0;while(n<cap&&s[n])n++;return n;
}

static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=9||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=(uint32_t)deadline(r[WF_TASK])||
       r[33]!=expected_nodes(r[WF_TASK])||r[33]>NODE_CAP||
       !ascii_words(r+QUERY,256))return -1;
    if(r[42]>r[43]||r[43]>24)return -1;
    if((r[WF_TASK]==0&&(r[32]>1||(r[32]==0&&r[34]!=0)||(r[32]==1&&(r[34]<1||r[34]>4))))||
       (r[WF_TASK]==2&&r[32]>1)||((r[WF_TASK]!=0&&r[WF_TASK]!=2)&&r[32]!=0))return -1;
    for(unsigned i=0;i<r[33];i++){
        const uint32_t *n=r+NODE_BASE+i*NODE_STRIDE;
        if(n[0]>5||n[1]>1||n[2]>1||!ascii_words(n+4,16)||
           !ascii_words(n+20,24)||n[46]>1000||n[47]>1000)return -1;
    }
    for(unsigned i=0;i<ACTION_TEXT_CAP;i++)if(r[ACTION_TEXT+i]>126)return -1;
    return 0;
}

static int add_words(WFView *v,const uint32_t *s,unsigned cap,WFText *out){
    char text[257];unsigned n=input_words(s,cap);if(n>256)return -1;
    for(unsigned i=0;i<n;i++)text[i]=(char)s[i];
    return wf_text_add(v,text,n,out);
}

static int add_name(WFView *v,const uint32_t *r,unsigned index,
                    const uint32_t *node,WFText *out){
    if(r[WF_TASK]==2&&index==2)return wf_text_add(v,"Generated number",16,out);
    return add_words(v,node+4,16,out);
}

static int add_int(WFView *v,int32_t value,WFText *out){
    char text[32];int n=snprintf(text,sizeof text,"%d",value);
    return n<0||(size_t)n>=sizeof text?-1:wf_text_add(v,text,(size_t)n,out);
}

static int add_signal(WFView *v,unsigned code,WFText *out){
    const char *s=code==1?"HOT":code==2?"WARM":code==3?"COLD":code==4?"ICE COLD":"";
    return wf_text_add(v,s,strlen(s),out);
}

static int add_feedback(WFView *v,const uint32_t *r,WFText *out){
    char text[128];
    if(r[40]==3)return wf_text_add(v,"Correct!",8,out);
    if(r[40]==4)return wf_text_add(v,"",0,out);
    const char *prefix=r[40]==1?"The number is higher than ":r[40]==2?"The number is lower than ":"Waiting for your guess...";
    int len=r[40]?snprintf(text,sizeof text,"%s%d.",prefix,(int32_t)r[34]):snprintf(text,sizeof text,"%s",prefix);
    return len<0||(size_t)len>=sizeof text?-1:wf_text_add(v,text,(size_t)len,out);
}

static int value_for_node(const uint32_t *r,unsigned index,const uint32_t *n,WFView *v,WFText *out){
    switch(n[0]){
    case 0:
        if(r[WF_TASK]==0&&!r[32])return add_int(v,(int32_t)n[3],out);
        return wf_text_add(v,"",0,out);
    case 5:
        return n[1]?add_int(v,(int32_t)n[3],out):wf_text_add(v,"",0,out);
    case 2:
        return add_words(v,n+20,24,out);
    case 4:
        if(r[WF_TASK]==2&&index==2){
            if(!r[32])return wf_text_add(v,"-",1,out);
            return add_int(v,(int32_t)r[41],out);
        }
        if(r[WF_TASK]==3&&index==2)return add_feedback(v,r,out);
        if(r[WF_TASK]==4&&index==1)return add_signal(v,r[40],out);
        if((r[WF_TASK]==7||r[WF_TASK]==8)&&index==2)return add_words(v,n+20,24,out);
        return add_words(v,n+20,24,out);
    default:
        return wf_text_add(v,"",0,out);
    }
}

static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(add_words(v,r+QUERY,256,&v->instruction))return -1;
    for(unsigned i=0;i<r[33];i++){
        const uint32_t *n=r+NODE_BASE+i*NODE_STRIDE;
        int ascending_gone=r[WF_TASK]==0&&r[32]&&i<r[34];
        if(ascending_gone)continue;
        if(v->count==WF_MAX_NODES){v->omitted++;continue;}
        WFNode *o=v->nodes+v->count++;
        o->ref=i+1;o->parent=0;
        switch(n[0]){
        case 0: case 5:o->role=WF_BUTTON;break;
        case 1:o->role=WF_CHECKBOX;break;
        case 2:o->role=WF_INPUT;break;
        case 3:o->role=WF_CANVAS;break;
        default:o->role=WF_TEXT;break;
        }
        o->flags=WF_VISIBLE|WF_ENABLED;
        if(n[0]!=4||(r[WF_TASK]==4&&i==1))o->flags|=WF_CLICKABLE;
        if(n[0]==1&&n[1])o->flags|=WF_CHECKED;
        if(n[0]==0&&r[WF_TASK]==6&&n[1])o->flags|=WF_SELECTED;
        if(n[0]==5&&n[1])o->flags|=WF_SELECTED;
        o->x=(float)(int32_t)n[44];o->y=(float)(int32_t)n[45];
        o->width=(float)n[46];o->height=(float)n[47];
        if(n[0]==2){o->selection_start=r[42];o->selection_end=r[43];o->capacity=23;}
        if(r[WF_TASK]==0&&r[32]&&i<5){
            if(wf_text_add(v,"",0,&o->name)||wf_text_add(v,"",0,&o->value))return -1;
        }else if(add_name(v,r,i,n,&o->name)||value_for_node(r,i,n,v,&o->value))return -1;
    }
    return 0;
}

static int input_node(const uint32_t *r,unsigned ref){
    if(!ref||ref>r[33])return 0;
    return r[NODE_BASE+(ref-1)*NODE_STRIDE]==2;
}

static int action(uint32_t *r,const WFAction *a){
    if(valid(r)||!a||r[WF_STATUS]!=WF_RUNNING||a->elapsed_ms<r[WF_ELAPSED]||
       a->text_length>ACTION_TEXT_CAP||(a->text_length&&!a->text))return -1;
    for(size_t i=0;i<a->text_length;i++)if((unsigned char)a->text[i]<32||(unsigned char)a->text[i]>126)return -1;
    if(a->kind==WF_WAIT){if(a->target||a->arg0||a->arg1||a->text_length)return -1;}
    else if(a->kind==WF_CLICK){
        if(!a->target||a->target>r[33]||a->text_length)return -1;
        if(r[WF_TASK]==0&&r[32]&&a->target<=r[34])return -1;
        const uint32_t *n=r+NODE_BASE+(a->target-1)*NODE_STRIDE;
        if(n[0]==4&&!(r[WF_TASK]==4&&a->target==2))return -1;
        if(r[WF_TASK]==4&&a->target==1){if(a->arg0>154||a->arg1>125)return -1;}
        else if(a->arg0||a->arg1)return -1;
    }else if(a->kind==WF_POINTER_MOVE){
        if(r[WF_TASK]!=4||a->target!=1||a->arg0>154||a->arg1>125||a->text_length)return -1;
    }else if(a->kind==WF_INSERT||a->kind==WF_BACKSPACE||a->kind==WF_DELETE||
             a->kind==WF_LEFT||a->kind==WF_RIGHT||a->kind==WF_HOME||
             a->kind==WF_END||a->kind==WF_SELECT_ALL){
        if((r[WF_TASK]!=3&&r[WF_TASK]!=7&&r[WF_TASK]!=8)||!input_node(r,a->target)||a->arg0||a->arg1)return -1;
        if(a->kind!=WF_INSERT&&a->text_length)return -1;
        if(a->kind==WF_INSERT){
            const uint32_t *input=r+NODE_BASE+20;
            unsigned current=input_words(input,24);
            if(current+a->text_length>23)return -1;
        }
    }else if(a->kind==WF_ENTER){
        if(r[WF_TASK]!=3||!input_node(r,a->target)||a->arg0||a->arg1||a->text_length)return -1;
    }else return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->kind==WF_INSERT?(uint32_t)a->text_length:a->arg1;
    r[WF_ELAPSED]=a->elapsed_ms;r[14]=(uint32_t)a->text_length;
    for(unsigned i=0;i<ACTION_TEXT_CAP;i++)r[ACTION_TEXT+i]=0;
    for(size_t i=0;i<a->text_length;i++)r[ACTION_TEXT+i]=(unsigned char)a->text[i];
    return 0;
}

static const WFFamily api={WF_ABI_VERSION,ROW,LANES,9,"numeric",tasks,valid,
                           family_numeric_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
