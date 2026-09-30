#ifndef WEBNAV_GEOMETRY_PUBLIC_CONTROLLER_H
#define WEBNAV_GEOMETRY_PUBLIC_CONTROLLER_H
#include "../common/family_api.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const WFNode *geometry_node(const WFView *v,unsigned ref){
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return v->nodes+i;
    return NULL;
}
static double geometry_cx(const WFNode *n){return (double)n->x+n->width/2.0;}
static double geometry_cy(const WFNode *n){return (double)n->y+n->height/2.0;}
static int geometry_point(double x,double y,WFAction *a){
    if(!isfinite(x)||!isfinite(y)||x<2.0||x>150.0||y<2.0||y>130.0)return -1;
    *a=(WFAction){.kind=WF_CLICK,.target=1u,
        .arg0=(uint32_t)lround(x*256.0),.arg1=(uint32_t)lround(y*256.0)};
    return 0;
}
/* phase advances only after an applied action; this policy sees WFView only. */
static int geometry_public_next(const WFView *v,unsigned task,unsigned phase,
                                WFAction *a){
    if(!v||!a||task>=5u)return -1;
    if(task==3u){
        const char *q=wf_text_get(v,v->instruction),*start=q?strchr(q,'('):NULL;
        int x,y;if(!start||sscanf(start,"(%d,%d)",&x,&y)!=2)return -1;
        char label[24];snprintf(label,sizeof label,"(%d,%d)",x,y);
        for(unsigned i=0;i<v->count;i++){
            const char *name=wf_text_get(v,v->nodes[i].name);
            if(name&&!strcmp(name,label)){
                *a=(WFAction){.kind=WF_CLICK,.target=v->nodes[i].ref};return 0;
            }
        }
        return -1;
    }
    if(phase){*a=(WFAction){.kind=WF_CLICK,.target=2u};return 0;}
    const WFNode *p=geometry_node(v,10u),*q=geometry_node(v,11u);
    if(!p)return -1;
    double px=geometry_cx(p),py=geometry_cy(p);
    if(task==1u)return geometry_point(px+0.5,py+0.5,a);
    if(!q)return -1;
    double qx=geometry_cx(q),qy=geometry_cy(q);
    if(task==2u)return geometry_point((px+qx)/2.0,(py+qy)/2.0,a);
    if(task==4u){
        double dx=px-qx,dy=py-qy,length=hypot(dx,dy);
        if(length<1.0)return -1;
        double x=qx+dy*20.0/length-0.5,y=qy-dx*20.0/length-0.5;
        if(geometry_point(x,y,a)==0)return 0;
        return geometry_point(qx-dy*20.0/length-0.5,
                              qy+dx*20.0/length-0.5,a);
    }
    const WFNode *vertex=geometry_node(v,12u);if(!vertex)return -1;
    double vx=geometry_cx(vertex),vy=geometry_cy(vertex);
    double ax=px-vx,ay=py-vy,bx=qx-vx,by=qy-vy;
    double al=hypot(ax,ay),bl=hypot(bx,by);if(al<1.0||bl<1.0)return -1;
    double sx=ax/al+bx/bl,sy=ay/al+by/bl,sl=hypot(sx,sy);
    if(sl<0.01)return -1;
    return geometry_point(vx+sx*35.0/sl,vy+sy*35.0/sl,a);
}
#endif
