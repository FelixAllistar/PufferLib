#include "capabilities.h"
#include <string.h>

/* These providers describe the effective wire protocols. In particular, an
 * accepted event whose decoder only emits Wait is not an affordance. */
static int simple(WUCapabilities *c,uint32_t kind,uint32_t ref,uint32_t target){
    return wu_cap_add(c,(WUCapability){.kind=kind,.ref=ref,.wire_target=target});
}
static int parameter(WUCapabilities *c,uint32_t kind,uint32_t ref,
                     uint32_t target,uint32_t lo,uint32_t hi,uint32_t unit){
    return wu_cap_add(c,(WUCapability){.kind=kind,.ref=ref,.wire_target=target,
        .flags=WU_CAP_RANGE,.min0=lo,.max0=hi,.step0=1,.unit0=unit});
}
static int selection(WUCapabilities *c,uint32_t ref,uint32_t target,
                     uint32_t length){
    /* The action builder must enforce arg0 <= arg1 as well as these bounds. */
    return wu_cap_add(c,(WUCapability){.kind=WF_SELECT_RANGE,.ref=ref,
        .wire_target=target,.flags=WU_CAP_RANGE,.max0=length,.max1=length,
        .step0=1,.step1=1,.unit0=WU_UNIT_TEXT_OFFSET,.unit1=WU_UNIT_TEXT_OFFSET});
}
static int insertion(WUCapabilities *c,uint32_t ref,uint32_t target,
                     uint32_t capacity){
    if(!capacity)return 0;
    return wu_cap_add(c,(WUCapability){.kind=WF_INSERT,.ref=ref,
        .wire_target=target,.flags=WU_CAP_TEXT|WU_CAP_ASCII,
        .text_capacity=capacity});
}
static int editable(const WFNode *n){
    return wu_available(n)&&!(n->flags&WF_READONLY)&&
        (n->role==WF_INPUT||n->role==WF_TEXTAREA);
}
static int focused(const WFNode *n){return editable(n)&&(n->flags&WF_FOCUSED);}
static uint32_t lesser(uint32_t a,uint32_t b){return a<b?a:b;}
static int text_length(const WFView *v,const WFNode *n,uint32_t *length){
    if(!n||!wf_text_get(v,n->value))return -1;
    *length=n->value.length;return 0;
}
static uint32_t room(const WFView *v,const WFNode *n,WUCapabilities *c,
                     uint32_t transport,int replace){
    uint32_t length;
    if(!n->capacity||text_length(v,n,&length)){
        c->incomplete=1;return 0;
    }
    /* These families advertise storage including the trailing NUL. Date and
     * time use their distinct whole-value protocol below. */
    uint32_t limit=n->capacity-1u;
    if(replace)return lesser(limit,transport);
    if(n->selection_start>n->selection_end||n->selection_end>length||length>limit){
        c->incomplete=1;return 0;
    }
    return lesser(limit-length+n->selection_end-n->selection_start,transport);
}
static int clicks(const WFView *v,WUCapabilities *c){
    for(uint32_t i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(wu_available(n)&&(n->flags&WF_CLICKABLE)&&
           simple(c,WF_CLICK,n->ref,n->ref))return -1;
    }
    return 0;
}
static int editing_keys(WUCapabilities *c,const WFNode *n,uint32_t last){
    for(uint32_t kind=WF_BACKSPACE;kind<=last;kind++)
        if(simple(c,kind,n->ref,0))return -1;
    return 0;
}

static int forms(const WFView *v,WUCapabilities *c){
    int have_field=0;
    for(uint32_t i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(!wu_available(n))continue;
        if((editable(n)||n->role==WF_BUTTON)&&(n->flags&WF_CLICKABLE)&&
           simple(c,WF_CLICK,n->ref,n->ref))return -1;
        if(editable(n))have_field=1;
        if(!focused(n))continue;
        if(insertion(c,n->ref,0,room(v,n,c,255,0))||
           editing_keys(c,n,WF_SELECT_ALL)||
           simple(c,WF_COPY,n->ref,0)||simple(c,WF_PASTE,n->ref,0))return -1;
        uint32_t length;
        if(text_length(v,n,&length))c->incomplete=1;
        else if(selection(c,n->ref,n->ref,length))return -1;
    }
    /* Enter, scroll, pointer and raw-key events decode to Wait. Tab has its
     * own focus-cycle transition; the API requires its target to be zero. */
    if(have_field&&simple(c,WF_TAB_KEY,0,0))return -1;
    return 1;
}

