#include "browser_contexts.h"
#include "public.h"
#include "../browser/row.h"
#include "../transport/utf8.h"
#include <string.h>

void primitive_browser_contexts_batch(uint32_t *);

static const uint32_t *tab_row(const WBCState *s,unsigned index) {
    return s->words+64+64*index;
}
static int find_tab(const WBCState *s,uint32_t id) {
    for (unsigned i=0;i<s->words[1];i++) if (tab_row(s,i)[32]==id) return (int)i;
    return -1;
}
int wbc_validate(const WBCState *s) {
    if (!s) return -1;
    const uint32_t *w=s->words;
    if (w[0]!=1 || w[4]<1 || w[4]>WBC_MAX_TABS || w[1]<1 || w[1]>w[4] ||
        w[5]<1 || w[5]>32 || !w[2] || w[3]<2) return -1;
    for (unsigned i=7;i<64;i++) if (w[i]) return -1;
    uint32_t previous=0;unsigned found=0;
    for (unsigned i=0;i<w[1];i++) {
        const uint32_t *r=tab_row(s,i);
        if (wb_row_validate(r) || r[1]!=w[5] || r[32]<=previous || r[32]>=w[3]) return -1;
        previous=r[32];found+=r[32]==w[2];
        for (unsigned j=8;j<16;j++) if (j!=11 && r[j]) return -1;
        for (unsigned j=r[6];j<8;j++) if (r[16+j]) return -1;
        for (unsigned j=r[7];j<8;j++) if (r[24+j]) return -1;
        for (unsigned j=33;j<64;j++) if (r[j]) return -1;
    }
    for (unsigned i=64+64*w[1];i<WBC_WORDS;i++) if (w[i]) return -1;
    return found==1?0:-1;
}
int wbc_reset(WBCState *s,uint32_t seed,uint32_t routes,uint32_t capacity) {
    if (!s || routes<1 || routes>32 || capacity<1 || capacity>WBC_MAX_TABS) return -1;
    WBCState candidate={0};
    candidate.words[12]=1;candidate.words[13]=seed;
    candidate.words[14]=routes;candidate.words[15]=capacity;
    primitive_browser_contexts_batch(candidate.words);
    if (wbc_validate(&candidate)) return -1;
    *s=candidate;return 0;
}
int wbc_step(WBCState *s,uint32_t command,uint32_t argument,uint32_t foreground,uint32_t elapsed) {
    if (wbc_validate(s) || command>WBC_CLOSE || foreground>1 ||
        (command!=WBC_OPEN && foreground)) return -1;
    const uint32_t *w=s->words;
    if (command==WB_NAVIGATE || command==WBC_OPEN) {
        if (argument>=w[5]) return -1;
    } else if (command==WBC_SWITCH || command==WBC_CLOSE) {
        if (find_tab(s,argument)<0) return -1;
    } else if (argument) return -1;
    if (command==WBC_OPEN && (w[1]==w[4] || w[3]==UINT32_MAX)) return -1;
    if (command==WBC_CLOSE && w[1]==1 && w[3]==UINT32_MAX) return -1;
    WBCState candidate=*s;
    candidate.words[8]=command;candidate.words[9]=argument;
    candidate.words[10]=foreground;candidate.words[11]=elapsed;candidate.words[12]=2;
    primitive_browser_contexts_batch(candidate.words);
    if (wbc_validate(&candidate)) return -1;
    *s=candidate;return 0;
}
int wbc_observe(const WBCState *s,WBCView *view) {
    if (!view || wbc_validate(s)) return -1;
    WBCView result={0};const uint32_t *w=s->words;
    result.count=w[1];result.active=w[2];
    result.can_open=w[1]<w[4] && w[3]<UINT32_MAX;
    result.can_close=w[1]>1 || w[3]<UINT32_MAX;
    for (unsigned i=0;i<w[1];i++) {
        const uint32_t *r=tab_row(s,i);
        result.tabs[i]=(WBCTabView){r[32],{r[0],r[4],r[6]!=0,r[7]!=0}};
    }
    *view=result;return 0;
}

