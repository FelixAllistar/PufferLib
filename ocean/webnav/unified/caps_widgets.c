#include "capabilities.h"
#include <string.h>

/* These are widget transport protocols, not instruction/answer parsers. */
static void plain(WUCapabilities *out,uint32_t kind,uint32_t ref,uint32_t wire) {
    wu_cap_add(out,(WUCapability){.kind=kind,.ref=ref,.wire_target=wire});
}

static void arg(WUCapabilities *out,uint32_t kind,uint32_t ref,uint32_t wire,
                uint32_t low,uint32_t high,uint32_t unit) {
    wu_cap_add(out,(WUCapability){.kind=kind,.ref=ref,.wire_target=wire,
        .min0=low,.max0=high,.step0=1,.unit0=unit});
}

static const WFNode *required(const WFView *view,WUCapabilities *out,
                              uint32_t ref,uint32_t role) {
    const WFNode *n=wu_node(view,ref);
    if(!n||n->role!=role) {out->incomplete=1;return NULL;}
    return n;
}

static void click(const WFNode *n,WUCapabilities *out) {
    if(wu_available(n)&&(n->flags&WF_CLICKABLE))
        plain(out,WF_CLICK,n->ref,n->ref);
}

static int text_room(const WFView *view,const WFNode *n,WUCapabilities *out,
                     uint32_t *room) {
    if(!n->capacity||!wf_text_get(view,n->value)||
       n->value.length>n->capacity||n->selection_start>n->selection_end||
       n->selection_end>n->value.length) {
        out->incomplete=1;return 0;
    }
    *room=n->capacity-(n->value.length-(n->selection_end-n->selection_start));
    return 1;
}

static void edits(const WFView *view,const WFNode *n,WUCapabilities *out,
                  int limited,int range) {
    if(!wu_available(n)||(n->flags&WF_READONLY))return;
    uint32_t room;
    if(text_room(view,n,out,&room)&&room)
        wu_cap_add(out,(WUCapability){.kind=WF_INSERT,.ref=n->ref,
            .wire_target=n->ref,.flags=WU_CAP_TEXT|WU_CAP_ASCII,
            .text_capacity=room});
    plain(out,WF_BACKSPACE,n->ref,n->ref);
    if(!limited) {
        for(uint32_t kind=WF_DELETE;kind<=WF_END;kind++)
            plain(out,kind,n->ref,n->ref);
    }
    plain(out,WF_SELECT_ALL,n->ref,n->ref);
    if(range) {
        if(!wf_text_get(view,n->value)) {out->incomplete=1;return;}
        wu_cap_add(out,(WUCapability){.kind=WF_SELECT_RANGE,.ref=n->ref,
            .wire_target=n->ref,.flags=WU_CAP_RANGE,
            .max0=n->value.length,.step0=1,.unit0=WU_UNIT_TEXT_OFFSET,
            .max1=n->value.length,.step1=1,.unit1=WU_UNIT_TEXT_OFFSET});
    }
}

static void controls(uint32_t task,const WFView *view,WUCapabilities *out) {
    if(task==0) {
        const WFNode *select=required(view,out,1,WF_SELECT);
        unsigned options=0;
        for(uint32_t i=0;i<view->count;i++) {
            const WFNode *n=&view->nodes[i];
            if(n->role!=WF_OPTION||n->parent!=1)continue;
            /* The static choose-list catalog is zero based, refs start at 10. */
            if(n->ref<10||n->ref>18) {out->incomplete=1;continue;}
            options++;
            if(wu_available(select)&&wu_available(n))
                arg(out,WF_SELECT_OPTION,n->ref,1,n->ref-10,n->ref-10,WU_UNIT_INDEX);
        }
        if(options<3)out->incomplete=1;
        click(required(view,out,2,WF_BUTTON),out);
    } else if(task==1||task==2) {
        uint32_t count=task==1?1:3;
        /* WFView lacks the dragged slider ref. MOVE remains a potential
         * gesture continuation; the public contract cannot gate it exactly. */
        out->incomplete=1;
        for(uint32_t ref=1;ref<=count;ref++) {
            const WFNode *n=required(view,out,ref,WF_SLIDER);
            if(!wu_available(n))continue;
            arg(out,WF_CLICK,ref,ref,0,1000,WU_UNIT_NORMALIZED);
            arg(out,WF_POINTER_DOWN,ref,ref,0,1000,WU_UNIT_NORMALIZED);
            arg(out,WF_POINTER_MOVE,ref,ref,0,1000,WU_UNIT_NORMALIZED);
            plain(out,WF_POINTER_UP,ref,ref);
            plain(out,WF_LEFT,ref,ref);
            plain(out,WF_RIGHT,ref,ref);
            plain(out,WF_HOME,ref,ref);
            plain(out,WF_END,ref,ref);
            arg(out,WF_KEY_DOWN,ref,ref,35,40,WU_UNIT_KEY);
            /* KEY_UP is accepted by C but Wire decodes it to Wait. */
        }
        click(required(view,out,count+1,WF_BUTTON),out);
    } else if(task==3) {
        for(uint32_t ref=1;ref<=3;ref++)click(required(view,out,ref,WF_BUTTON),out);
        /* Spinner KEY_DOWN only prevents browser default; Wire is Wait. */
    } else {
        const WFNode *n=required(view,out,1,WF_INPUT);
        click(n,out);
        if(n)edits(view,n,out,0,1);
        click(required(view,out,2,WF_BUTTON),out);
    }
}

