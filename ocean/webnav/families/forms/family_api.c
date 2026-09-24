#include "../common/family_api.h"
#include <string.h>

#define ROW 8192u
#define LANES 4u
#define FIELD_BASE 128u
#define FIELD_STRIDE 544u
#define STATIC_BASE 2400u
#define STATIC_STRIDE 128u
#define QUERY_BASE 4608u
#define CLIPBOARD_BASE 5120u
#define PAYLOAD_BASE 5376u

void family_forms_batch(uint32_t *words);

static const char *tasks[]={
    "enter-text-dynamic", "enter-text-2", "enter-password", "text-transform",
    "copy-paste", "copy-paste-2", "read-table-2", "login-user-popup"
};

static unsigned field_count(unsigned task){
    switch(task){case 0:case 1:case 3:return 1;case 2:case 4:case 6:case 7:return 2;case 5:return 4;default:return 0;}
}
static unsigned static_count(unsigned task){
    switch(task){case 0:case 1:case 2:case 4:case 5:return 1;case 3:return 2;case 6:return 13;case 7:return 5;default:return 0;}
}
static unsigned deadline(unsigned task){return task==6?20000u:(task==2||task==3||task==7?15000u:10000u);}
static int ascii_words(const uint32_t *s,unsigned len,unsigned cap){
    if(len>cap)return 0;
    for(unsigned i=0;i<len;i++)if(s[i]<32u||s[i]>126u)return 0;
    return s[len]==0;
}
static int ascii_z(const uint32_t *s,unsigned cap){
    for(unsigned i=0;i<=cap;i++){
        if(!s[i])return 1;
        if(s[i]<32u||s[i]>126u)return 0;
    }
    return 0;
}
static const uint32_t *field(const uint32_t *r,unsigned i){return r+FIELD_BASE+i*FIELD_STRIDE;}
static const uint32_t *statik(const uint32_t *r,unsigned i){return r+STATIC_BASE+i*STATIC_STRIDE;}

static int valid(const uint32_t *r){
    if(!r||r[0]!=WF_ABI_VERSION||r[1]>=8u||r[2]>WF_STEP)return -1;
    if(r[2]==WF_RESET)return r[6]>1u?-1:0;
    unsigned task=r[1],nf=field_count(task),ns=static_count(task);
    if(!nf||r[9]>WF_TIMEOUT||r[12]!=deadline(task)||r[32]!=nf||r[38]!=ns||r[33]>nf)return -1;
    if(r[34]>1u||r[35]>2u||r[36]>1u||r[37]>255u)return -1;
    if(task!=7&&(r[34]||r[35]||r[36]))return -1;
    if(task==7&&r[35]>2u)return -1;
    if(!ascii_z(r+QUERY_BASE,511u))return -1;
    if(!ascii_words(r+CLIPBOARD_BASE,r[37],255u))return -1;
    for(unsigned i=0;i<nf;i++){
        const uint32_t *f=field(r,i);
        uint32_t role=f[0],len=f[1],start=f[2],end=f[3],goal_len=f[4],label_len=f[5];
        if((role!=WF_INPUT&&role!=WF_TEXTAREA)||len>255u||goal_len>255u||start>len||end>len||label_len>23u)return -1;
        if(!ascii_words(f+8,len,255u)||!ascii_words(f+264,goal_len,255u)||!ascii_words(f+520,label_len,23u))return -1;
        if(task==4&&i==0&&role!=WF_TEXTAREA)return -1;
        if(task==5&&i<3&&role!=WF_TEXTAREA)return -1;
        if(role==WF_TEXTAREA&&goal_len)return -1;
    }
    for(unsigned i=0;i<ns;i++){
        const uint32_t *n=statik(r,i);
        if(n[0]>WF_TEXTAREA||n[1]>3u||n[3]>1u||n[5]>63u||n[6]>55u)return -1;
        if(!ascii_words(n+8,n[5],63u)||!ascii_words(n+72,n[6],55u))return -1;
        if(n[1]&&n[0]!=WF_BUTTON)return -1;
        if(n[3]&&task!=7)return -1;
    }
    if(r[2]==WF_STEP){
        if(r[4]>WF_PASTE)return -1;
        if(r[4]==WF_INSERT&&r[6]>255u)return -1;
        if(!ascii_words(r+PAYLOAD_BASE,r[4]==WF_INSERT?r[6]:0u,255u))return -1;
    }
    return 0;
}

