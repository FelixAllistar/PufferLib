#include "../common/family_api.h"
#include <string.h>
#include <stdio.h>

#define ROW 8192u
#define LANES 4u
#define CONTACT_BASE 64u
#define CONTACT_STRIDE 512u
#define QUERY 3000u

void family_catalog_batch(uint32_t *words);
static const char *const tasks[]={"phone-book","order-food","search-engine"};
static const char *const properties[]={"Phone","Email","Address"};
static const char *const food_types[]={"dairy","gluten-free","meat","peanuts","vegan"};
static const uint32_t *contact(const uint32_t *r,unsigned page){
    return r+CONTACT_BASE+(page-1u)*CONTACT_STRIDE;
}
static int ascii_z(const uint32_t *s,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!s[i])return 1;
        if(s[i]<32u||s[i]>126u)return 0;
    }
    return 0;
}
static int ascii_len(const uint32_t *s,unsigned len,unsigned cap){
    if(len>=cap||s[len])return 0;
    for(unsigned i=0;i<len;i++)if(s[i]<32u||s[i]>126u)return 0;
    return 1;
}
static unsigned zlen(const uint32_t *s,unsigned cap){
    unsigned i=0;while(i<cap&&s[i])i++;return i;
}
static int phone_valid(const uint32_t *r){
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=15000u||
       r[32]<1u||r[32]>5u||r[33]<1u||r[33]>5u||
       r[34]>2u||r[35]!=5u||r[36]>3u||!ascii_z(r+QUERY,512u))return -1;
    for(unsigned i=1;i<=5u;i++){
        const uint32_t *p=contact(r,i);
        if(!ascii_z(p,64u)||!ascii_z(p+64,64u)||
           !ascii_z(p+128,128u)||!ascii_z(p+256,128u))return -1;
    }
    if(r[WF_OP]==WF_STEP){
        if(r[WF_ACTION]!=WF_WAIT&&r[WF_ACTION]!=WF_CLICK)return -1;
        if(r[WF_ACTION]==WF_WAIT&&r[WF_TARGET])return -1;
    }
    return 0;
}
static int food_valid(const uint32_t *r){
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=20000u||
       r[32]>1u||r[33]>=12u||r[34]>=12u||r[33]==r[34]||
       r[35]>=5u||r[36]<2u||r[36]>4u||!ascii_z(r+QUERY,512u))return -1;
    uint64_t total=0u;
    for(unsigned i=0;i<12u;i++){
        const uint32_t *p=r+CONTACT_BASE+i*128u;
        if(p[1]>31u||p[2]>2u||!ascii_z(p+8,120u))return -1;
        total+=p[0];
    }
    if(total>UINT32_MAX)return -1;
    if(r[WF_OP]==WF_STEP){
        if(r[WF_ACTION]!=WF_WAIT&&r[WF_ACTION]!=WF_CLICK)return -1;
        if(r[WF_ACTION]==WF_WAIT&&r[WF_TARGET])return -1;
    }
    return 0;
}
static int search_valid(const uint32_t *r){
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=20000u||
       r[32]>3u||r[33]>=9u||r[34]<1u||r[34]>63u||
       r[35]>127u||r[36]>r[35]||r[37]>r[35]||r[36]>r[37]||
       r[38]>1u||r[39]>1u||(r[32]==0u&&r[39])||
       !ascii_len(r+2700,r[34],64u)||
       !ascii_len(r+2500,r[35],128u)||!ascii_z(r+QUERY,512u))return -1;
    for(unsigned i=0;i<9u;i++){
        const uint32_t *p=r+CONTACT_BASE+i*256u;
        if(!ascii_z(p,64u)||!ascii_z(p+64,96u)||!ascii_z(p+160,96u))return -1;
    }
    for(unsigned i=0;i<3u;i++){
        const uint32_t *p=r+4000u+i*256u;
        if(!ascii_z(p,64u)||!ascii_z(p+64,96u)||!ascii_z(p+160,96u))return -1;
    }
    if(r[WF_OP]==WF_STEP){
        unsigned k=r[WF_ACTION];
        if(k!=WF_WAIT&&k!=WF_CLICK&&k!=WF_INSERT&&
           k!=WF_BACKSPACE&&k!=WF_DELETE&&k!=WF_LEFT&&k!=WF_RIGHT&&
           k!=WF_HOME&&k!=WF_END&&k!=WF_SELECT_ALL)return -1;
        if(k==WF_INSERT){if(!r[38]||!ascii_len(r+5000,r[6],128u)||!r[6])return -1;}
        else if(r[6])return -1;
    }
    return 0;
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=3u||r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    return r[WF_TASK]==0u?phone_valid(r):r[WF_TASK]==1u?food_valid(r):search_valid(r);
}
static int units(WFView *v,const uint32_t *s,unsigned cap,WFText *out){
    char buf[513];unsigned n=zlen(s,cap);if(n>=cap||n>512u)return -1;
    for(unsigned i=0;i<n;i++)buf[i]=(char)s[i];
    return wf_text_add(v,buf,n,out);
}
static WFNode *node(WFView *v,unsigned ref,unsigned role,unsigned flags){
    if(v->count==WF_MAX_NODES){v->omitted++;return NULL;}
    WFNode *n=v->nodes+v->count++;*n=(WFNode){0};
    n->ref=ref;n->role=role;n->flags=flags|WF_VISIBLE;n->y=(float)(v->count-1u);
    n->width=n->height=1.0f;return n;
}
static int phone_observe(const uint32_t *r,WFView *v){
    for(unsigned i=1;i<=5u;i++){
        if(i!=r[32]&&i+1u!=r[32]&&i!=r[32]+1u)continue;
        WFNode *n=node(v,i,WF_BUTTON,r[32]==i?WF_SELECTED:
            WF_ENABLED|WF_CLICKABLE);
        if(!n)return -1;
        char label[]={"Page 0"};label[5]=(char)('0'+i);
        if(wf_text_add(v,label,strlen(label),&n->name))return -1;
    }
    const uint32_t *p=contact(r,r[32]);
    WFNode *n=node(v,99u,WF_TEXT,0);
    if(!n||units(v,p,64u,&n->name))return -1;
    for(unsigned i=0;i<3u;i++){
        n=node(v,100u+i,WF_LINK,WF_ENABLED|WF_CLICKABLE);
        if(!n||wf_text_add(v,properties[i],strlen(properties[i]),&n->name))return -1;
        const uint32_t *s=i==0u?p+64:i==1u?p+128:p+256;
        unsigned cap=i==0u?64u:128u;
        if(units(v,s,cap,&n->value))return -1;
    }
    return v->omitted?-1:0;
}
static int food_observe(const uint32_t *r,WFView *v){
    for(unsigned i=0;i<12u;i++){
        const uint32_t *p=r+CONTACT_BASE+i*128u;
        unsigned parent=16u*(i+1u)+2u;
        WFNode *n=node(v,parent,WF_TEXT,0);
        if(!n||units(v,p+8,120u,&n->name))return -1;
        char qty[16];snprintf(qty,sizeof qty,"%u",p[0]);
        if(wf_text_add(v,qty,strlen(qty),&n->value))return -1;
        for(unsigned control=0;control<2u;control++){
            n=node(v,16u*(i+1u)+control,WF_BUTTON,WF_ENABLED|WF_CLICKABLE);
            if(!n||wf_text_add(v,control?"Add":"Remove",control?3u:6u,&n->name))return -1;
        }
        for(unsigned bit=0;bit<5u;bit++)if(p[1]&(1u<<bit)){
            n=node(v,200u+i*5u+bit,WF_TEXT,0);
            if(!n||wf_text_add(v,food_types[bit],strlen(food_types[bit]),&n->name))return -1;
            n->parent=parent;
        }
    }
    WFNode *n=node(v,1u,WF_BUTTON,WF_ENABLED|WF_CLICKABLE);
    if(!n||wf_text_add(v,"Order!",6u,&n->name))return -1;
    return v->omitted?-1:0;
}
static int search_result(WFView *v,const uint32_t *p,unsigned ref,unsigned desc_ref){
    WFNode *n=node(v,ref,WF_LINK,WF_ENABLED|WF_CLICKABLE);
    if(!n||units(v,p,64u,&n->name)||units(v,p+64,96u,&n->value))return -1;
    n=node(v,desc_ref,WF_TEXT,0);
    if(!n||wf_text_add(v,"Description",11u,&n->name)||
       units(v,p+160,96u,&n->value))return -1;
    n->parent=ref;return 0;
}
static int search_observe(const uint32_t *r,WFView *v){
    WFNode *n=node(v,1u,WF_INPUT,WF_ENABLED|WF_CLICKABLE|
        (r[38]?WF_FOCUSED:0u));
    if(!n||wf_text_add(v,"Search text",11u,&n->name)||
       units(v,r+2500,128u,&n->value))return -1;
    n->capacity=128u;n->selection_start=r[36];n->selection_end=r[37];
    n=node(v,2u,WF_BUTTON,WF_ENABLED|WF_CLICKABLE);
    if(!n||wf_text_add(v,"Search",6u,&n->name))return -1;
    if(r[32]){
        for(unsigned page=1u;page<=3u;page++){
            n=node(v,9u+page,WF_BUTTON,r[32]==page?WF_SELECTED:
                WF_ENABLED|WF_CLICKABLE);
            char label[]="Page 0";label[5]=(char)('0'+page);
            if(!n||wf_text_add(v,label,6u,&n->name))return -1;
        }
        for(unsigned i=0;i<3u;i++){
            if(r[39]){
                unsigned index=(r[32]-1u)*3u+i;
                if(search_result(v,r+CONTACT_BASE+index*256u,100u+index,300u+index))return -1;
            }else if(search_result(v,r+4000u+i*256u,200u+i,400u+i))return -1;
        }
    }
    return v->omitted?-1:0;
}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET||!v)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    WFText empty;if(wf_text_add(v,"",0,&empty))return -1;
    if(units(v,r+QUERY,512u,&v->instruction))return -1;
    return r[WF_TASK]==0u?phone_observe(r,v):
        r[WF_TASK]==1u?food_observe(r,v):search_observe(r,v);
}
static int search_target(const uint32_t *r,unsigned ref){
    if(ref==1u||ref==2u)return 1;
    if(ref>=10u&&ref<=12u)return r[32]!=0u&&ref!=9u+r[32];
    if(r[32]==0u)return 0;
    if(r[39])return ref>=100u+3u*(r[32]-1u)&&ref<100u+3u*r[32];
    return ref>=200u&&ref<=202u;
}
static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[WF_OP]==WF_RESET||a->elapsed_ms<r[WF_ELAPSED]||
       a->arg0||a->arg1)return -1;
    if(r[WF_TASK]!=2u){
        if(a->text_length||(a->kind!=WF_WAIT&&a->kind!=WF_CLICK))return -1;
    }else{
        unsigned k=a->kind;
        if(k!=WF_WAIT&&k!=WF_CLICK&&k!=WF_INSERT&&
           k!=WF_BACKSPACE&&k!=WF_DELETE&&k!=WF_LEFT&&k!=WF_RIGHT&&
           k!=WF_HOME&&k!=WF_END&&k!=WF_SELECT_ALL)return -1;
        if(k==WF_INSERT){
            if(!r[38]||a->target||!a->text||!a->text_length||a->text_length>127u||
               r[35]-(r[37]-r[36])+a->text_length>127u)return -1;
            for(size_t i=0;i<a->text_length;i++)if((unsigned char)a->text[i]<32u||
                (unsigned char)a->text[i]>126u)return -1;
        }else if(k!=WF_CLICK&&k!=WF_WAIT){
            if(!r[38]||a->target||a->text_length)return -1;
        }else if(a->text_length)return -1;
    }
    if(a->kind==WF_CLICK){
        if(r[WF_TASK]==0u){
            if(!((a->target>=1u&&a->target<=5u)||
                 (a->target>=100u&&a->target<=102u)))return -1;
            if(a->target<=5u&&
               a->target+1u!=r[32]&&a->target!=r[32]+1u)return -1;
        }else if(r[WF_TASK]==1u&&a->target!=1u){
            unsigned post=a->target/16u,slot=a->target%16u;
            if(post<1u||post>12u||slot>1u)return -1;
            if(slot==1u){
                uint64_t total=0u;
                for(unsigned i=0;i<12u;i++)
                    total+=r[CONTACT_BASE+i*128u];
                if(total>=UINT32_MAX)return -1;
            }
        }else if(r[WF_TASK]==2u&&!search_target(r,a->target))return -1;
    }else if(a->target)return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ELAPSED]=a->elapsed_ms;
    r[6]=a->kind==WF_INSERT?(uint32_t)a->text_length:0u;
    if(r[WF_TASK]==2u){
        memset(r+5000,0,128u*sizeof *r);
        if(a->kind==WF_INSERT)for(size_t i=0;i<a->text_length;i++)r[5000+i]=(unsigned char)a->text[i];
    }
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,LANES,3,"catalog",tasks,
    valid,family_catalog_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
