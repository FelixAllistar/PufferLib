#ifndef SWAT_BREACH_RUBBLE_H
#define SWAT_BREACH_RUBBLE_H
#include "world.h"

// Settled cosmetic fragments reconstructed from removed physical wall pieces.
// One small bounded batch per camera; no bodies, persistent state or game RNG.
#define SWAT_RUBBLE_FRAGMENTS 12
typedef struct SwatRubbleFragment {
    b3Pos position;
    float length,width,height,yaw;
    int owner;
    SwatMaterial material;
} SwatRubbleFragment;
static inline int swat_rubble_sample(const SwatWorld* w,b3Pos eye,SwatRubbleFragment out[SWAT_RUBBLE_FRAGMENTS]) {
    int owners[SWAT_RUBBLE_FRAGMENTS],count=0;float distances[SWAT_RUBBLE_FRAGMENTS];
    for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];
        if(o->active || !((o->fractured && o->part==SWAT_PART_SKIN) || (o->part==SWAT_PART_FRAME && o->max_health>0)))continue;
        // View relevance is horizontal: an upper fragment lands on its floor.
        float dx=(float)(o->center.x-eye.x),dz=(float)(o->center.z-eye.z),d=dx*dx+dz*dz;
        if(d>144 || (count==SWAT_RUBBLE_FRAGMENTS && d>=distances[count-1]))continue;
        int at=count<SWAT_RUBBLE_FRAGMENTS?count++:count-1;
        while(at && d<distances[at-1]){if(at<SWAT_RUBBLE_FRAGMENTS){owners[at]=owners[at-1];distances[at]=distances[at-1];}at--;}
        owners[at]=i;distances[at]=d;
    }
    for(int i=0;i<count;i++) {
        const SwatObject* o=&w->objects[owners[i]];
        uint32_t h=(uint32_t)(owners[i]+1)*374761393u;h=(h^(h>>13))*1274126177u;h^=h>>16;
        bool board=o->material==SWAT_DRYWALL || o->material==SWAT_PLASTER;
        bool wood=o->material==SWAT_WOOD;
        float length=wood?.13f+.03f*(h&255)/255.0f:board?.10f+.06f*(h&255)/255.0f:.065f+.065f*(h&255)/255.0f;
        float floor=w->motel?-.079f:0;
        for(int r=0;r<w->room_count;r++) {
            const SwatRoom* room=&w->rooms[r];float level=(float)(room->center.y-room->half.y);
            if(level<=o->center.y && fabs(o->center.x-room->center.x)<room->half.x+.3f &&
               fabs(o->center.z-room->center.z)<room->half.z+.3f)floor=fmaxf(floor,level);
        }
        float across=2*o->half.x+length*.7f,along=.22f*(((h>>8)&255)/255.0f-.5f);
        if(h&256)across=-across;
        b3Pos p=b3OffsetPos(o->center,swat_v(cosf(o->yaw)*across+sinf(o->yaw)*along,0,-sinf(o->yaw)*across+cosf(o->yaw)*along));
        p.y=floor+.002f;
        out[i]=(SwatRubbleFragment){p,length,length*(wood?.18f:board?.7f:.8f),board?.008f:wood?.012f:.035f+.03f*((h>>16)&255)/255.0f,
            2*SWAT_PI*((h>>24)&255)/255.0f,owners[i],o->material};
    }
    return count;
}
#endif