typedef struct {
    unsigned role,flags,command,argument,foreground;
    const char *name;
} Control;
static int public_valid(const WBCView *v) {
    if (!v || v->count<1 || v->count>WBC_MAX_TABS || v->can_open>1 || v->can_close>1) return 0;
    unsigned active=0;uint32_t previous=0;
    for (unsigned i=0;i<v->count;i++) {
        const WBCTabView *t=v->tabs+i;
        if (t->identity<=previous || t->browser.phase>WB_FAILED ||
            t->browser.can_back>1 || t->browser.can_forward>1) return 0;
        previous=t->identity;active+=t->identity==v->active;
    }
    return active==1;
}
static unsigned layout(const WBCView *v,Control *out) {
    unsigned n=0;const WBView *active=NULL;
    out[n++]=(Control){.role=WF_PANEL,.flags=WF_VISIBLE|WF_ENABLED,.name="Browser"};
    for (unsigned i=0;i<v->count;i++) {
        int selected=v->tabs[i].identity==v->active;
        if (selected) active=&v->tabs[i].browser;
        out[n++]=(Control){.role=WF_TAB,.flags=WF_VISIBLE|WF_ENABLED|WF_CLICKABLE|(selected?WF_SELECTED:0),
            .command=WBC_SWITCH,.argument=v->tabs[i].identity};
    }
#define BUTTON(label,cmd,arg,fg,enabled) out[n++]=(Control){.role=WF_BUTTON, \
    .flags=WF_VISIBLE|WF_CLICKABLE|((enabled)?WF_ENABLED:0),.name=label,.command=cmd,.argument=arg,.foreground=fg}
    BUTTON("Back",WB_BACK,0,0,active->can_back);
    BUTTON("Forward",WB_FORWARD,0,0,active->can_forward);
    BUTTON("Reload",WB_RELOAD,0,0,1);
    BUTTON("New tab",WBC_OPEN,0,1,v->can_open);
    BUTTON("Close tab",WBC_CLOSE,v->active,0,v->can_close);
    if (active->phase==WB_FAILED) { BUTTON("Retry",WB_RETRY,0,0,1); }
