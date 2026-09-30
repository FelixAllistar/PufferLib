#ifndef WEBNAV_TREE_PUBLIC_CONTROLLER_H
#define WEBNAV_TREE_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <string.h>

/* Open unexpanded folders until the requested visible name is found. No
 * hidden descendant label, private target bit or generator seed is read. */
static int tree_public_next(const WFView *v,WFAction *a){
    const char *q=wf_text_get(v,v->instruction),*begin=q?strchr(q,'"'):NULL;
    const char *end=begin?strchr(begin+1,'"'):NULL;
    if(!end)return -1;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;const char *s=wf_text_get(v,n->name);
        if((n->flags&WF_VISIBLE)&&s&&strlen(s)==(size_t)(end-begin-1)&&!strncmp(s,begin+1,(size_t)(end-begin-1))){
            *a=(WFAction){.kind=WF_CLICK,.target=n->ref};return 0;
        }
    }
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(n->role==WF_FOLDER&&(n->flags&WF_VISIBLE)&&!(n->flags&WF_EXPANDED)){
            *a=(WFAction){.kind=WF_CLICK,.target=n->ref};return 0;
        }
    }
    return -1;
}
#endif
