#include "capabilities.h"
#include <stdio.h>
#include <string.h>

/* Public affordances only. These helpers never receive a simulation row.
 * WAIT (target/args zero) is supplied by the shared dispatcher. */
static int plain(WUCapabilities *c,uint32_t kind,uint32_t ref,uint32_t wire) {
    return wu_cap_add(c,(WUCapability){.kind=kind,.ref=ref,.wire_target=wire});
}
static int at_ref(const WFView *v,WUCapabilities *c,uint32_t kind,uint32_t ref) {
    const WFNode *n=wu_node(v,ref);
    if(!n){c->incomplete=1;return 0;}
    return wu_available(n)?plain(c,kind,ref,ref):0;
}
static int coords(WUCapabilities *c,uint32_t kind,uint32_t ref,uint32_t wire,
                  uint32_t lo0,uint32_t hi0,uint32_t lo1,uint32_t hi1,
                  uint32_t step,uint32_t unit) {
    return wu_cap_add(c,(WUCapability){.kind=kind,.ref=ref,.wire_target=wire,
        .min0=lo0,.max0=hi0,.step0=step,.unit0=unit,
        .min1=lo1,.max1=hi1,.step1=hi1>lo1?step:0,
        .unit1=hi1>lo1?unit:WU_UNIT_NONE});
}
static int edits(const WFView *v,WUCapabilities *c,const WFNode *n,
                 uint32_t capacity,int replace_selection) {
    const char *value=wf_text_get(v,n->value);
    if(!value||n->value.length>capacity||n->selection_start>n->selection_end||
       n->selection_end>n->value.length)return -1;
    uint32_t retained=n->value.length;
    if(replace_selection)retained-=n->selection_end-n->selection_start;
    uint32_t room=capacity-retained;
    if(room&&wu_cap_add(c,(WUCapability){.kind=WF_INSERT,.ref=n->ref,
        .wire_target=n->ref,.flags=WU_CAP_TEXT|WU_CAP_ASCII,
        .text_capacity=room})<0)return -1;
    for(uint32_t kind=WF_BACKSPACE;kind<=WF_SELECT_ALL;kind++)
        if(plain(c,kind,n->ref,n->ref)<0)return -1;
    return 0;
}

static int numeric(uint32_t task,const WFView *v,WUCapabilities *c) {
    if(task>=9)return -1;
    for(uint32_t i=0;i<v->count;i++) {
        const WFNode *n=v->nodes+i;
        if(!wu_available(n))continue;
        if(n->flags&WF_CLICKABLE) {
            /* Hot/cold canvas CLICK and hover use canvas-local integer px;
             * its public feedback node is also an accepted click target. */
            if(task==4&&n->ref==1) {
                if(coords(c,WF_CLICK,1,1,0,154,0,125,1,WU_UNIT_PIXEL)<0||
                   coords(c,WF_POINTER_MOVE,1,1,0,154,0,125,1,WU_UNIT_PIXEL)<0)
                    return -1;
            }else if(plain(c,WF_CLICK,n->ref,n->ref)<0)return -1;
        }
        if((task==3||task==7||task==8)&&n->role==WF_INPUT&&
           !(n->flags&WF_READONLY)) {
            /* Numeric transport bounds current length + insertion by 23,
             * even when a selection will be replaced in the Bend field. */
            if(edits(v,c,n,23,0)<0)return -1;
            if(task==3&&plain(c,WF_ENTER,n->ref,n->ref)<0)return -1;
        }
    }
    return 1;
}

static int visual_click(uint32_t task,uint32_t ref) {
    switch(task) {
        case 0:return ref==1||(ref>=10&&ref<=13);
        case 1:case 2:return ref==1||(ref>=10&&ref<=17);
        case 3:return ref==2||(ref>=10&&ref<=21);
        case 4:return ref==1||(ref>=10&&ref<=28);
        /* Count-shape's displayed shapes are not selectable. All five
         * answer buttons, including wrong answers, are advertised. */
        case 5:return ref==1||(ref>=40&&ref<=44);
        case 6:case 7:return ref>=10&&ref<=14;
        case 8:return ref==1||ref==2;
        default:return 0;
    }
}
static int visual(uint32_t task,const WFView *v,WUCapabilities *c) {
    if(task>=9)return -1;
    for(uint32_t i=0;i<v->count;i++) {
        const WFNode *n=v->nodes+i;
        if(!wu_available(n))continue;
        /* Pie choices absent during the delay are not projected targets.
         * Derive availability from the public nodes, never the hidden timer. */
        if((n->flags&WF_CLICKABLE)&&visual_click(task,n->ref)&&
           plain(c,WF_CLICK,n->ref,n->ref)<0)return -1;
        /* AdditionWire ignores edit target, but transport requires ref 1.
         * The model edits only the publicly focused field. */
        if(task==8&&n->ref==1&&n->role==WF_INPUT&&
           (n->flags&WF_FOCUSED)&&!(n->flags&WF_READONLY)&&
           edits(v,c,n,64,1)<0)return -1;
    }
    return 1;
}

/* SVG coordinates encode tenths of a page pixel plus 2560. The wire rounds
 * after applying an SVG bounding-box offset. Compute transport-safe inclusive
 * bounds from a public shape's size; this is not a task transition. */