static int add_text(WFView *v,const uint32_t *s,unsigned len,WFText *out){
    char buf[513];if(len>512u)return -1;
    for(unsigned i=0;i<len;i++)buf[i]=(char)s[i];
    return wf_text_add(v,buf,len,out);
}

static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[2]==WF_RESET)return -1;
    wf_view_init(v,r[8],r[12]);
    unsigned nf=r[32],ns=r[38];
    unsigned query_len=0;while(query_len<512u&&r[QUERY_BASE+query_len])query_len++;
    if(add_text(v,r+QUERY_BASE,query_len,&v->instruction))return -1;
    for(unsigned i=0;i<nf;i++){
        const uint32_t *f=field(r,i);WFNode *n=v->nodes+v->count++;
        n->ref=i+1u;n->role=f[0];n->flags=WF_VISIBLE|WF_CLICKABLE;
        if(!r[34])n->flags|=WF_ENABLED;
        if(r[33]==i+1u)n->flags|=WF_FOCUSED;
        n->selection_start=f[2];n->selection_end=f[3];n->capacity=256u;
        if(add_text(v,f+520,f[5],&n->name)||add_text(v,f+8,f[1],&n->value))return -1;
        n->x=(float)i;n->y=0.0f;n->width=1.0f;n->height=1.0f;
    }
    for(unsigned i=0;i<ns;i++){
        const uint32_t *s=statik(r,i);if(s[3]&&!r[34])continue;
        WFNode *n=v->nodes+v->count++;n->ref=nf+i+1u;n->role=s[0];n->parent=s[2];n->flags=WF_VISIBLE;
        if(s[0]==WF_BUTTON){n->flags|=WF_CLICKABLE;if(!(r[34]&&s[1]==1u))n->flags|=WF_ENABLED;}
        if(add_text(v,s+8,s[5],&n->name)||add_text(v,s+72,s[6],&n->value))return -1;
        n->x=(float)(nf+i);n->y=1.0f;n->width=1.0f;n->height=1.0f;
    }
    return 0;
}

static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[2]==WF_RESET||a->elapsed_ms<r[8])return -1;
    unsigned nf=r[32],ns=r[38];
    if(a->kind>WF_PASTE)return -1;
    if(a->kind!=WF_INSERT&&a->text_length)return -1;
    if(a->kind==WF_INSERT){
        if(a->text_length>255u||(!a->text&&a->text_length))return -1;
        for(size_t i=0;i<a->text_length;i++)if((unsigned char)a->text[i]<32u||(unsigned char)a->text[i]>126u)return -1;
        if(!r[33]||r[34]||a->target||a->arg1)return -1;
    }
    if(a->kind==WF_CLICK){
        if(!a->target||a->target>nf+ns)return -1;
        if(a->target<=nf){if(r[34])return -1;}
        else {unsigned i=a->target-nf-1u;const uint32_t *n=statik(r,i);
            if(n[3]&&!r[34])return -1;
            if(a->target==nf+1u&&r[34]&&n[1]==1u)return -1;
        }
    }else if(a->kind==WF_SELECT_RANGE){
        if(!r[33]||r[34]||a->target!=r[33]||a->arg0>a->arg1||a->arg1>field(r,r[33]-1u)[1])return -1;
    }else if(a->kind==WF_COPY||a->kind==WF_PASTE||
             (a->kind>=WF_BACKSPACE&&a->kind<=WF_SELECT_ALL)){
        if(!r[33]||r[34]||a->target)return -1;
    }else if(a->kind==WF_TAB_KEY||a->kind==WF_ENTER||a->kind==WF_WAIT){
        if(a->target||a->arg0||a->arg1)return -1;
    }
    r[2]=WF_STEP;r[4]=a->kind;r[5]=a->target;r[6]=a->kind==WF_INSERT?(uint32_t)a->text_length:a->arg0;
    r[7]=a->arg1;r[8]=a->elapsed_ms;
    memset(r+PAYLOAD_BASE,0,256u*sizeof *r);
    if(a->kind==WF_INSERT)for(size_t i=0;i<a->text_length;i++)r[PAYLOAD_BASE+i]=(unsigned char)a->text[i];
    return 0;
}

static const WFFamily api={WF_ABI_VERSION,ROW,LANES,8,"forms",tasks,valid,family_forms_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
