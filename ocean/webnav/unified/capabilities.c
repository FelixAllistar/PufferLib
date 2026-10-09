#include "capabilities.h"
#include <string.h>
int wu_available(const WFNode *n) {
    return n && (n->flags&(WF_VISIBLE|WF_ENABLED))==(WF_VISIBLE|WF_ENABLED);
}
const WFNode *wu_node(const WFView *v,uint32_t ref) {
    if(!v)return NULL;
    for(uint32_t i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
int wu_cap_add(WUCapabilities *c,WUCapability item) {
    if(!c||item.kind>WF_SELECT_OPTION||item.min0>item.max0||item.min1>item.max1)return -1;
    for(uint32_t i=0;i<c->count;i++)if(c->items[i].kind==item.kind&&c->items[i].ref==item.ref){
        return memcmp(c->items+i,&item,sizeof item)==0?0:-1;
    }
    if(c->count==WU_MAX_CAPABILITIES){c->incomplete=1;return -1;}
    c->items[c->count++]=item;return 0;
}
int wu_capabilities(const WFFamily *family,uint32_t task,const WFView *view,WUCapabilities *caps) {
    if(!family||!view||!caps||task>=family->task_count||view->version!=WF_ABI_VERSION||view->count>WF_MAX_NODES)return -1;
    memset(caps,0,sizeof *caps);caps->version=WU_CAP_VERSION;
    WUCapability wait={0};wait.kind=WF_WAIT;
    if(wu_cap_add(caps,wait))return -1;
    int (*providers[])(const WFFamily *,uint32_t,const WFView *,WUCapabilities *)={wu_caps_basic,wu_caps_text,wu_caps_widgets,wu_caps_pointer};
    for(unsigned i=0;i<sizeof providers/sizeof *providers;i++){
        int result=providers[i](family,task,view,caps);
        if(result<0)return -1;
        if(result>0){
            if(view->omitted||view->text_truncated)caps->incomplete=1;
            for(uint32_t k=0;k<caps->count;k++)if(caps->items[k].ref&&!wu_node(view,caps->items[k].ref))return -1;
            return 0;
        }
    }
    return -1;
}
