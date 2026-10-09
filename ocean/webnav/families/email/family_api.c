#include "../common/family_api.h"
#include <string.h>

#define ROW 8192u
#define LANES 4u
#define EMAIL_BASE 64u
#define EMAIL_STRIDE 256u
#define QUERY_BASE 3200u
#define SEARCH_BASE 4300u
#define REPLY_BASE 4500u
#define RECIPIENT_BASE 4700u
#define FORWARD_BASE 4800u
#define GOAL_REPLY_BASE 5100u
#define GOAL_RECIPIENT_BASE 5300u
#define PAYLOAD_BASE 5500u

void family_email_batch(uint32_t *words);

static const char *const tasks[]={
    "email-inbox-delete", "email-inbox-forward-nl-turk",
    "email-inbox-forward-nl", "email-inbox-forward", "email-inbox-important",
    "email-inbox-nl-turk", "email-inbox-noscroll", "email-inbox-reply",
    "email-inbox-star-reply", "email-inbox"
};

static const uint32_t *mail(const uint32_t *r,unsigned index){return r+EMAIL_BASE+index*EMAIL_STRIDE;}
static unsigned max_mails(unsigned task){return task==1||task==5||task==9?11u:3u;}
static int supported_kind(unsigned kind){
    return kind==WF_WAIT||kind==WF_CLICK||kind==WF_INSERT||
           kind==WF_BACKSPACE||kind==WF_DELETE||kind==WF_SELECT_ALL||
           kind==WF_SCROLL;
}
static int ascii_z(const uint32_t *s,unsigned cap){
    for(unsigned i=0;i<=cap;i++){
        if(!s[i])return 1;
        if(s[i]<32u||s[i]>126u)return 0;
    }
    return 0;
}
static int ascii_len(const uint32_t *s,unsigned len,unsigned cap){
    if(len>cap||s[len])return 0;
    for(unsigned i=0;i<len;i++)if(s[i]<32u||s[i]>126u)return 0;
    return 1;
}
static unsigned zlen(const uint32_t *s,unsigned cap){unsigned i=0;while(i<cap&&s[i])i++;return i;}
static int valid(const uint32_t *r){
    if(!r||r[0]!=WF_ABI_VERSION||r[1]>=10u||r[2]>WF_STEP)return -1;
    if(r[2]==WF_RESET)return 0;
    unsigned n=r[32],task=r[1];
    if(r[12]!=30000u||r[9]>WF_TIMEOUT||r[33]>4u||r[34]>n||r[35]>4u||r[45]>1u)return -1;
    if(n<3u||n>max_mails(task)||(max_mails(task)==3u&&n!=3u))return -1;
    if(r[46]>=(1u<<n))return -1;
    if(r[40]>3u||r[41]<1u||r[41]>n)return -1;
    if(task==0&&r[40]!=2u)return -1;
    if((task==1||task==2||task==3)&&r[40]!=1u)return -1;
    if(task==4&&r[40]!=3u)return -1;
    if(task==7&&r[40]!=0u)return -1;
    if(task==8&&r[40]!=0u&&r[40]!=3u)return -1;
    if(!ascii_z(r+QUERY_BASE,1023u))return -1;
    if(!ascii_len(r+SEARCH_BASE,r[36],127u)||!ascii_len(r+REPLY_BASE,r[37],159u)||
       !ascii_len(r+RECIPIENT_BASE,r[38],63u)||!ascii_len(r+FORWARD_BASE,r[39],159u))return -1;
    if(!ascii_z(r+GOAL_REPLY_BASE,159u)||!ascii_z(r+GOAL_RECIPIENT_BASE,63u))return -1;
    for(unsigned i=0;i<n;i++){
        const uint32_t *e=mail(r,i);
        if(!ascii_len(e+8,e[0],31u)||!ascii_len(e+40,e[1],39u)||
           !ascii_len(e+80,e[2],159u)||e[3]>1u||e[5]>1u)return -1;
    }
    if(r[33]==0u&&r[35]>0u)return -1;
    if(r[33]==1u&&r[35]>1u)return -1;
    if(r[33]==2u&&(!r[34]||r[35]))return -1;
    if(r[33]==3u&&(!r[34]||r[35]>2u))return -1;
    if(r[33]==4u&&(!r[34]||(r[35]!=0u&&r[35]!=3u&&r[35]!=4u)))return -1;
    if(r[2]==WF_STEP){
        if(!supported_kind(r[4])||r[7])return -1;
        if(r[4]==WF_INSERT){if(!ascii_len(r+PAYLOAD_BASE,r[6],159u))return -1;}
        else if(r[6])return -1;
    }
    return 0;
}
static int add_text(WFView *v,const uint32_t *s,unsigned n,WFText *out){
    char buf[1025];if(n>1024u)return -1;
    for(unsigned i=0;i<n;i++)buf[i]=(char)s[i];
    return wf_text_add(v,buf,n,out);
}
static int add_cstr(WFView *v,const char *s,WFText *out){return wf_text_add(v,s,strlen(s),out);}
static WFNode *node(WFView *v,unsigned ref,unsigned role,unsigned flags,unsigned parent){
    if(v->count==WF_MAX_NODES){v->omitted++;return NULL;}
    WFNode *n=v->nodes+v->count++;*n=(WFNode){0};
    n->ref=ref;n->role=role;n->flags=flags;n->parent=parent;
    n->x=(float)(ref%20u);n->y=(float)(ref/20u);n->width=n->height=1.0f;
    return n;
}
static int button(WFView *v,unsigned ref,const char *label,unsigned flags,unsigned parent){
    WFNode *n=node(v,ref,WF_BUTTON,flags|WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,parent);
    return n?add_cstr(v,label,&n->name):-1;
}
static int field(WFView *v,unsigned ref,const char *label,const uint32_t *value,
                 unsigned len,unsigned cap,unsigned role){
    WFNode *n=node(v,ref,role,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,0);
    if(!n)return -1;
    n->capacity=cap+1u;
    n->selection_start=n->selection_end=len;
    return add_cstr(v,label,&n->name)||add_text(v,value,len,&n->value);
}
static int mail_thread(WFView *v,const uint32_t *r,unsigned i,unsigned mode){
    const uint32_t *e=mail(r,i);unsigned ref=10u+8u*i+(mode?3u:0u);
    WFNode *n=node(v,ref,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,0);
    if(!n||add_text(v,e+8,e[0],&n->name)||add_text(v,e+40,e[1],&n->value))return -1;
    if(button(v,ref+1u,"Star",mode?0u:(e[3]?WF_CHECKED:0u),ref))return -1;
    return button(v,ref+2u,"Trash",0,ref);
}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[2]==WF_RESET)return -1;
    wf_view_init(v,r[8],r[12]);
    /* Zero-initialized optional text references must name an empty string,
       not the first byte of the instruction. */
    WFText empty;
    if(add_cstr(v,"",&empty))return -1;
    if(add_text(v,r+QUERY_BASE,zlen(r+QUERY_BASE,1024u),&v->instruction))return -1;
    unsigned count=r[32],screen=r[33],i;
    if(screen==0u){
        if(button(v,1,"Search",0,0))return -1;
        for(i=0;i<count;i++)if(mail_thread(v,r,i,0))return -1;
    }else if(screen==1u){
        if(button(v,3,"Back",0,0)||field(v,2,"Search",r+SEARCH_BASE,r[36],127u,WF_INPUT))return -1;
        for(i=0;i<count;i++)if((r[46]&(1u<<i))&&mail_thread(v,r,i,1))return -1;
    }else if(screen==2u){
        const uint32_t *e=mail(r,r[34]-1u);
        if(button(v,112,"Back",0,0)||button(v,10u+8u*(r[34]-1u)+6u,"Star",e[5]?WF_CHECKED:0,0)||
           button(v,10u+8u*(r[34]-1u)+7u,"Trash",0,0))return -1;
        WFNode *n=node(v,110,WF_TEXT,WF_VISIBLE,0);
        if(!n||add_text(v,e+8,e[0],&n->name)||add_text(v,e+40,e[1],&n->value))return -1;
        n=node(v,111,WF_TEXT,WF_VISIBLE,0);
        if(!n||add_cstr(v,"Body",&n->name)||add_text(v,e+80,e[2],&n->value))return -1;
        if(button(v,113,"Reply",0,0)||button(v,114,"Forward",0,0))return -1;
    }else if(screen==3u){
        const uint32_t *e=mail(r,r[34]-1u);
        if(button(v,115,"Back",0,0)||button(v,116,"Send",0,0))return -1;
        WFNode *n=node(v,110,WF_TEXT,WF_VISIBLE,0);
        if(!n||add_cstr(v,"To",&n->name)||add_text(v,e+8,e[0],&n->value))return -1;
        if(field(v,117,"Reply",r+REPLY_BASE,r[37],159u,WF_TEXTAREA))return -1;
        if(r[35]==2u)v->nodes[v->count-1u].flags|=WF_FOCUSED;
    }else{
        const uint32_t *e=mail(r,r[34]-1u);
        if(button(v,118,"Back",0,0)||button(v,119,"Send",0,0))return -1;
        WFNode *n=node(v,110,WF_TEXT,WF_VISIBLE,0);
        if(!n||add_cstr(v,"Subject",&n->name)||add_text(v,e+40,e[1],&n->value))return -1;
        if(field(v,120,"To",r+RECIPIENT_BASE,r[38],63u,WF_INPUT)||
           field(v,121,"Forward body",r+FORWARD_BASE,r[39],159u,WF_TEXTAREA))return -1;
        if(r[35]>=3u&&r[35]<=4u)v->nodes[v->count-(r[35]==3u?2u:1u)].flags|=WF_FOCUSED;
    }
    if(screen==1u&&r[35]==1u){for(i=0;i<v->count;i++)if(v->nodes[i].ref==2u)v->nodes[i].flags|=WF_FOCUSED;}
    for(i=0;i<v->count;i++)if((v->nodes[i].flags&WF_FOCUSED)&&r[45]){
        v->nodes[i].selection_start=0;
        v->nodes[i].selection_end=v->nodes[i].value.length;
    }
    return v->omitted? -1:0;
}
static int visible_ref(const uint32_t *r,unsigned ref){
    WFView v;if(observe(r,&v))return 0;
    for(unsigned i=0;i<v.count;i++)if(v.nodes[i].ref==ref&&(v.nodes[i].flags&WF_CLICKABLE))return 1;
    return 0;
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[2]==WF_RESET||a->elapsed_ms<r[8]||!supported_kind(a->kind))return -1;
    if(a->arg0||a->arg1)return -1;
    if(a->kind==WF_CLICK){if(!a->target||!visible_ref(r,a->target)||a->text_length)return -1;}
    else if(a->kind==WF_INSERT){
        if(a->target||!r[35]||!a->text||!a->text_length||a->text_length>159u)return -1;
        unsigned cap=r[35]==1u?127u:(r[35]==3u?63u:159u),len=r[35]==1u?r[36]:(r[35]==2u?r[37]:(r[35]==3u?r[38]:r[39]));
        if((r[45]?0u:len)+a->text_length>cap)return -1;
        for(size_t i=0;i<a->text_length;i++)if((unsigned char)a->text[i]<32u||(unsigned char)a->text[i]>126u)return -1;
    }else if(a->kind==WF_BACKSPACE||a->kind==WF_DELETE||a->kind==WF_SELECT_ALL){
        if(!r[35]||a->target||a->text_length)return -1;
    }else if(a->target||a->text_length)return -1;
    r[2]=WF_STEP;r[4]=a->kind;r[5]=a->target;r[6]=a->kind==WF_INSERT?(uint32_t)a->text_length:0u;
    r[7]=0;r[8]=a->elapsed_ms;
    memset(r+PAYLOAD_BASE,0,160u*sizeof *r);
    if(a->kind==WF_INSERT)for(size_t i=0;i<a->text_length;i++)r[PAYLOAD_BASE+i]=(unsigned char)a->text[i];
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,LANES,10,"email",tasks,valid,family_email_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
