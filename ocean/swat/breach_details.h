#ifndef SWAT_BREACH_DETAILS_H
#define SWAT_BREACH_DETAILS_H
#include "world.h"
// Cosmetic attachments on actual exposed cut edges. Four pieces per material,
// selected locally, leave the structural cavity empty. No bodies or game RNG.
#define SWAT_BREACH_DETAILS 12
typedef struct SwatBreachDetail {b3Pos position;b3Vec3 x,y,z;int mesh,owner;} SwatBreachDetail;
static inline uint32_t swat_detail_hash(uint32_t h) {h=(h^(h>>13))*1274126177u;return h^(h>>16);}
static inline int swat_breach_details(const SwatWorld* w,b3Pos eye,SwatBreachDetail out[SWAT_BREACH_DETAILS]) {
    int count=0,owners[3][4],used[3]={0};float distances[3][4];
    for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];if(!o->active || !o->cuts)continue;
        int kind=o->material==SWAT_WOOD?0:o->material==SWAT_DRYWALL?1:o->material==SWAT_BRICK?2:-1;
        if(kind<0)continue;
        bool breach=false;for(int c=0;c<o->cuts->count;c++)breach|=o->cuts->cuts[c].kind==SWAT_CUT_BREACH;
        if(!breach)continue;
        b3Vec3 d=b3SubPos(o->center,eye);float distance=b3Dot(d,d);
        if(distance>64 || (used[kind]==4 && distance>=distances[kind][3]))continue;
        int at=used[kind]<4?used[kind]++:3;
        while(at && distance<distances[kind][at-1]){owners[kind][at]=owners[kind][at-1];distances[kind][at]=distances[kind][at-1];at--;}
        owners[kind][at]=i;distances[kind][at]=distance;
    }
    for(int kind=0;kind<3;kind++)for(int i=0;i<used[kind];i++) {
        int owner=owners[kind][i];const SwatObject* o=&w->objects[owner];
        uint32_t hash=swat_detail_hash((owner+1)*374761393u),best=0;bool found=false;
        float y=0,z=0,ny=0,nz=0;
        for(int s=0;s<o->cuts->section_count;s++) {
            const SwatFracturePolygon* p=&o->cuts->sections[s];
            for(int e=0;e<p->count;e++) {
                if(!(p->exposed_edges&(1u<<e)))continue;
                const float* a=p->vertices[e];const float* b=p->vertices[(e+1)%p->count];
                float dy=b[0]-a[0],dz=b[1]-a[1],length=hypotf(dy,dz);if(length<(kind==0?.035f:.065f))continue;
                float cy=(a[0]+b[0])*.5f,cz=(a[1]+b[1])*.5f,oy=dz/length,oz=-dy/length;
                bool edge=false;
                for(int c=0;c<o->cuts->count;c++) {
                    const SwatCut* cut=&o->cuts->cuts[c];if(cut->kind!=SWAT_CUT_BREACH)continue;
                    float u=(cy-cut->y)/cut->height,v=(cz-cut->z)/cut->width,q=u*u*u*u+v*v*v*v;
                    // Outward from the retained polygon must head into this
                    // opening, excluding the original cell perimeter.
                    edge|=q>.70f && q<1.04f && (u*u*u*oy/cut->height+v*v*v*oz/cut->width)<0;
                }
                uint32_t score=swat_detail_hash(hash+(uint32_t)(s*32+e)*0x85ebca6bu);
                if(edge && (!found || score>best)){found=true;best=score;y=cy;z=cz;ny=oy;nz=oz;}
            }
        }
        if(!found)continue;
        b3Quat q=swat_rotation(o->yaw,o->pitch);
        // Native fragment thickness stays unchanged. Timber/clay can sit at
        // different depths on a real core; thin paper stays with its board skin.
        float depth=kind==1?0:((hash>>8)&255)/255.0f*(2*o->half.x-.02f)-o->half.x+.01f;
        b3Vec3 x=swat_v(0,ny,nz),normal=swat_v(1,0,0),tangent=swat_v(0,nz,-ny);
        out[count++]=(SwatBreachDetail){b3OffsetPos(o->center,b3RotateVector(q,swat_v(depth,y,z))),
            b3RotateVector(q,x),b3RotateVector(q,normal),b3RotateVector(q,tangent),kind*2+(int)(hash&1),owner};
    }
    return count;
}
#endif
