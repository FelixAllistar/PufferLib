#include "../common/family_api.h"
#include <stdio.h>
#include <string.h>

#define ROW 1024u
#define LANES 4u
#define QUERY 256u
void family_drag_batch(uint32_t *words);
static const char *const tasks[]={"drag-box","drag-circle","drag-cube",
    "drag-items","drag-items-grid","drag-shapes","drag-shapes-2",
    "drag-single-shape","drag-sort-numbers","resize-textarea"};
static const char *const shape_names[]={"circle","rectangle","triangle"};
static const char *const colors[]={"red","green","blue","aqua","black",
    "magenta","yellow","brown"};
static const char *const circle_colors[]={"blue","red","black","green",
    "yellow","orange","white","brown"};
static const char *const rectangle_colors[]={"blue","red","black","green",
    "yellow","orange","white","cyan"};
static const char *const triangle_colors[]={"blue","red","black","green",
    "yellow","orange","white","purple"};

static int ascii(const uint32_t *p,unsigned cap){
    for(unsigned i=0;i<cap;i++){
        if(!p[i])return 1;
        if(p[i]<32||p[i]>126)return 0;
    }
    return 0;
}
static int valid_sort(const uint32_t *r,unsigned task){
    unsigned n=task==3?5u:task==4?9u:4u;
    if(r[32]!=n||r[33]>n||r[34]>n)return -1;
    if(task==8){if(r[35]||r[36])return -1;}
    else if(r[35]<1||r[35]>n||r[36]<1||r[36]>n)return -1;
    unsigned seen=0;
    for(unsigned i=0;i<n;i++){
        unsigned id=r[37+i];if(id<1||id>n||(seen&(1u<<id)))return -1;
        seen|=1u<<id;
        if(task==8){if(r[64+i]>199)return -1;}
        else if(!ascii(r+384+i*64,64))return -1;
    }
    return 0;
}
static int valid_shapes(const uint32_t *r,unsigned task){
    unsigned n=task==5?4u:5u;
    if(r[32]<(task==5?4u:4u)||r[32]>(task==5?69u:79u)||
       r[33]<4||r[33]>(task==5?84u:69u)||
       r[34]>(task==5?0u:1u)||r[35]>(r[34]?6u:2u)||
       r[36]>n||r[37]!=n)return -1;
    for(unsigned i=0;i<n;i++){
        const uint32_t *p=r+64+i*4;
        if(p[0]>2||p[1]>6||p[2]>5119||p[3]>5119)return -1;
    }
    return 0;
}
static int valid(const uint32_t *r){
    if(!r||r[WF_VERSION]!=WF_ABI_VERSION||r[WF_TASK]>=10||
       r[WF_OP]>WF_STEP)return -1;
    if(r[WF_OP]==WF_RESET)return 0;
    unsigned task=r[WF_TASK];
    unsigned deadline=task==2||task==5?15000u:task==6?30000u:10000u;
    if(r[WF_STATUS]>WF_TIMEOUT||r[WF_DEADLINE]!=deadline||
       r[WF_ELAPSED]>deadline||!ascii(r+QUERY,128))return -1;
    switch(task){
      case 0:
        return r[32]>511||r[33]>511||r[34]>511||r[35]>511||
            r[36]>2?-1:0;
      case 1:case 7:
        return r[32]>5119||r[33]>5119||r[34]>5119||r[35]>5119||
            r[36]>3||r[37]>1||r[38]>2||r[39]<13||r[39]>39||
            r[40]>(task==1?6u:7u)?-1:0;
      case 2: return r[32]<1||r[32]>6||r[33]<1||r[33]>6||r[34]>1?-1:0;
      case 3:case 4:case 8: return valid_sort(r,task);
      case 5:case 6: return valid_shapes(r,task);
      case 9:
        return r[32]<10||r[32]>256||r[33]<10||r[33]>256||
            r[34]<50||r[34]>95||r[35]<50||r[35]>70||
            r[36]<5||r[36]>35||r[37]<5||r[37]>35||
            r[38]>1||r[39]>1||r[40]>1?-1:0;
    }
    return -1;
}
static int add_ascii(WFView *v,const uint32_t *p,unsigned cap,WFText *out){
    char s[129];unsigned n=0;
    while(n<cap&&p[n]){s[n]=(char)p[n];n++;}
    return wf_text_add(v,s,n,out);
}
static int add_node(WFView *v,unsigned ref,unsigned role,unsigned flags,
                    const char *name,const char *value,
                    float x,float y,float w,float h){
    if(v->count==WF_MAX_NODES){v->omitted++;return -1;}
    WFNode *n=&v->nodes[v->count++];
    n->ref=ref;n->role=role;n->flags=flags;
    n->x=x;n->y=y;n->width=w;n->height=h;
    return wf_text_add(v,name,strlen(name),&n->name)||
           wf_text_add(v,value,strlen(value),&n->value)?-1:0;
}
static int observe_sort(const uint32_t *r,WFView *v,unsigned task){
    unsigned n=r[32];
    for(unsigned i=0;i<n;i++){
        unsigned id=r[37+i];char name[65];
        if(task==8)snprintf(name,sizeof name,"%d",(int)r[64+id-1]-100);
        else{
            unsigned k=0;const uint32_t *p=r+384+(id-1)*64;
            while(k<64&&p[k]){name[k]=(char)p[k];k++;}name[k]=0;
        }
        float x=task==4?(float)(i%3)*43.0f:3.0f;
        float y=task==4?(float)(i/3)*43.0f:(float)i*36.0f;
        if(add_node(v,id,WF_OTHER,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                    name,"",x,y,task==4?40:140,task==4?40:33))return -1;
    }
    if(task==8&&add_node(v,5,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                         "Submit","",0,160,70,24))return -1;
    return 0;
}
static int observe_shapes(const uint32_t *r,WFView *v,unsigned task){
    unsigned count=r[37];
    for(unsigned i=0;i<count;i++){
        const uint32_t *p=r+64+i*4;char name[48];
        snprintf(name,sizeof name,"%s %s",colors[p[1]],shape_names[p[0]]);
        if(add_node(v,i+1,WF_OTHER,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                    name,"",((int)p[2]-2560)/10.0f,
                    ((int)p[3]-2560)/10.0f,17,17))return -1;
    }
    if(task==5){
        if(add_node(v,6,WF_OTHER,WF_VISIBLE,"black box","",
                    (float)r[32],(float)r[33],80,45))return -1;
    }else{
        if(add_node(v,6,WF_OTHER,WF_VISIBLE,"left box","",2,5,70,50)||
           add_node(v,7,WF_OTHER,WF_VISIBLE,"right box","",84,5,70,50))return -1;
    }
    return add_node(v,8,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                    "Submit","",0,125,70,24);
}
static int observe(const uint32_t *r,WFView *v){
    if(valid(r)||r[WF_OP]==WF_RESET||!v)return -1;
    wf_view_init(v,r[WF_ELAPSED],r[WF_DEADLINE]);
    if(add_ascii(v,r+QUERY,128,&v->instruction))return -1;
    unsigned task=r[WF_TASK];
    switch(task){
      case 0:
        return add_node(v,1,WF_OTHER,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                 "s","",(float)((int)r[32]-256),(float)((int)r[33]-256),28,28)||
               add_node(v,2,WF_OTHER,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                 "L","",(float)((int)r[34]-256),(float)((int)r[35]-256),58,58)||
               add_node(v,3,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                 "Submit","",0,110,70,24)?-1:0;
      case 1:case 7:{
        unsigned kind=r[38];float size=kind==0?(float)r[39]*2:(float)r[39];
        const char *color=kind==0?circle_colors[r[40]]:
            kind==1?rectangle_colors[r[40]]:triangle_colors[r[40]];
        if(add_node(v,1,WF_OTHER,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                    shape_names[kind],color,
                    ((int)r[34]-2560)/10.0f,((int)r[35]-2560)/10.0f,
                    size,size)||
           add_node(v,2,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                    "Submit","",0,135,70,24))return -1;
        return 0;
      }
      case 2:
        for(unsigned i=1;i<=6;i++){
            char name[8];snprintf(name,sizeof name,"%u",i);
            if(add_node(v,i,WF_OTHER,WF_VISIBLE|WF_ENABLED|
                        (r[33]==i?WF_SELECTED:0),name,"",0,0,80,80))return -1;
        }
        return add_node(v,7,WF_CANVAS,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                   "cube","",40,30,80,80)||
               add_node(v,8,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                   "Submit","",30,140,70,24)?-1:0;
      case 3:case 4:case 8: return observe_sort(r,v,task);
      case 5:case 6: return observe_shapes(r,v,task);
      case 9:
        return add_node(v,1,WF_TEXTAREA,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                   "textarea","",(float)r[36],(float)r[37],
                   (float)r[32],(float)r[33])||
               add_node(v,2,WF_BUTTON,WF_VISIBLE|WF_ENABLED|WF_CLICKABLE,
                   "Submit","",0,140,70,24)?-1:0;
    }
    return -1;
}
static unsigned svg_offset(unsigned kind,unsigned size){
    return kind==0?size*12u:kind==1?size*5u:size*4u;
}
static int svg_axis_valid(unsigned coordinate,unsigned offset){
    if(coordinate>5119)return 0;
    uint64_t rounded=((uint64_t)coordinate+offset+5u)/10u*10u;
    return rounded>=offset&&rounded-offset<=5119u;
}
static int valid_action(const uint32_t *r,const WFAction *a){
    unsigned task=r[WF_TASK],kind=a->kind,target=a->target;
    if(kind==WF_WAIT)return 0;
    if(task==0){
        if(kind==WF_CLICK)return target==3?0:-1;
        if(kind==WF_POINTER_DOWN)return target>=1&&target<=2?0:-1;
        if(kind==WF_POINTER_MOVE)return !target&&a->arg0<=511&&a->arg1<=511?0:-1;
        return kind==WF_POINTER_UP&&!target?0:-1;
    }
    if(task==1||task==7){
        if(kind==WF_CLICK)return target==2?0:-1;
        if(kind==WF_POINTER_DOWN)return target==1?0:-1;
        if(kind==WF_POINTER_MOVE){
            unsigned offset=svg_offset(r[38],r[39]);
            return !target&&svg_axis_valid(a->arg0,offset)&&
                svg_axis_valid(a->arg1,offset)?0:-1;
        }
        return kind==WF_POINTER_UP&&!target?0:-1;
    }
    if(task==2){
        if(kind==WF_CLICK)return target==8?0:-1;
        if(kind==WF_POINTER_DOWN)return target==7?0:-1;
        if(kind==WF_POINTER_MOVE)return !target&&a->arg0>=1&&a->arg0<=6?0:-1;
        return kind==WF_POINTER_UP&&!target?0:-1;
    }
    if(task==3||task==4||task==8){
        unsigned n=r[32];
        if(kind==WF_CLICK)return task==8&&target==5?0:-1;
        if(kind==WF_POINTER_DOWN)return target>=1&&target<=n?0:-1;
        if(kind==WF_POINTER_MOVE)return !target&&a->arg0>=1&&a->arg0<=n?0:-1;
        return kind==WF_POINTER_UP&&!target?0:-1;
    }
    if(task==5||task==6){
        if(kind==WF_CLICK)return target==8?0:-1;
        if(kind==WF_POINTER_DOWN)return target>=1&&target<=r[37]?0:-1;
        if(kind==WF_POINTER_MOVE){
            if(target||a->arg0>5119||a->arg1>5119)return -1;
            if(!r[36])return 0; /* no held shape, hence no quantization */
            unsigned offset=svg_offset(r[64+(r[36]-1)*4],17);
            return svg_axis_valid(a->arg0,offset)&&
                svg_axis_valid(a->arg1,offset)?0:-1;
        }
        return kind==WF_POINTER_UP&&!target?0:-1;
    }
    if(task==9){
        if(kind==WF_CLICK)return target==2?0:-1;
        if(kind==WF_POINTER_DOWN)return target==1?0:-1;
        if(kind==WF_POINTER_MOVE)return !target&&a->arg0>=10&&a->arg0<=256&&
            a->arg1>=10&&a->arg1<=256?0:-1;
        return kind==WF_POINTER_UP&&!target?0:-1;
    }
    return -1;
}
static int action(uint32_t *r,const WFAction *a){
    if(valid(r)||!a||r[WF_STATUS]!=WF_RUNNING||
       a->elapsed_ms<r[WF_ELAPSED]||a->elapsed_ms>r[WF_DEADLINE]||
       a->text_length||valid_action(r,a))return -1;
    r[WF_OP]=WF_STEP;r[WF_ACTION]=a->kind;r[WF_TARGET]=a->target;
    r[WF_ARG0]=a->arg0;r[WF_ARG1]=a->arg1;r[WF_ELAPSED]=a->elapsed_ms;
    return 0;
}
static const WFFamily api={WF_ABI_VERSION,ROW,LANES,10,"drag",tasks,
                           valid,family_drag_batch,observe,action};
WF_EXPORT const WFFamily *webnav_family_v2(void){return &api;}