static void scroll(uint32_t task,const WFView *view,WUCapabilities *out) {
    const WFNode *area=required(view,out,1,task==0?WF_SELECT:WF_TEXTAREA);
    /* The readonly agreement area still accepts scrolling. Wire clamps
     * overshoot; expose all effective positions, in absolute CSS pixels. */
    if(area&&(area->flags&WF_VISIBLE)) {
        float high=area->scroll_max_y;
        if(!(high>=0&&high<=6000)||high!=(float)(uint32_t)high)
            out->incomplete=1;
        else arg(out,WF_SCROLL,1,1,0,(uint32_t)high,WU_UNIT_PIXEL);
    }
    if(task==0) {
        unsigned options=0;
        for(uint32_t i=0;i<view->count;i++) {
            const WFNode *n=&view->nodes[i];
            if(n->role!=WF_OPTION||n->parent!=1)continue;
            if(n->ref<2||n->ref>12) {out->incomplete=1;continue;}
            options++;
            if(wu_available(n)&&(n->flags&WF_VISIBLE))
                arg(out,WF_SELECT_OPTION,n->ref,n->ref,0,1,WU_UNIT_INDEX);
        }
        if(options<8)out->incomplete=1;
        click(required(view,out,16,WF_BUTTON),out);
    } else if(task==2) {
        click(required(view,out,2,WF_BUTTON),out);
    } else {
        const WFNode *n=required(view,out,2,WF_INPUT);
        if(n)edits(view,n,out,0,0);
        /* Clicking the answer/name input is accepted but decodes to Wait. */
        click(required(view,out,3,WF_BUTTON),out);
        if(task==3)click(required(view,out,4,WF_BUTTON),out);
    }
}

static void travel(uint32_t task,const WFView *view,WUCapabilities *out) {
    if(task==2) {
        for(uint32_t ref=1;ref<=4;ref++)click(required(view,out,ref,WF_BUTTON),out);
        return;
    }
    const WFNode *from=wu_node(view,1),*back=wu_node(view,5);
    if(from) {
        from=required(view,out,1,WF_INPUT);
        const WFNode *to=required(view,out,2,WF_INPUT);
        const WFNode *date=required(view,out,3,WF_INPUT);
        /* Blank and invalid airports are legal wrong edits. The public
         * airport options have no parent, so the same option cannot supply a
         * unique (kind, ref) mapping to both fields. Use the field protocols. */
        out->incomplete=1;
        if(wu_available(from))arg(out,WF_SELECT_OPTION,1,1,0,7,WU_UNIT_INDEX);
        if(wu_available(to))arg(out,WF_SELECT_OPTION,2,2,0,7,WU_UNIT_INDEX);
        if(wu_available(date))arg(out,WF_SELECT_OPTION,3,3,0,92,WU_UNIT_INDEX);
        for(uint32_t ref=11;ref<=16;ref++) {
            required(view,out,ref,WF_OPTION);
        }
        for(uint32_t ref=21;ref<=112;ref++) {
            const WFNode *n=required(view,out,ref,WF_OPTION);
            if(wu_available(date)&&wu_available(n))
                arg(out,WF_SELECT_OPTION,ref,3,ref-20,ref-20,WU_UNIT_INDEX);
        }
        click(required(view,out,4,WF_BUTTON),out);
    } else if(back) {
        click(required(view,out,5,WF_BUTTON),out);
        unsigned flights=0;
        for(uint32_t ref=6;ref<=9;ref++) {
            const WFNode *n=wu_node(view,ref);
            if(!n)continue;
            if(n->role!=WF_BUTTON) {out->incomplete=1;continue;}
            flights++;click(n,out);
        }
        if(flights<3)out->incomplete=1;
    } else out->incomplete=1;
}

