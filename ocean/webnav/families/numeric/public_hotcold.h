#ifndef WEBNAV_NUMERIC_PUBLIC_HOTCOLD_H
#define WEBNAV_NUMERIC_PUBLIC_HOTCOLD_H
#include "../common/family_api.h"
#include <stdint.h>
#include <string.h>
/* Feasible targets inferred solely from public temperature observations.
 * The grid bounds are the fixed original task's documented canvas preset. */
static uint16_t numeric_hotcold_x[17250],numeric_hotcold_y[17250];
static unsigned numeric_hotcold_count,numeric_hotcold_probe_x,numeric_hotcold_probe_y;
static int numeric_hotcold_pending;
static unsigned numeric_hotcold_heat(int x,int y,int px,int py) {
    int dx=x-px,dy=y-py,d=dx*dx+dy*dy;
    return d<25?0:d<100?1:d<1600?2:3;
}
static uint64_t numeric_hotcold_score(unsigned x,unsigned y) {
    unsigned bins[4]={0};
    for(unsigned i=0;i<numeric_hotcold_count;i++)bins[numeric_hotcold_heat(numeric_hotcold_x[i],numeric_hotcold_y[i],x,y)]++;
    uint64_t score=0;
    for(unsigned i=0;i<4;i++)score+=(uint64_t)bins[i]*bins[i];
    return score;
}
static int numeric_hotcold_public_action(const WFView *v,WFAction *a) {
    const WFNode *canvas=NULL;const char *signal="";
    if(!v->elapsed_ms) {
        numeric_hotcold_count=0;numeric_hotcold_pending=0;
        for(unsigned y=5;y<120;y++)for(unsigned x=5;x<155;x++) {
            unsigned i=numeric_hotcold_count++;
            numeric_hotcold_x[i]=(uint16_t)x;numeric_hotcold_y[i]=(uint16_t)y;
        }
    }
    for(unsigned i=0;i<v->count;i++) {
        const WFNode *n=&v->nodes[i];
        if(n->role==WF_CANVAS)canvas=n;
        const char *name=wf_text_get(v,n->name);
        if(name&&!strcmp(name,"Temperature"))signal=wf_text_get(v,n->value);
    }
    if(!canvas||!signal)return -1;
    if(numeric_hotcold_pending) {
        unsigned heat=!strcmp(signal,"HOT")?0:!strcmp(signal,"WARM")?1:!strcmp(signal,"COLD")?2:!strcmp(signal,"ICE COLD")?3:4;
        if(heat==4)return -1;
        if(!heat){*a=(WFAction){.kind=WF_CLICK,.target=canvas->ref,.arg0=numeric_hotcold_probe_x,.arg1=numeric_hotcold_probe_y};return 0;}
        unsigned kept=0;
        for(unsigned i=0;i<numeric_hotcold_count;i++)if(numeric_hotcold_heat(numeric_hotcold_x[i],numeric_hotcold_y[i],numeric_hotcold_probe_x,numeric_hotcold_probe_y)==heat) {
            numeric_hotcold_x[kept]=numeric_hotcold_x[i];numeric_hotcold_y[kept++]=numeric_hotcold_y[i];
        }
        numeric_hotcold_count=kept;
    }
    if(!numeric_hotcold_count)return -1;
    uint64_t best=UINT64_MAX;unsigned bx=numeric_hotcold_x[0],by=numeric_hotcold_y[0];
    /* Bounded query search: coarse canvas coverage and sampled feasible points. */
    for(unsigned y=5;y<120;y+=15)for(unsigned x=5;x<155;x+=15) {
        uint64_t score=numeric_hotcold_score(x,y);
        if(score<best){best=score;bx=x;by=y;}
    }
    unsigned stride=(numeric_hotcold_count+99)/100;
    for(unsigned i=0;i<numeric_hotcold_count;i+=stride) {
        unsigned x=numeric_hotcold_x[i],y=numeric_hotcold_y[i];uint64_t score=numeric_hotcold_score(x,y);
        if(score<=best){best=score;bx=x;by=y;}
    }
    numeric_hotcold_probe_x=bx;numeric_hotcold_probe_y=by;numeric_hotcold_pending=1;
    *a=(WFAction){.kind=WF_POINTER_MOVE,.target=canvas->ref,.arg0=bx,.arg1=by};return 0;
}
#endif
