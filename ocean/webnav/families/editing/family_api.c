#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 8192u
#define LANES 4u
void family_editing_batch(uint32_t *words);
static const char *const tasks[]={"find-word","highlight-text","highlight-text-2",
    "text-editor","terminal"};

static int ascii_len(const uint32_t *p,unsigned len,unsigned cap){
    if(len>=cap||p[len])return -1;
    for(unsigned i=0;i<len;i++)if(p[i]<32u||p[i]>126u)return -1;
    return 0;
}
static int ascii_z(const uint32_t *p,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!p[i])return 0;
        if(p[i]<32u||p[i]>126u)return -1;
    }
    return -1;
}
static int paragraph_len(const uint32_t *p,unsigned len,unsigned cap){
    if(len>=cap||p[len])return -1;
    for(unsigned i=0;i<len;i++)
        if(p[i]!=10u&&(p[i]<32u||p[i]>126u))return -1;
    return 0;
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=5u||
       r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    unsigned deadline=r[WF_TASK]==3u?15000u:
        r[WF_TASK]==4u?20000u:10000u;
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=deadline||
       ascii_z(r+256u,256u))return -1;
    if(r[WF_TASK]==3u){
        if(r[32]>r[33]||r[33]>r[37]||r[34]>3u||r[35]>=6u||
           r[36]>1u||r[37]<3u||
           r[37]>WF_MAX_NODES-36u||
           r[38]>r[39]||r[39]>r[37]||
           r[40]<3u||r[40]>4u||r[41]>=r[40]||r[42]>1u||
           ascii_len(r+512u,r[37],128u))return -1;
        for(unsigned i=0;i<r[37];i++)
            if((r[2048u+i]>>3u)>30u)return -1;
        if(r[WF_OP]==WF_STEP&&
           r[WF_ACTION]!=WF_WAIT&&r[WF_ACTION]!=WF_CLICK&&
           r[WF_ACTION]!=WF_SELECT_RANGE)return -1;
        return 0;
    }
    if(r[WF_TASK]==4u){
        if(r[32]>=13u||r[33]<3u||r[33]>5u||r[34]>1u||
           r[35]>=128u||r[36]>9u||
           r[40u]>=(1u<<r[33])||
           ascii_len(r+2048u,r[35],128u))return -1;
        for(unsigned i=0;i<r[33];i++)
            if(r[48u+i]>=13u||r[56u+i]<1u||r[56u+i]>=128u||
               ascii_len(r+512u+i*128u,r[56u+i],128u))return -1;
        if(r[WF_OP]==WF_STEP){
            unsigned k=r[WF_ACTION];
            if(k!=WF_WAIT&&k!=WF_CLICK&&k!=WF_INSERT&&
               k!=WF_BACKSPACE&&k!=WF_ENTER)return -1;
            if(k==WF_INSERT){
                if(!r[34]||r[6]<1u||r[6]>64u||
                   ascii_len(r+4096u,r[6],65u))return -1;
            }else if(r[6])return -1;
        }
        return 0;
    }
    if(r[WF_TASK]!=0u){
        unsigned count=r[WF_TASK]==1u?1u:3u;
        if(r[32]>=count||r[33]!=count||r[34]>r[35]||
           r[35]>r[36]||r[36]<1u||r[36]>=512u||
           r[37]<1u||r[37]>=1024u||r[38]>1u||
           paragraph_len(r+512u,r[36],512u)||
           paragraph_len(r+2048u,r[37],1024u))return -1;
        for(unsigned i=0;i<count;i++)
            if(r[40u+2u*i]+r[41u+2u*i]>r[36])return -1;
        if(r[WF_OP]==WF_STEP){
            if(r[WF_ACTION]!=WF_WAIT&&r[WF_ACTION]!=WF_CLICK&&
               r[WF_ACTION]!=WF_SELECT_RANGE)return -1;
            if(r[WF_ACTION]==WF_SELECT_RANGE&&
               (r[WF_ARG0]>r[WF_ARG1]||r[WF_ARG1]>r[36]))return -1;
        }
        return 0;
    }
    if(
       r[32]>=r[33]||r[33]<6u||r[33]>14u||r[34]>=128u||
       r[35]!=r[34]||r[36]!=r[34]||r[37]>1u||
       r[38]<1u||r[38]>=64u||
       ascii_z(r+512u,512u)||
       ascii_len(r+1024u,r[38],64u)||
       ascii_len(r+1152u,r[34],128u))return -1;
    if(r[WF_OP]==WF_STEP){
        if(r[WF_ACTION]==WF_INSERT){
            if(!r[37]||r[6]<1u||r[6]>64u||
               ascii_len(r+2048u,r[6],65u))return -1;
        }else if(r[6])return -1;
        if(r[WF_ACTION]!=WF_WAIT&&r[WF_ACTION]!=WF_CLICK&&
           r[WF_ACTION]!=WF_INSERT)return -1;
    }
    return 0;
}
static int add_units(WFView *v,const uint32_t *p,unsigned cap,WFText *out){
    char text[513];unsigned n=0;
    while(n<cap&&p[n]){text[n]=(char)p[n];n++;}
    if(n==cap)return -1;
    return wf_text_add(v,text,n,out);
}
static WFNode *node(WFView *v,unsigned ref,unsigned role,unsigned flags){
    if(v->count>=WF_MAX_NODES){v->omitted++;return NULL;}
    WFNode *n=v->nodes+v->count++;*n=(WFNode){0};
    n->ref=ref;n->role=role;n->flags=flags|WF_VISIBLE;
    n->width=n->height=1.0f;n->y=(float)v->count;return n;
}
static int highlight_text(const uint32_t *r,WFView *v){
    for(unsigned i=0;i<r[33];i++){
        WFNode *n=node(v,10u+i,WF_TEXT,0u);
        if(!n)return -1;
        char label[]="Paragraph 0";label[10]=(char)('1'+i);
        if(wf_text_add(v,label,11u,&n->name))return -1;
        uint32_t begin=r[40u+2u*i],len=r[41u+2u*i];
        char text[513];for(unsigned k=0;k<len;k++)
            text[k]=(char)r[512u+begin+k];
        if(wf_text_add(v,text,len,&n->value))return -1;
        n->selection_start=r[34u]>begin?
            (r[34u]-begin<len?r[34u]-begin:len):0u;
        n->selection_end=r[35u]>begin?
            (r[35u]-begin<len?r[35u]-begin:len):0u;
    }
    return 0;
}
static int highlight_submit(WFView *v){
    WFNode *n=node(v,1u,WF_BUTTON,WF_ENABLED|WF_CLICKABLE);
    return n&& !wf_text_add(v,"Submit",6u,&n->name)?0:-1;
}
static int editor_view(const uint32_t *r,WFView *v){
    WFNode *n=node(v,10u,WF_TEXTAREA,WF_ENABLED|WF_CLICKABLE);
    if(!n||wf_text_add(v,"Editor",6u,&n->name)||
       add_units(v,r+512u,128u,&n->value))return -1;
    n->capacity=128u;n->selection_start=r[38];n->selection_end=r[39];
    for(unsigned i=0;i<r[37];i++){
        n=node(v,100u+i,WF_TEXT,0u);if(!n)return -1;
        n->parent=10u;
        char ch=(char)r[512u+i],code[16];
        snprintf(code,sizeof code,"%u",r[2048u+i]);
        if(wf_text_add(v,&ch,1u,&n->name)||
           wf_text_add(v,code,strlen(code),&n->value))return -1;
    }
    static const char *const labels[]={"Bold","Italic","Underline"};
    for(unsigned i=0;i<3u;i++){
        n=node(v,2u+i,WF_BUTTON,WF_ENABLED|WF_CLICKABLE);
        if(!n||wf_text_add(v,labels[i],strlen(labels[i]),&n->name))return -1;
    }
    n=node(v,5u,WF_BUTTON,WF_ENABLED|WF_CLICKABLE|
        (r[42]?WF_EXPANDED:0u));
    if(!n||wf_text_add(v,"Color",5u,&n->name))return -1;
    static const char *const colors[]={"red","orange","yellow",
        "green","blue","purple"};
    for(unsigned i=0;r[42]&&i<30u;i++){
        n=node(v,20u+i,WF_OPTION,WF_ENABLED|WF_CLICKABLE);
        if(!n||wf_text_add(v,colors[i/5u],strlen(colors[i/5u]),&n->name))return -1;
    }
    return highlight_submit(v);
}
static int terminal_output(const uint32_t *r,char *out,size_t cap){
    static const char *const fixed[]={"",
        "ls: list contents\nUsage: ls\nrm: remove entries\nUsage: rm file",
        "","error: ls arguments not understood.",
        "error: file argument not found.",
        "error: rm argument '*' not supported. please enter the exact file name.",
        "error: file not found.","","Command not found.",""};
    if(r[36]!=2u){snprintf(out,cap,"%s",fixed[r[36]]);return 0;}
    size_t used=0u;
    for(unsigned i=0;i<r[33];i++)if(r[40u]&(1u<<i)){
        const uint32_t *p=r+512u+i*128u;
        unsigned len=r[56u+i];
        if(used+len+2u>=cap)return -1;
        if(used)out[used++]=' ';
        for(unsigned k=0;k<len;k++)out[used++]=(char)p[k];
    }
    out[used]=0;return 0;
}
static int terminal_view(const uint32_t *r,WFView *v){
    WFNode *n=node(v,1u,WF_PANEL,WF_ENABLED|WF_CLICKABLE|
        (r[34]?WF_FOCUSED:0u));
    if(!n||wf_text_add(v,"terminal",8u,&n->name))return -1;
    n->capacity=128u; /* public command buffer, including its terminator */
    n=node(v,2u,WF_TEXT,0u);
    if(!n||wf_text_add(v,"user$",5u,&n->name)||
       add_units(v,r+2048u,128u,&n->value))return -1;
    n->capacity=128u;
    n=node(v,3u,WF_TEXT,0u);
    char output[1024];if(!n||terminal_output(r,output,sizeof output)||
       wf_text_add(v,"Output",6u,&n->name)||
       wf_text_add(v,output,strlen(output),&n->value))return -1;
    return 0;
}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET||!v)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    WFText empty;if(wf_text_add(v,"",0u,&empty))return -1;
    if(add_units(v,r+256u,256u,&v->instruction))return -1;
    if(r[WF_TASK]==3u)return editor_view(r,v);
    if(r[WF_TASK]==4u)return terminal_view(r,v);
    if(r[WF_TASK]!=0u){
        if(r[38]==0u&&highlight_submit(v))return -1;
        if(highlight_text(r,v))return -1;
        if(r[38]==1u&&highlight_submit(v))return -1;
        return v->omitted?-1:0;
    }
    WFNode *n=node(v,3u,WF_TEXT,0u);
    if(!n||wf_text_add(v,"Paragraph",9u,&n->name)||
       add_units(v,r+512u,512u,&n->value))return -1;
    n=node(v,1u,WF_INPUT,WF_ENABLED|WF_CLICKABLE|
        (r[37]?WF_FOCUSED:0u));
    if(!n||wf_text_add(v,"Answer",6u,&n->name)||
       add_units(v,r+1152u,128u,&n->value))return -1;
    n->capacity=128u;n->selection_start=r[35];n->selection_end=r[36];
    n=node(v,2u,WF_BUTTON,WF_ENABLED|WF_CLICKABLE);
    if(!n||wf_text_add(v,"Submit",6u,&n->name))return -1;
    return v->omitted?-1:0;
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[WF_OP]==WF_RESET||
       a->elapsed_ms<r[WF_ELAPSED])return -1;
    if(r[WF_TASK]==3u){
        if(a->text_length||r[WF_STATUS]!=WF_RUNNING)return -1;
        if(a->kind==WF_SELECT_RANGE){
            if(a->target||a->arg0>a->arg1||a->arg1>r[37])return -1;
        }else if(a->kind==WF_CLICK){
            unsigned t=a->target;
            if(a->arg0||a->arg1||
               !(t==1u||(t>=2u&&t<=5u)||
                 (r[42]&&t>=20u&&t<=49u)))return -1;
        }else if(a->kind!=WF_WAIT||a->target||a->arg0||a->arg1)return -1;
        r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;
        r[WF_TARGET]=a->target;r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->arg1;
        r[WF_ELAPSED]=a->elapsed_ms;return 0;
    }
    if(r[WF_TASK]==4u){
        if(r[WF_STATUS]!=WF_RUNNING||a->arg0||a->arg1)return -1;
        if(a->kind==WF_CLICK){
            if(a->target!=1u||a->text_length)return -1;
        }else if(a->kind==WF_INSERT){
            if(!r[34]||a->target||!a->text||!a->text_length||
               a->text_length>64u||r[35]+a->text_length>127u)return -1;
            for(size_t i=0;i<a->text_length;i++)
                if((unsigned char)a->text[i]<32u||
                   (unsigned char)a->text[i]>126u)return -1;
        }else if(a->kind==WF_BACKSPACE||a->kind==WF_ENTER){
            if(!r[34]||a->target||a->text_length)return -1;
        }else if(a->kind!=WF_WAIT||a->target||a->text_length)return -1;
        r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
        r[WF_ELAPSED]=a->elapsed_ms;
        r[6]=a->kind==WF_INSERT?(uint32_t)a->text_length:0u;
        memset(r+4096u,0,65u*sizeof *r);
        if(a->kind==WF_INSERT)for(size_t i=0;i<a->text_length;i++)
            r[4096u+i]=(unsigned char)a->text[i];
        return 0;
    }
    if(r[WF_TASK]!=0u){
        if(a->text_length||r[WF_STATUS]!=WF_RUNNING)return -1;
        if(a->kind==WF_SELECT_RANGE){
            if(a->target||a->arg0>a->arg1||a->arg1>r[36])return -1;
        }else if(a->kind==WF_CLICK){
            if(a->target!=1u||a->arg0||a->arg1)return -1;
        }else if(a->kind!=WF_WAIT||a->target||a->arg0||a->arg1)return -1;
        r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;
        r[WF_TARGET]=a->target;r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->arg1;
        r[WF_ELAPSED]=a->elapsed_ms;return 0;
    }
    if(a->arg0||a->arg1)return -1;
    if(a->kind==WF_CLICK){
        if(r[WF_STATUS]!=WF_RUNNING||a->text_length||
           (a->target!=1u&&a->target!=2u))return -1;
    }else if(a->kind==WF_INSERT){
        if(r[WF_STATUS]!=WF_RUNNING||!r[37]||a->target||
           !a->text||!a->text_length||a->text_length>64u||
           r[34]+a->text_length>127u)return -1;
        for(size_t i=0;i<a->text_length;i++){
            unsigned ch=(unsigned char)a->text[i];
            if(ch<32u||ch>126u)return -1;
        }
    }else if(a->kind!=WF_WAIT||a->target||a->text_length)return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ELAPSED]=a->elapsed_ms;
    r[6]=a->kind==WF_INSERT?(uint32_t)a->text_length:0u;
    memset(r+2048u,0,65u*sizeof *r);
    if(a->kind==WF_INSERT)
        for(size_t i=0;i<a->text_length;i++)r[2048u+i]=(unsigned char)a->text[i];
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,LANES,5u,"editing",tasks,
    valid,family_editing_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