static int autocomplete(const WFView *v,WUCapabilities *c){
    if(clicks(v,c))return -1;
    const WFNode *n=wu_node(v,1);
    if(!focused(n))return 1;
    if(insertion(c,n->ref,0,room(v,n,c,32,0))||
       editing_keys(c,n,WF_SELECT_ALL)||simple(c,WF_ENTER,n->ref,0)||
       wu_cap_add(c,(WUCapability){.kind=WF_KEY_DOWN,.ref=n->ref,
           .wire_target=0,.flags=WU_CAP_RANGE,.min0=38,.max0=40,
           .step0=2,.unit0=WU_UNIT_KEY}))return -1;
    /* Options are clicked by their public refs, not selected using an index
     * from WF_SELECT_OPTION (which this family does not accept). */
    return 1;
}

static int editing(uint32_t task,const WFView *v,WUCapabilities *c){
    if(task==0){
        if(clicks(v,c))return -1;
        const WFNode *n=wu_node(v,1);
        if(focused(n)&&insertion(c,n->ref,0,room(v,n,c,64,0)))return -1;
        return 1;
    }
    if(task==1||task==2){
        const WFNode *submit=wu_node(v,1);
        if(wu_available(submit)&&simple(c,WF_CLICK,1,1))return -1;
        /* The declared document protocol joins the observed paragraphs with
         * one newline. Selection coordinates address that complete document,
         * including cross-paragraph ranges and incorrect choices. */
        uint32_t length=0,count=task==1?1u:3u;
        for(uint32_t i=0;i<count;i++){
            const WFNode *n=wu_node(v,10+i);uint32_t part;
            if(!n||text_length(v,n,&part)){c->incomplete=1;return 1;}
            if(part>511u||length>511u-part){c->incomplete=1;return 1;}
            length+=part;
            if(i+1<count)length++;
        }
        if(selection(c,10,0,length))return -1;
        return 1;
    }
    if(task==3){
        /* The textarea click is syntactically rejected: only formatting
         * controls, their open color options, and Submit are click targets. */
        for(uint32_t i=0;i<v->count;i++){
            const WFNode *n=v->nodes+i;
            if(wu_available(n)&&(n->flags&WF_CLICKABLE)&&
               (n->role==WF_BUTTON||n->role==WF_OPTION)&&
               simple(c,WF_CLICK,n->ref,n->ref))return -1;
        }
        const WFNode *n=wu_node(v,10);uint32_t length;
        if(!n||text_length(v,n,&length))c->incomplete=1;
        else if(selection(c,n->ref,0,length))return -1;
        return 1;
    }
    const WFNode *panel=wu_node(v,1),*prompt=wu_node(v,2);
    if(wu_available(panel)&&(panel->flags&WF_CLICKABLE)&&simple(c,WF_CLICK,1,1))return -1;
    if(!wu_available(panel)||!(panel->flags&WF_FOCUSED))return 1;
    if(simple(c,WF_BACKSPACE,1,0)||simple(c,WF_ENTER,1,0))return -1;
    /* Existing terminal views expose the command on the prompt but omit
     * its capacity. Do not manufacture a capacity from private row layout. */
    uint32_t length,capacity=panel->capacity;
    if(!capacity&&prompt)capacity=prompt->capacity;
    if(!capacity||!prompt||text_length(v,prompt,&length)||length>=capacity){
        c->incomplete=1;return 1;
    }
    if(insertion(c,1,0,lesser(capacity-1u-length,64)))return -1;
    return 1;
}

static int email(const WFView *v,WUCapabilities *c){
    if(clicks(v,c))return -1;
    for(uint32_t i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(!focused(n))continue;
        /* This protocol appends or replaces a selected-all field. Older
         * projections omit that selection state. Bound insertion by known
         * remaining room and flag the missing observation instead of assuming
         * that select-all has occurred. */
        if(insertion(c,n->ref,0,room(v,n,c,159,0))||
           simple(c,WF_BACKSPACE,n->ref,0)||simple(c,WF_DELETE,n->ref,0)||
           simple(c,WF_SELECT_ALL,n->ref,0))return -1;
    }
    /* WF_SCROLL is accepted by C but the wire only performs an unchanged edit. */
    return 1;
}