#undef BUTTON
    out[n++]=(Control){.role=WF_TEXT,.flags=WF_VISIBLE|WF_ENABLED|WF_READONLY,.name="Address"};
    return n;
}
static int text_clipped(WFView *v,WUCapabilities *caps,const char *text,size_t limit,WFText *out) {
    if (!text) return -1;
    size_t length=strlen(text);
    if (wt_decode(text,length,NULL,SIZE_MAX,NULL)) return -1;
    size_t n=wt_prefix(text,length,limit);
    if (wf_text_add(v,text,n,out)) return -1;
    if (n<length) {v->text_truncated=1;caps->incomplete=1;}
    return 0;
}
static int control_cap(WUCapabilities *caps,WUCapability item) {
    for (unsigned i=0;i<caps->count;i++) if (caps->items[i].kind==item.kind && caps->items[i].ref==item.ref) {
        WUCapability previous=caps->items[i];
        /* A fixed argument has the same single value for every step size.
         * Browser capabilities conventionally use step=1; native ones use 0. */
        if (previous.min0==previous.max0 && item.min0==item.max0) previous.step0=item.step0=0;
        if (previous.min1==previous.max1 && item.min1==item.max1) previous.step1=item.step1=0;
        return memcmp(&previous,&item,sizeof item)?-1:0;
    }
    if (caps->count>=WU_MAX_CAPABILITIES) return -1;
    caps->items[caps->count++]=item;return 0;
}
int wbc_controls_append(const WBCView *public,const char *const *titles,const char *address,
    uint32_t base,WFView *view,WUCapabilities *caps) {
    if (!public_valid(public) || !titles || !address || !base || base>UINT32_MAX-WBC_REF_STRIDE ||
        !view || !caps || view->version!=WF_ABI_VERSION || view->count>WF_MAX_NODES ||
        view->text_bytes>WF_TEXT_BYTES || caps->version!=WU_CAP_VERSION || caps->count>WU_MAX_CAPABILITIES) return -1;
    for (unsigned i=0;i<view->count;i++) if (view->nodes[i].ref>=base && view->nodes[i].ref<base+WBC_REF_STRIDE) return -1;
    for (unsigned i=0;i<caps->count;i++) {
        const WUCapability *c=caps->items+i;
        if ((c->ref>=base && c->ref<base+WBC_REF_STRIDE) ||
            (c->wire_target>=base && c->wire_target<base+WBC_REF_STRIDE)) return -1;
    }
    Control controls[WBC_MAX_CONTROLS]={0};unsigned count=layout(public,controls);
    if (count>WF_MAX_NODES-view->count) return -1;
    WFView next=*view;WUCapabilities next_caps=*caps;
    unsigned phase=0;
    for (unsigned i=0;i<public->count;i++) if (public->tabs[i].identity==public->active) phase=public->tabs[i].browser.phase;
    const char *const phases[]={"Ready","Loading","Could not load page"};
    for (unsigned i=0;i<count;i++) {
        const Control *c=controls+i;WFNode *node=next.nodes+next.count;
        *node=(WFNode){.ref=base+i,.parent=i?base:0,.role=c->role,.flags=c->flags,
            .x=i<=public->count?(float)(i?i-1:0)*76:100*(float)(i-public->count-1),
            .y=i<=public->count?0:28,.width=i<=public->count?76:96,.height=24};
        const char *name=c->name?c->name:titles[i-1];
        const char *value=i==0?phases[phase]:i==count-1?address:"";
        if (i==0) {node->x=0;node->width=1280;node->height=84;}
        if (i==count-1) {node->x=0;node->y=56;node->width=1280;}
        if (text_clipped(&next,&next_caps,name,128,&node->name) ||
            text_clipped(&next,&next_caps,value,512,&node->value)) return -1;
        next.count++;
        if ((c->flags&(WF_CLICKABLE|WF_ENABLED))==(WF_CLICKABLE|WF_ENABLED) &&
            control_cap(&next_caps,(WUCapability){.kind=WF_CLICK,.ref=node->ref,.wire_target=node->ref})) return -1;
    }
    if (control_cap(&next_caps,(WUCapability){.kind=WF_WAIT})) return -1;
    next_caps.incomplete|=!!(next.omitted||next.text_truncated);
    *view=next;*caps=next_caps;return 0;
}
int wbc_controls_action(const WBCView *public,uint32_t base,const WFAction *action,WBCCommand *out) {
    if (!public_valid(public) || !base || base>UINT32_MAX-WBC_REF_STRIDE || !action || !out ||
        action->arg0 || action->arg1 || action->text_length) return -1;
    WBCCommand result={.elapsed_ms=action->elapsed_ms};
    if (action->kind==WF_WAIT && !action->target) {*out=result;return 0;}
    if (action->kind!=WF_CLICK || action->target<base) return -1;
    Control controls[WBC_MAX_CONTROLS]={0};unsigned count=layout(public,controls),index=action->target-base;
    if (index>=count) return -1;
    const Control *c=controls+index;
    if ((c->flags&(WF_CLICKABLE|WF_ENABLED))!=(WF_CLICKABLE|WF_ENABLED)) return -1;
    result.kind=c->command;result.argument=c->argument;result.foreground=c->foreground;
    *out=result;return 0;
}
int wbc_session_validate(const WBCSession *s) {
    return !s || !s->ref_base || s->ref_base>UINT32_MAX-WBC_REF_STRIDE || wbc_validate(&s->browser)?-1:0;
}
int wbc_session_reset(WBCSession *s,uint32_t seed,uint32_t routes,uint32_t capacity,uint32_t first_ref) {
    if (!s || !first_ref || first_ref>UINT32_MAX-WBC_REF_STRIDE) return -1;
    WBCSession next={.ref_base=first_ref};
    if (wbc_reset(&next.browser,seed,routes,capacity)) return -1;
    *s=next;return 0;
}
int wbc_session_observe(const WBCSession *s,const char *const *titles,const char *address,
    const char *instruction,WFView *view,WUCapabilities *caps) {
    if (wbc_session_validate(s) || !view || !caps || !instruction) return -1;
    WBCView public;if (wbc_observe(&s->browser,&public)) return -1;
    WFView next;wf_view_init(&next,s->elapsed_ms,0);
    WUCapabilities next_caps={.version=WU_CAP_VERSION};
    if (text_clipped(&next,&next_caps,instruction,4096,&next.instruction) ||
        wbc_controls_append(&public,titles,address,s->ref_base,&next,&next_caps)) return -1;
    *view=next;*caps=next_caps;return 0;
}
int wbc_session_command(WBCSession *s,const WBCCommand *command) {
    if (wbc_session_validate(s) || !command || s->ref_base>UINT32_MAX-2*WBC_REF_STRIDE) return -1;
    WBCSession next=*s;
    if (wbc_step(&next.browser,command->kind,command->argument,command->foreground,command->elapsed_ms)) return -1;
    next.ref_base+=WBC_REF_STRIDE;
    uint64_t elapsed=(uint64_t)next.elapsed_ms+command->elapsed_ms;
    next.elapsed_ms=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
    *s=next;return 0;
}
int wbc_session_action(WBCSession *s,const WFAction *action) {
    if (wbc_session_validate(s)) return -1;
    WBCView public;WBCCommand command;
    if (wbc_observe(&s->browser,&public) || wbc_controls_action(&public,s->ref_base,action,&command)) return -1;
    return wbc_session_command(s,&command);
}