static void svg_bounds(uint32_t offset,uint32_t *lo,uint32_t *hi) {
    int32_t lower=(int32_t)(((offset+9)/10)*10)-(int32_t)offset-5;
    uint32_t upper=((offset+5119)/10)*10-offset+4;
    *lo=lower>0?(uint32_t)lower:0;
    *hi=upper<5119?upper:5119;
}
static int direction_bounds(const WFView *v,uint32_t *lo,uint32_t *hi) {
    const WFNode *n=wu_node(v,1);
    const char *name=n?wf_text_get(v,n->name):NULL;
    if(!n||!name||!(n->width>0&&n->width<=100))return 0;
    uint32_t width=(uint32_t)n->width;
    if(n->width!=(float)width)return 0;
    uint32_t offset;
    if(!strcmp(name,"circle"))offset=width*6;
    else if(!strcmp(name,"rectangle"))offset=width*5;
    else if(!strcmp(name,"triangle"))offset=width*4;
    else return 0;
    svg_bounds(offset,lo,hi);return 1;
}
static int drag(uint32_t task,const WFView *v,WUCapabilities *c) {
    if(task>=10)return -1;
    /* WFView omits held-object and pending-drop state for every drag task.
     * Keep DOWN/MOVE/UP and wrong choices; do not infer held state from private
     * rows or instruction text. A MOVE can be an idle gesture. Shape moves
     * use the intersection of all possible held-shape transport ranges. */
    c->incomplete=1;
    uint32_t count=0,submit=0,lo0=0,hi0=0,lo1=0,hi1=0,unit=WU_UNIT_NONE;
    switch(task) {
        case 0:
            count=2;submit=3;hi0=hi1=511;unit=WU_UNIT_PIXEL;
            /* Box axes encode signed page px + 256. */
            break;
        case 1:case 7:
            count=1;submit=2;lo0=4;hi0=5110;
            (void)direction_bounds(v,&lo0,&hi0);
            lo1=lo0;hi1=hi0;unit=WU_UNIT_FIXED_PIXEL;
            break;
        case 2:
            submit=8;lo0=1;hi0=6;unit=WU_UNIT_INDEX;
            /* arg0 is the requested cube face, not a pointer x coordinate. */
            if(at_ref(v,c,WF_POINTER_DOWN,7)<0)return -1;
            break;
        case 3:case 4:case 8:
            count=task==3?5:task==4?9:4;submit=task==8?5:0;
            lo0=1;hi0=count;unit=WU_UNIT_INDEX;
            /* arg0 is the one-based destination order position. */
            break;
        case 5:case 6:
            count=task==5?4:5;submit=8;
            /* Public shape kinds permit offsets 204, 85, 68 (size 17).
             * Held ref is missing, so [1,5116] is legal for all of them,
             * and for the unquantized no-held case. */
            lo0=lo1=1;hi0=hi1=5116;unit=WU_UNIT_FIXED_PIXEL;
            break;
        case 9:
            count=1;submit=2;lo0=lo1=10;hi0=hi1=256;unit=WU_UNIT_PIXEL;
            /* args are resulting textarea width/height, not page x/y. */
            break;
    }
    for(uint32_t ref=1;ref<=count;ref++)
        if(at_ref(v,c,WF_POINTER_DOWN,ref)<0)return -1;
    if(submit&&at_ref(v,c,WF_CLICK,submit)<0)return -1;
    if(coords(c,WF_POINTER_MOVE,0,0,lo0,hi0,lo1,hi1,1,unit)<0||
       plain(c,WF_POINTER_UP,0,0)<0)return -1;
    return 1;
}

static int drawing(uint32_t task,const WFView *v,WUCapabilities *c) {
    if(task>=2)return -1;
    const WFNode *canvas=wu_node(v,1);
    if(!canvas){c->incomplete=1;return 1;}
    if(!wu_available(canvas))return 1;
    uint32_t active=0,samples=0;int end=0;
    const char *state=wf_text_get(v,canvas->value);
    int known=state&&sscanf(state,"active=%u samples=%u;%n",&active,&samples,&end)==2&&
        end>0&&active<=1&&samples<=256;
    if(!known)c->incomplete=1;
    /* This parses the explicit public gesture-state prefix, not instructions
     * or an answer. With missing gesture state only UP is guaranteed legal:
     * DOWN/Submit may be rejected while active, MOVE at a full stroke. */
    if(known&&!active) {
        if(coords(c,WF_POINTER_DOWN,1,1,0,150*256,0,110*256,
                  256,WU_UNIT_FIXED_PIXEL)<0||at_ref(v,c,WF_CLICK,2)<0)return -1;
    }
    if(known&&(!active||samples<256)&&
       coords(c,WF_POINTER_MOVE,1,1,0,150*256,0,110*256,
              256,WU_UNIT_FIXED_PIXEL)<0)return -1;
    /* Accepted coordinates are 1/256 canvas-local px. Wire.decode integer
     * divides by 256, so the effective grid is one pixel (step 256). */
    return plain(c,WF_POINTER_UP,1,1)<0?-1:1;
}

int wu_caps_pointer(const WFFamily *f,uint32_t task,const WFView *v,WUCapabilities *c) {
    if(!f||!f->family||!v||!c)return -1;
    if(strcmp(f->family,"drag")&&strcmp(f->family,"drawing")&&
       strcmp(f->family,"numeric")&&strcmp(f->family,"visual"))return 0;
    if(task>=f->task_count||v->version!=WF_ABI_VERSION||v->count>WF_MAX_NODES)return -1;
    if(!strcmp(f->family,"drag"))return drag(task,v,c);
    if(!strcmp(f->family,"drawing"))return drawing(task,v,c);
    if(!strcmp(f->family,"numeric"))return numeric(task,v,c);
    return visual(task,v,c);
}