static int catalog(uint32_t task,const WFView *v,WUCapabilities *c){
    if(task==1){
        uint64_t total=0;
        int quantities=1;
        /* Add is rejected at the uint32 aggregate bound. Quantities are
         * public decimal values on the twelve product nodes. */
        for(uint32_t product=1;product<=12;product++){
            const WFNode *n=wu_node(v,16u*product+2u);
            const char *value=n?wf_text_get(v,n->value):NULL;
            uint64_t quantity=0;
            if(!value||!n->value.length){quantities=0;break;}
            for(uint32_t j=0;j<n->value.length;j++){
                if(value[j]<'0'||value[j]>'9'){quantities=0;break;}
                quantity=quantity*10u+(uint32_t)(value[j]-'0');
                if(quantity>UINT32_MAX){quantities=0;break;}
            }
            if(!quantities)break;
            total+=quantity;
        }
        if(!quantities)c->incomplete=1;
        for(uint32_t i=0;i<v->count;i++){
            const WFNode *n=v->nodes+i;
            if(!wu_available(n)||!(n->flags&WF_CLICKABLE))continue;
            int add=n->ref>=17u&&n->ref<=193u&&n->ref%16u==1u;
            if(add&&(!quantities||total>=UINT32_MAX))continue;
            if(simple(c,WF_CLICK,n->ref,n->ref))return -1;
        }
        return 1;
    }
    if(clicks(v,c))return -1;
    if(task!=2)return 1;
    const WFNode *n=wu_node(v,1);
    if(focused(n)&&(insertion(c,n->ref,0,room(v,n,c,127,0))||
                   editing_keys(c,n,WF_SELECT_ALL)))return -1;
    return 1;
}

static int typed_inputs(uint32_t task,const WFView *v,WUCapabilities *c){
    if(task==2){
        for(uint32_t i=0;i<v->count;i++){
            const WFNode *n=v->nodes+i;
            if(n->role==WF_BUTTON&&wu_available(n)&&(n->flags&WF_CLICKABLE)&&
               simple(c,WF_CLICK,n->ref,n->ref))return -1;
        }
        return 1;
    }
    const WFNode *n=wu_node(v,1),*submit=wu_node(v,2);
    if(wu_available(submit)&&simple(c,WF_CLICK,2,2))return -1;
    /* Whole-value replacement needs neither focus nor a field-click event;
     * the latter is explicitly translated to Wait by date/time Wire. */
    if(editable(n)){
        if(!n->capacity)c->incomplete=1;
        else if(insertion(c,n->ref,n->ref,lesser(n->capacity,task==0?10u:5u)))return -1;
    }
    return 1;
}

static int composite_forms(uint32_t task,const WFView *v,WUCapabilities *c){
    for(uint32_t i=0;i<v->count;i++){
        const WFNode *n=v->nodes+i;
        if(!wu_available(n))continue;
        if((n->role==WF_CHECKBOX||n->role==WF_RADIO||n->role==WF_BUTTON)&&
           (n->flags&WF_CLICKABLE)&&simple(c,WF_CLICK,n->ref,n->ref))return -1;
        if(editable(n)&&task!=0&&task!=2&&
           insertion(c,n->ref,n->ref,room(v,n,c,63,1)))return -1;
    }
    if(task==0||task==2){
        const WFNode *n=wu_node(v,1);
        if(wu_available(n)){
            /* These are static declared widget indices, never goal-derived.
             * The slider uses 0..20 (displayed as -10..10); height has an empty
             * index 0 and six options. Existing height views omit the options. */
            if(task==2){
                uint32_t options=0;
                for(uint32_t i=0;i<v->count;i++)
                    if(v->nodes[i].parent==n->ref&&v->nodes[i].role==WF_OPTION)options++;
                if(options<7u)c->incomplete=1;
            }
            if(parameter(c,WF_SELECT_OPTION,n->ref,n->ref,0,
                         task==0?20u:6u,WU_UNIT_INDEX))return -1;
        }
    }
    return 1;
}

int wu_caps_text(const WFFamily *family,uint32_t task,const WFView *v,
                 WUCapabilities *c){
    if(!family||!family->family||!v||!c||task>=family->task_count||
       v->count>WF_MAX_NODES)return -1;
    if(!strcmp(family->family,"forms"))return task<11?forms(v,c):-1;
    if(!strcmp(family->family,"autocomplete"))return task<2?autocomplete(v,c):-1;
    if(!strcmp(family->family,"editing"))return task<5?editing(task,v,c):-1;
    if(!strcmp(family->family,"email"))return task<10?email(v,c):-1;
    if(!strcmp(family->family,"catalog"))return task<3?catalog(task,v,c):-1;
    if(!strcmp(family->family,"typed_inputs"))return task<3?typed_inputs(task,v,c):-1;
    if(!strcmp(family->family,"composite_forms"))return task<5?composite_forms(task,v,c):-1;
    return 0;
}
