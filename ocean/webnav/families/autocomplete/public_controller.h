#ifndef WEBNAV_AUTOCOMPLETE_PUBLIC_CONTROLLER_H
#define WEBNAV_AUTOCOMPLETE_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <string.h>

static int autocomplete_public_next(const WFView *v,char *scratch,size_t cap,WFAction *a){
    const char *q=wf_text_get(v,v->instruction),*p=q?strchr(q,'"'):NULL,*e=p?strchr(p+1,'"'):NULL;
    if(!e)return -1;
    size_t np=(size_t)(e-p-1);const char *s=strchr(e+1,'"'),*t=s?strchr(s+1,'"'):NULL;
    size_t ns=t?(size_t)(t-s-1):0;const WFNode *field=NULL;
    for(unsigned i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;const char *name=wf_text_get(v,n->name);
        if(n->role==WF_INPUT)field=n;
        if(n->role==WF_OPTION&&name){size_t len=strlen(name);
            if(len>=np&&len>=ns&&!strncmp(name,p+1,np)&&(!ns||!strncmp(name+len-ns,s+1,ns))){*a=(WFAction){.kind=WF_CLICK,.target=n->ref};return 0;}
        }
    }
    if(!field)return -1;
    const char *value=wf_text_get(v,field->value);if(!value)return -1;
    size_t len=strlen(value);
    if(len>=np&&len>=ns&&!strncmp(value,p+1,np)&&(!ns||!strncmp(value+len-ns,s+1,ns))){*a=(WFAction){.kind=WF_CLICK,.target=2};return 0;}
    if(!(field->flags&WF_FOCUSED)){*a=(WFAction){.kind=WF_CLICK,.target=field->ref};return 0;}
    if(len==np&&!strncmp(value,p+1,np)){*a=(WFAction){.kind=WF_WAIT};return 0;}
    if(len&&field->selection_end-field->selection_start!=len){*a=(WFAction){.kind=WF_SELECT_ALL};return 0;}
    if(np>=cap)return -1;memcpy(scratch,p+1,np);scratch[np]=0;
    *a=(WFAction){.kind=WF_INSERT,.text=scratch,.text_length=np};return 0;
}
#endif
