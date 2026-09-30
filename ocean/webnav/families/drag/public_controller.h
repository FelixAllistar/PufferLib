#ifndef WEBNAV_DRAG_PUBLIC_CONTROLLER_H
#define WEBNAV_DRAG_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Only WFView data and this controller's prior actions are used. Coordinates
   are normalized gesture targets, with signed pixels encoded by an offset. */
typedef struct {
    unsigned task,phase,index,held,goal_position;
    uint32_t x,y;
} DragPublic;

static const WFNode *drag_node(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static const char *drag_name(const WFView *v,const WFNode *n){
    return n?wf_text_get(v,n->name):NULL;
}
static uint32_t drag_coordinate(float x,unsigned scale,unsigned bias){
    float value=x*(float)scale+(float)bias;
    return (uint32_t)(value+0.5f);
}
static void drag_command(WFAction *a,unsigned kind,unsigned target,
                         unsigned arg0,unsigned arg1,unsigned now){
    *a=(WFAction){0};a->kind=kind;a->target=target;
    a->arg0=arg0;a->arg1=arg1;a->elapsed_ms=now;
}
static unsigned drag_direction(const char *q){
    if(strstr(q," left "))return 0;
    if(strstr(q," right "))return 1;
    if(strstr(q," up "))return 2;
    if(strstr(q," down "))return 3;
    return 4;
}
static unsigned drag_grid_position(const char *q){
    static const char *const phrases[]={"to the top left.","to the top center.",
        "to the top right.","to the center left.","to the center.",
        "to the center right.","to the bottom left.",
        "to the bottom center.","to the bottom right."};
    for(unsigned i=0;i<9;i++)if(strstr(q,phrases[i]))return i+1;
    return 0;
}
static unsigned drag_ordinal_position(const char *q){
    const char *p=strstr(q," to the ");
    if(!p)return 0;
    p+=8;return isdigit((unsigned char)*p)?(unsigned)strtoul(p,NULL,10):0;
}
static unsigned drag_items_goal(unsigned task,const char *q,unsigned ix){
    if(task==3){
        if(strstr(q,"to the top."))return 1;
        if(strstr(q,"to the bottom."))return 5;
        if(strstr(q,"down by one position."))return ix+1;
        if(strstr(q,"up by one position."))return ix-1;
        return drag_ordinal_position(q);
    }
    if(strstr(q,"up by one."))return ix-3;
    if(strstr(q,"down by one."))return ix+3;
    if(strstr(q,"left by one."))return ix-1;
    if(strstr(q,"right by one."))return ix+1;
    return drag_grid_position(q);
}
static int drag_target_name(const char *q,char *out,size_t cap){
    if(strncmp(q,"Drag ",5))return -1;
    const char *end=strstr(q+5," to ");
    static const char *const relative[]={" up by "," down by ",
        " left by "," right by "};
    for(unsigned i=0;i<4;i++){
        const char *p=strstr(q+5,relative[i]);
        if(p&&(!end||p<end))end=p;
    }
    if(!end)return -1;
    size_t n=(size_t)(end-(q+5));if(!n||n>=cap)return -1;
    memcpy(out,q+5,n);out[n]=0;return 0;
}
static int drag_shape_target(const char *q,char *out,size_t cap){
    const char *start=strstr(q,"Drag all ");if(!start)return -1;start+=9;
    const char *end=strstr(start," into ");if(!end)return -1;
    size_t n=(size_t)(end-start);if(!n||n>=cap)return -1;
    memcpy(out,start,n);out[n]=0;
    if(n>7&&!strcmp(out+n-7," shapes"))out[n-7]=0;
    return 0;
}
static int drag_shape_matches(const char *name,const char *target){
    const char *space=strchr(name,' ');if(!space)return 0;
    if(!strcmp(target,"circles"))return !strcmp(space+1,"circle");
    if(!strcmp(target,"rectangles"))return !strcmp(space+1,"rectangle");
    if(!strcmp(target,"triangles"))return !strcmp(space+1,"triangle");
    return strlen(target)==(size_t)(space-name)&&!strncmp(name,target,space-name);
}
static int drag_public_box(DragPublic *c,const WFView *v,unsigned now,WFAction *a){
    const WFNode *small=drag_node(v,1),*large=drag_node(v,2);
    if(!small||!large)return -1;
    switch(c->phase++){
      case 0: drag_command(a,WF_POINTER_DOWN,1,0,0,now);return 0;
      case 1:
        drag_command(a,WF_POINTER_MOVE,0,
          drag_coordinate(large->x+5,1,256),
          drag_coordinate(large->y+5,1,256),now);return 0;
      case 2: drag_command(a,WF_POINTER_UP,0,0,0,now);return 0;
      default: drag_command(a,WF_CLICK,3,0,0,now);return 0;
    }
}
static int drag_public_direction(DragPublic *c,const WFView *v,
                                 const char *q,unsigned now,WFAction *a){
    const WFNode *item=drag_node(v,1);if(!item)return -1;
    unsigned direction=drag_direction(q);if(direction>3)return -1;
    switch(c->phase++){
      case 0: drag_command(a,WF_POINTER_DOWN,1,0,0,now);return 0;
      case 1:{
        float x=item->x,y=item->y;
        if(direction==0)x-=10;else if(direction==1)x+=10;
        else if(direction==2)y-=10;else y+=10;
        drag_command(a,WF_POINTER_MOVE,0,drag_coordinate(x,10,2560),
                     drag_coordinate(y,10,2560),now);return 0;
      }
      case 2: drag_command(a,WF_POINTER_UP,0,0,0,now);return 0;
      default: drag_command(a,WF_CLICK,2,0,0,now);return 0;
    }
}
static int drag_public_cube(DragPublic *c,const WFView *v,
                            const char *q,unsigned now,WFAction *a){
    const char *quote=strchr(q,'"');if(!quote||quote[1]<'1'||quote[1]>'6')return -1;
    unsigned goal=(unsigned)(quote[1]-'0');
    const WFNode *active=drag_node(v,goal);if(!active)return -1;
    if(!c->phase&&(active->flags&WF_SELECTED)){
        drag_command(a,WF_CLICK,8,0,0,now);return 0;
    }
    switch(c->phase++){
      case 0: drag_command(a,WF_POINTER_DOWN,7,0,0,now);return 0;
      case 1: drag_command(a,WF_POINTER_MOVE,0,goal,0,now);return 0;
      case 2: drag_command(a,WF_POINTER_UP,0,0,0,now);return 0;
      default: drag_command(a,WF_CLICK,8,0,0,now);return 0;
    }
}
static int drag_public_items(DragPublic *c,const WFView *v,
                             const char *q,unsigned now,WFAction *a){
    if(!c->phase){
        char target[80];if(drag_target_name(q,target,sizeof target))return -1;
        unsigned ix=0;
        for(unsigned i=0;i<v->count;i++){
            const char *name=drag_name(v,v->nodes+i);
            if(name&&!strcmp(name,target)){
                c->held=v->nodes[i].ref;ix=i+1;break;
            }
        }
        if(!c->held)return -1;
        c->goal_position=drag_items_goal(c->task,q,ix);
        if(!c->goal_position||c->goal_position>v->count)return -1;
    }
    switch(c->phase++){
      case 0: drag_command(a,WF_POINTER_DOWN,c->held,0,0,now);return 0;
      case 1: drag_command(a,WF_POINTER_MOVE,0,c->goal_position,0,now);return 0;
      default: drag_command(a,WF_POINTER_UP,0,0,0,now);return 0;
    }
}
static int drag_public_shapes(DragPublic *c,const WFView *v,
                              const char *q,unsigned now,WFAction *a){
    char target[48];if(drag_shape_target(q,target,sizeof target))return -1;
    unsigned count=c->task==5?4:5;
    while(c->index<count&&c->phase==0&&c->task==5){
        const char *name=drag_name(v,drag_node(v,c->index+1));
        if(name&&drag_shape_matches(name,target))break;
        c->index++;
    }
    if(c->index==count){drag_command(a,WF_CLICK,8,0,0,now);return 0;}
    const WFNode *item=drag_node(v,c->index+1);
    const char *name=drag_name(v,item);if(!name)return -1;
    int left=drag_shape_matches(name,target);
    const WFNode *box=drag_node(v,c->task==5||left?6:7);
    if(!box)return -1;
    switch(c->phase++){
      case 0: drag_command(a,WF_POINTER_DOWN,item->ref,0,0,now);return 0;
      case 1: drag_command(a,WF_POINTER_MOVE,0,
        drag_coordinate(box->x+10,10,2560),
        drag_coordinate(box->y+10,10,2560),now);return 0;
      default:
        drag_command(a,WF_POINTER_UP,0,0,0,now);
        c->phase=0;c->index++;return 0;
    }
}
static int drag_public_numbers(DragPublic *c,const WFView *v,
                               unsigned now,WFAction *a){
    if(v->count!=5)return -1;
    if(c->phase==0){
        int value[4];
        for(unsigned i=0;i<4;i++){
            const char *name=drag_name(v,v->nodes+i);
            if(!name)return -1;value[i]=(int)strtol(name,NULL,10);
        }
        unsigned from=4,to=0;
        for(unsigned i=0;i<4&&from==4;i++){
            unsigned best=i;
            for(unsigned j=i+1;j<4;j++)if(value[j]<value[best])best=j;
            if(best!=i){from=best;to=i;}
        }
        if(from==4){drag_command(a,WF_CLICK,5,0,0,now);return 0;}
        c->held=v->nodes[from].ref;c->goal_position=to+1;
    }
    switch(c->phase++){
      case 0: drag_command(a,WF_POINTER_DOWN,c->held,0,0,now);return 0;
      case 1: drag_command(a,WF_POINTER_MOVE,0,c->goal_position,0,now);return 0;
      default:
        drag_command(a,WF_POINTER_UP,0,0,0,now);c->phase=0;return 0;
    }
}
static int drag_public_resize(DragPublic *c,const WFView *v,
                              const char *q,unsigned now,WFAction *a){
    const WFNode *field=drag_node(v,1);if(!field)return -1;
    int width=strstr(q,"width")!=NULL,smaller=strstr(q,"smaller")!=NULL;
    switch(c->phase++){
      case 0: drag_command(a,WF_POINTER_DOWN,1,0,0,now);return 0;
      case 1:{
        unsigned w=(unsigned)field->width,h=(unsigned)field->height;
        if(width)w=smaller?w-10:w+10;
        else h=smaller?h-10:h+10;
        drag_command(a,WF_POINTER_MOVE,0,w,h,now);return 0;
      }
      case 2: drag_command(a,WF_POINTER_UP,0,0,0,now);return 0;
      default: drag_command(a,WF_CLICK,2,0,0,now);return 0;
    }
}
static int drag_public_next(DragPublic *c,const WFView *v,
                            unsigned now,WFAction *a){
    if(!c||!v||!a||c->task>=10)return -1;
    const char *q=wf_text_get(v,v->instruction);if(!q)return -1;
    switch(c->task){
      case 0: return drag_public_box(c,v,now,a);
      case 1:case 7: return drag_public_direction(c,v,q,now,a);
      case 2: return drag_public_cube(c,v,q,now,a);
      case 3:case 4: return drag_public_items(c,v,q,now,a);
      case 5:case 6: return drag_public_shapes(c,v,q,now,a);
      case 8: return drag_public_numbers(c,v,now,a);
      case 9: return drag_public_resize(c,v,q,now,a);
    }
    return -1;
}
#endif
