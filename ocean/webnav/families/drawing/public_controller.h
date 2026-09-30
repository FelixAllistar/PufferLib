#ifndef WEBNAV_DRAWING_PUBLIC_CONTROLLER_H
#define WEBNAV_DRAWING_PUBLIC_CONTROLLER_H
#include "../common/loader.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int drawing_public_action(const WFView *v,WFAction *a){
    const WFNode *canvas=NULL,*dot=NULL;
    for(unsigned i=0;i<v->count;i++){
        if(v->nodes[i].role==WF_CANVAS)canvas=v->nodes+i;
        if(v->nodes[i].role==WF_OTHER&&v->nodes[i].parent==1)dot=v->nodes+i;
    }
    if(!canvas||!dot)return -1;
    unsigned active,count;if(sscanf(wf_text_get(v,canvas->value),"active=%u samples=%u;",&active,&count)!=2)return -1;
    const char *q=wf_text_get(v,v->instruction);
    int circle=strstr(q,"circle")!=NULL,horizontal=strstr(q,"horizontal")!=NULL;
    unsigned needed=circle?33:3;
    if(count&&!active){*a=(WFAction){.kind=WF_CLICK,.target=2};return 0;}
    if(active&&count>=needed){*a=(WFAction){.kind=WF_POINTER_UP,.target=1};return 0;}
    double x=dot->x+dot->width/2,y=dot->y+dot->height/2;
    if(circle){double theta=count*6.2831853071795864769/32;x+=20*cos(theta);y+=20*sin(theta);}
    else {double delta=count?20.0*(count-1):-20.0;if(horizontal)x+=delta;else y+=delta;}
    *a=(WFAction){.kind=active?WF_POINTER_MOVE:WF_POINTER_DOWN,.target=1,
        .arg0=(unsigned)lround(x*256),.arg1=(unsigned)lround(y*256)};
    return 0;
}
#endif