static void calendar(uint32_t task,const WFView *view,WUCapabilities *out) {
    if(task<4) {
        click(required(view,out,1,WF_INPUT),out);
        click(required(view,out,2,WF_BUTTON),out);
        for(uint32_t i=0;i<view->count;i++) {
            const WFNode *n=&view->nodes[i];
            if(n->ref>=3&&n->ref<=35&&n->role==WF_BUTTON)click(n,out);
        }
        return;
    }
    const WFNode *area=required(view,out,56,WF_PANEL);
    const WFNode *input=wu_node(view,53),*draft=wu_node(view,52);
    if(wu_available(area))arg(out,WF_SCROLL,56,56,0,40,WU_UNIT_INDEX);
    if(!input) {
        /* A pointer-down without a move has no public draft node. Thus the
         * initial and start-only gesture states have identical WFViews. */
        if(!draft)out->incomplete=1;
        for(uint32_t i=0;i<view->count;i++) {
            const WFNode *n=&view->nodes[i];
            if(n->role!=WF_CELL||n->ref<1||n->ref>48||!wu_available(n))continue;
            if(!draft)plain(out,WF_POINTER_DOWN,n->ref,n->ref);
            plain(out,WF_POINTER_MOVE,n->ref,n->ref);
        }
        /* UP uses the public draft ref, not either time cell or a point. */
        if(draft) {
            if(draft->role!=WF_BUTTON)out->incomplete=1;
            else if(wu_available(draft))plain(out,WF_POINTER_UP,52,52);
        }
    } else {
        if(input->role!=WF_INPUT)out->incomplete=1;
        else edits(view,input,out,1,0);
        click(required(view,out,54,WF_BUTTON),out);
        click(required(view,out,55,WF_BUTTON),out);
    }
}

static void menus(const WFView *view,WUCapabilities *out) {
    for(uint32_t i=0;i<view->count;i++) {
        const WFNode *n=&view->nodes[i];
        if(n->role!=WF_OPTION&&n->role!=WF_BUTTON)continue;
        /* Disabled nodes still update Model hover and can close a branch. */
        if(n->flags&WF_VISIBLE)plain(out,WF_POINTER_MOVE,n->ref,n->ref);
        click(n,out);
    }
}

int wu_caps_widgets(const WFFamily *family,uint32_t task,const WFView *view,
                    WUCapabilities *out) {
    if(!family||!family->family)return -1;
    unsigned group,limit;
    if(!strcmp(family->family,"controls")) {group=0;limit=6;}
    else if(!strcmp(family->family,"scroll")) {group=1;limit=4;}
    else if(!strcmp(family->family,"travel")) {group=2;limit=3;}
    else if(!strcmp(family->family,"calendar")) {group=3;limit=5;}
    else if(!strcmp(family->family,"menus")) {group=4;limit=2;}
    else return 0;
    if(!view||!out||family->abi_version!=WF_ABI_VERSION||
       task>=limit||task>=family->task_count||view->version!=WF_ABI_VERSION||
       view->count>WF_MAX_NODES||view->text_bytes>WF_TEXT_BYTES||
       out->count>WU_MAX_CAPABILITIES)return -1;
    if(view->omitted||view->text_truncated)out->incomplete=1;
    /* Global WAIT (ref/wire target zero) belongs to the shared wrapper. */
    switch(group) {
        case 0:controls(task,view,out);break;
        case 1:scroll(task,view,out);break;
        case 2:travel(task,view,out);break;
        case 3:calendar(task,view,out);break;
        default:menus(view,out);break;
    }
    return 1;
}
