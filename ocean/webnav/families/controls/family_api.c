#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 2048u
#define QUERY 512u
#define COLOR 768u
#define ACTION_TEXT 800u
#define TEXT_LIMIT 31u

void family_controls_batch(uint32_t *words);
static const char *tasks[]={"choose-list","use-slider","use-slider-2",
                            "use-spinner","use-colorwheel","use-colorwheel-2"};

static int ascii_words(const uint32_t *p,unsigned cap) {
    for(unsigned i=0;i<cap;i++) {
        if(!p[i])return 1;
        if(p[i]<32||p[i]>126)return 0;
    }
    return 0;
}
static unsigned word_length(const uint32_t *p,unsigned cap) {
    unsigned i=0;while(i<cap&&p[i])i++;return i;
}
static int add_words(WFView *v,const uint32_t *p,unsigned cap,WFText *out) {
    char buf[257];unsigned n=word_length(p,cap);
    if(n>256)return -1;
    for(unsigned i=0;i<n;i++)buf[i]=(char)p[i];
    return wf_text_add(v,buf,n,out);
}
static int add_string(WFView *v,const char *s,WFText *out) {
    return wf_text_add(v,s,strlen(s),out);
}
static int add_number(WFView *v,uint32_t encoded,int mode,WFText *out) {
    char buf[32];int n=snprintf(buf,sizeof buf,"%d",mode==1?(int)encoded-100:
                                              mode==2?(int32_t)encoded:(int)encoded);
    return n<0||(unsigned)n>=sizeof buf?-1:wf_text_add(v,buf,(size_t)n,out);
}
static int expected_deadline(unsigned task) {
    return task==2?20000:task>=4?7000:10000;
}
static int valid(const uint32_t *r) {
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=6||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    unsigned t=r[WF_TASK];
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=(uint32_t)expected_deadline(t)||
       !ascii_words(r+QUERY,256)||r[44]>4||r[45]>3||r[14]>TEXT_LIMIT)return -1;
    if(t==0) {
        if(r[32]<3||r[32]>9||r[42]>=r[32]||r[43]>=r[32])return -1;
        for(unsigned i=0;i<r[32];i++)if(!ascii_words(r+1024+i*64,64))return -1;
    } else if(t==1||t==2) {
        if(r[33]>r[34]||r[34]>300||r[35]>1||r[36]<r[33]||r[36]>r[34]||
           r[37]<r[33]||r[37]>r[34])return -1;
        if(t==2&&(r[33]!=0||r[34]!=20||r[35]||r[38]>20||r[39]>20||r[40]>20||r[41]>20))return -1;
    } else if(t==3) {
        if((int32_t)r[37]<-10||(int32_t)r[37]>9)return -1;
    } else {
        if(r[46]>0xffffffu||r[49]>31||r[47]>r[48]||r[48]>r[49]||
           !ascii_words(r+COLOR,32)||word_length(r+COLOR,32)!=r[49])return -1;
    }
    return 0;
}
static WFNode *node(WFView *v,unsigned ref,unsigned parent,unsigned role,unsigned flags,
                    float x,float y,float width,float height) {
    if(v->count>=WF_MAX_NODES){v->omitted++;return NULL;}
    WFNode *n=&v->nodes[v->count++];
    n->ref=ref;n->parent=parent;n->role=role;n->flags=flags;
    n->x=x;n->y=y;n->width=width;n->height=height;
    return n;
}
static int named(WFView *v,WFNode *n,const char *name,const char *value) {
    return !n||add_string(v,name,&n->name)||add_string(v,value,&n->value)?-1:0;
}
static int add_submit(WFView *v,unsigned ref,float y) {
    return named(v,node(v,ref,0,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                        12,y,92,24),"Submit","");
}
static int observe(const uint32_t *r,WFView *v) {
    if(valid(r)||r[WF_OP]==WF_RESET||!v)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(add_words(v,r+QUERY,256,&v->instruction))return -1;
    unsigned t=r[WF_TASK];
    if(t==0) {
        WFNode *sel=node(v,1,0,WF_SELECT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,12,32,150,23);
        if(named(v,sel,"Options",""))return -1;
        for(unsigned i=0;i<r[32];i++) {
            unsigned flags=WF_ENABLED|WF_VISIBLE|WF_CLICKABLE|(i==r[42]?WF_SELECTED:0);
            WFNode *n=node(v,10+i,1,WF_OPTION,flags,12,55+i*22,150,22);
            if(!n||add_words(v,r+1024+i*64,64,&n->name)||add_words(v,r+1024+i*64,64,&n->value))return -1;
        }
        return add_submit(v,2,58);
    }
    if(t==1||t==2) {
        unsigned count=t==1?1:3;
        for(unsigned i=0;i<count;i++) {
            char name[80];unsigned encoded=r[36+i*2];
            snprintf(name,sizeof name,"Slider %u (%d to %d, %s)",i+1,
                     (int)r[33]-(t==1?100:0),(int)r[34]-(t==1?100:0),
                     r[35]?"vertical":"horizontal");
            WFNode *n=node(v,i+1,0,WF_SLIDER,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                           12,32+i*45,r[35]?22:105,r[35]?105:22);
            if(!n||add_string(v,name,&n->name)||add_number(v,encoded,t==1,&n->value))return -1;
        }
        return add_submit(v,count+1,35+count*45);
    }
    if(t==3) {
        WFNode *up=node(v,1,0,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,122,34,17,18);
        WFNode *down=node(v,2,0,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,122,52,17,18);
        WFNode *value=node(v,4,0,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_READONLY,12,34,110,36);
        if(named(v,up,"Increase","")||named(v,down,"Decrease","")||!value||
           add_string(v,"Value",&value->name)||add_number(v,r[36],2,&value->value))return -1;
        return add_submit(v,3,75);
    }
    WFNode *input=node(v,1,0,WF_INPUT,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|
                       (r[44]==1?WF_FOCUSED:0),12,44,120,22);
    if(!input||add_string(v,"Color",&input->name)||add_words(v,r+COLOR,32,&input->value))return -1;
    input->selection_start=r[47];input->selection_end=r[48];input->capacity=31;
    if(t==5) {
        char hex[8];snprintf(hex,sizeof hex,"#%06x",r[46]);
        if(named(v,node(v,3,0,WF_CANVAS,WF_VISIBLE,13,12,10,10),"Target swatch",hex))return -1;
    }
    return add_submit(v,2,85);
}
static int action(uint32_t *r,const WFAction *a) {
    if(valid(r)||!a||r[WF_OP]==WF_RESET||r[WF_STATUS]!=WF_RUNNING||
       a->elapsed_ms<r[WF_ELAPSED]||a->text_length>TEXT_LIMIT||
       (a->text_length&&!a->text))return -1;
    unsigned t=r[WF_TASK],kind=a->kind,ref=a->target;
    if(kind==WF_WAIT) {
        if(ref||a->arg0||a->arg1||a->text_length)return -1;
    } else if(t==0) {
        if(kind==WF_SELECT_OPTION) {
            if(ref!=1||a->arg0>=r[32]||a->arg1||a->text_length)return -1;
        } else if(kind==WF_CLICK) {
            if(ref!=2||a->arg0||a->arg1||a->text_length)return -1;
        } else return -1;
    } else if(t==1||t==2) {
        unsigned count=t==1?1:3;
        if(kind==WF_CLICK) {
            if(ref==count+1) {if(a->arg0||a->arg1||a->text_length)return -1;}
            else if(ref<1||ref>count||a->arg0>1000||a->arg1||a->text_length)return -1;
        } else if(kind==WF_POINTER_DOWN||kind==WF_POINTER_MOVE) {
            if(ref<1||ref>count||a->arg0>1000||a->arg1||a->text_length)return -1;
        } else if(kind==WF_POINTER_UP) {
            if(ref>count||a->arg0||a->arg1||a->text_length)return -1;
        } else if(kind==WF_LEFT||kind==WF_RIGHT||kind==WF_HOME||kind==WF_END) {
            if(ref<1||ref>count||a->arg0||a->arg1||a->text_length)return -1;
        } else if(kind==WF_KEY_DOWN||kind==WF_KEY_UP) {
            if(ref<1||ref>count||a->arg0<35||a->arg0>40||a->arg1||a->text_length)return -1;
        } else return -1;
    } else if(t==3) {
        if(kind==WF_CLICK) {
            if(ref<1||ref>3||a->arg0||a->arg1||a->text_length)return -1;
        } else if(kind==WF_KEY_DOWN) {
            if(ref!=4||a->arg1||a->text_length)return -1;
            /* Original page prevents default for every keydown on the input. */
        } else return -1;
    } else {
        if(kind==WF_CLICK) {
            if((ref!=1&&ref!=2)||a->arg0||a->arg1||a->text_length)return -1;
        } else if(kind==WF_SELECT_RANGE) {
            if(ref!=1||a->arg0>a->arg1||a->arg1>r[49]||a->text_length)return -1;
        } else if(kind==WF_INSERT||kind==WF_BACKSPACE||kind==WF_DELETE||
                  kind==WF_LEFT||kind==WF_RIGHT||kind==WF_HOME||kind==WF_END||
                  kind==WF_SELECT_ALL) {
            if(ref!=1||a->arg0||a->arg1)return -1;
            if(kind!=WF_INSERT&&a->text_length)return -1;
            if(kind==WF_INSERT) {
                if(!a->text_length||r[49]-(r[48]-r[47])+a->text_length>31)return -1;
                for(size_t i=0;i<a->text_length;i++)
                    if((unsigned char)a->text[i]<32||(unsigned char)a->text[i]>126)return -1;
            }
        } else return -1;
    }
    r[WF_OP]=WF_STEP;r[WF_ACTION]=kind;r[WF_TARGET]=ref;
    r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->arg1;r[WF_ELAPSED]=a->elapsed_ms;
    r[14]=(uint32_t)a->text_length;
    for(unsigned i=0;i<TEXT_LIMIT;i++)r[ACTION_TEXT+i]=0;
    for(size_t i=0;i<a->text_length;i++)r[ACTION_TEXT+i]=(unsigned char)a->text[i];
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,4,6,"controls",tasks,valid,
                           family_controls_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
