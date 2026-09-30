#include "../common/family_api.h"
#include "../../miniwob/tree/validation.h"
#include <string.h>

void family_tree_batch(uint32_t *words);
static const char *tasks[]={"navigate-tree"};

static int unpack(const uint32_t *words,unsigned cap,char *out){
    for(unsigned i=0;i<cap;i++){
        unsigned c=(words[i/4u]>>(8u*(i%4u)))&255u;
        out[i]=(char)c;if(!c)return (int)i;
        if(c<32u||c>126u)return -1;
    }
    return -1;
}

static int valid(const uint32_t *r){
    if(!r||r[0]!=WF_ABI_VERSION||r[1]||r[2]>WF_STEP)return -1;
    if(r[2]==WF_RESET)return r[6]? -1:0;
    const uint32_t *b=r+32;char text[128];
    if(!b[1]||!webnav_tree_valid(b)||r[12]!=10000u||r[9]!=b[2])return -1;
    if(unpack(b+224,128,text)<0)return -1;
    for(unsigned i=0;i<b[1];i++)if(b[16+6*i+5]>1u||unpack(b+64+8*i,32,text)<0)return -1;
    return 0;
}

static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[2]==WF_RESET)return -1;
    const uint32_t *b=r+32;char text[128];
    wf_view_init(v,r[8],r[12]);
    int len=unpack(b+224,128,text);if(len<0||wf_text_add(v,text,(size_t)len,&v->instruction))return -1;
    for(unsigned i=0;i<b[1];i++){
        const uint32_t *p=b+16+6*i;if(!p[5])continue;
        WFNode *n=v->nodes+v->count++;
        n->ref=i+1;n->parent=p[3];n->role=p[0]?WF_FOLDER:WF_FILE;
        n->flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|(p[2]?WF_EXPANDED:0);
        len=unpack(b+64+8*i,32,text);
        if(len<0||wf_text_add(v,text,(size_t)len,&n->name)||wf_text_add(v,"",0,&n->value))return -1;
        n->x=(float)webnav_tree_depth(b,i);n->y=(float)i;n->width=1;n->height=1;
    }
    return 0;
}

static int action(uint32_t *r,const WFAction *a){
    if(!a||valid(r)||r[2]==WF_RESET||a->elapsed_ms<r[8]||a->text_length||a->arg0||a->arg1)return -1;
    uint32_t *b=r+32;
    if(a->kind==WF_CLICK){
        if(!a->target||a->target>b[1]||!b[16+6*(a->target-1)+5])return -1;
    }else if(a->kind!=WF_WAIT||a->target)return -1;
    r[2]=WF_STEP;r[4]=a->kind;r[5]=a->target;r[8]=a->elapsed_ms;
    b[7]=a->kind==WF_CLICK?1u:0u;b[8]=a->target?a->target-1u:0u;b[9]=a->elapsed_ms;
    return 0;
}

static const WFFamily api={WF_ABI_VERSION,512,8,1,"tree",tasks,valid,family_tree_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
