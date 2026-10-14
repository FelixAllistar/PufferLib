// Deterministic polygon subtraction shared by physics, queries and art.
#ifndef SWAT_FRACTURE_GEOMETRY_H
#define SWAT_FRACTURE_GEOMETRY_H
#include "world.h"
#include "door_damage.h"
#include <string.h>
typedef struct SwatCutPlane {float y,z,offset;} SwatCutPlane;
static inline int swat_cut_plane_count(const SwatCut* cut) {return cut->kind==SWAT_CUT_BREACH?SWAT_CUT_SIDES:8;}
static inline bool swat_cut_valid(const SwatCut* cut) {
    return isfinite(cut->y) && isfinite(cut->z) && isfinite(cut->height) && isfinite(cut->width) &&
        fabsf(cut->y)<=200 && fabsf(cut->z)<=200 && cut->height>=.005f && cut->width>=.005f &&
        cut->height<=10 && cut->width<=10 && (unsigned int)cut->kind<SWAT_CUT_KINDS;
}
static inline void swat_cut_planes(const SwatCut* cut,SwatCutPlane planes[SWAT_CUT_SIDES]) {
    if(cut->kind>=SWAT_CUT_PATCH) {
        // A convex octagon with fixed plane normals. The four bevel distances
        // are rounded outward; growing a patch can never restore old cover.
        const int sy[8]={1,1,0,-1,-1,-1,0,1},sz[8]={0,1,1,1,0,-1,-1,-1};
        for(int i=0;i<8;i++) {
            float distance=(sy[i]?cut->height:0)+(sz[i]?cut->width:0);
            if(i&1)distance-=(cut->height+cut->width)*((cut->seed>>(8*(i/2)))&255u)/255.0f;
            float scale=(i&1)?.7071067811865475f:1;
            planes[i]=(SwatCutPlane){sy[i]*scale,sz[i]*scale,(sy[i]*cut->y+sz[i]*cut->z+distance)*scale};
        }
        return;
    }
    float points[SWAT_CUT_SIDES][2];int sides=swat_cut_plane_count(cut);
    for(int i=0;i<sides;i++) {
        float angle=2*SWAT_PI*i/sides,c=cosf(angle),s=sinf(angle);
        // Breacher silhouette follows an authored broad rectangle with rounded
        // corners. Impact openings use a small ellipse. These are game profiles.
        if(cut->kind==SWAT_CUT_BREACH){c=copysignf(sqrtf(fabsf(c)),c);s=copysignf(sqrtf(fabsf(s)),s);}
        unsigned int hash=cut->seed+(unsigned int)i*0x85ebca6bu;hash=(hash^(hash>>13))*1274126177u;hash^=hash>>16;
        float rough=cut->kind==SWAT_CUT_BREACH?.004f:.025f;
        float radius=1+rough*((hash%10001)/5000.0f-1);
        points[i][0]=cut->y+cut->height*radius*c;points[i][1]=cut->z+cut->width*radius*s;
    }
    for(int i=0;i<sides;i++) {
        int j=(i+1)%sides;float y=points[j][0]-points[i][0],z=points[j][1]-points[i][1],length=hypotf(y,z);
        planes[i]=(SwatCutPlane){z/length,-y/length,0};
        planes[i].offset=planes[i].y*points[i][0]+planes[i].z*points[i][1];
    }
}
static inline SwatCut swat_impact_patch(const SwatObject* o,float y,float z,float radius) {
    int row=(int)floorf((y+o->half.y)*(SWAT_IMPACT_ROWS/(2*o->half.y)));
    int col=(int)floorf((z+o->half.z)*(SWAT_IMPACT_COLS/(2*o->half.z)));
    row=(int)swat_clamp(row,0,SWAT_IMPACT_ROWS-1);col=(int)swat_clamp(col,0,SWAT_IMPACT_COLS-1);
    // 74/255 gives a nearly regular octagon with a conservative round edge.
    return (SwatCut){.y=y,.z=z,.height=radius,.width=radius,.seed=0x4a4a4a4au,
        .kind=(SwatCutKind)(SWAT_CUT_PATCH+row*SWAT_IMPACT_COLS+col)};
}
static inline SwatCut swat_patch_union(SwatCut a,SwatCut b) {
    SwatCut out=a;
    float low_y=fminf(a.y-a.height,b.y-b.height),high_y=fmaxf(a.y+a.height,b.y+b.height);
    float low_z=fminf(a.z-a.width,b.z-b.width),high_z=fmaxf(a.z+a.width,b.z+b.width);
    out.y=.5f*(low_y+high_y);out.z=.5f*(low_z+high_z);
    out.height=.5f*(high_y-low_y);out.width=.5f*(high_z-low_z);out.seed=0;
    const int sy[4]={1,-1,-1,1},sz[4]={1,1,-1,-1};
    for(int i=0;i<4;i++) {
        float da=(a.height+a.width)*((a.seed>>(8*i))&255u)/255.0f;
        float db=(b.height+b.width)*((b.seed>>(8*i))&255u)/255.0f;
        float support=fmaxf(sy[i]*a.y+sz[i]*a.z+a.height+a.width-da,
                           sy[i]*b.y+sz[i]*b.z+b.height+b.width-db);
        float bevel=sy[i]*out.y+sz[i]*out.z+out.height+out.width-support;
        unsigned int packed=(unsigned int)swat_clamp(floorf(255*bevel/(out.height+out.width)-.0001f),0,255);
        out.seed|=packed<<(8*i);
    }
    return out;
}
static inline float swat_polygon_area(const SwatFracturePolygon* p) {
    float area=0;for(int i=0;i<p->count;i++){int j=(i+1)%p->count;area+=p->vertices[i][0]*p->vertices[j][1]-p->vertices[i][1]*p->vertices[j][0];}
    return .5f*area;
}
static inline void swat_polygon_simplify(SwatFracturePolygon* p) {
    bool changed=true;
    while(changed && p->count>=3) {
        changed=false;
        for(int i=0;i<p->count;i++) {
            int j=(i+1)%p->count,k=(i+2)%p->count;
            float y=p->vertices[j][0]-p->vertices[i][0],z=p->vertices[j][1]-p->vertices[i][1];
            float dy=p->vertices[k][0]-p->vertices[j][0],dz=p->vertices[k][1]-p->vertices[j][1];
            if(y*y+z*z<1e-12f || fabsf(y*dz-z*dy)<1e-9f) {
                memmove(&p->vertices[j],&p->vertices[j+1],(size_t)(p->count-j-1)*sizeof(p->vertices[0]));p->count--;changed=true;break;
            }
        }
    }
}
static inline bool swat_polygon_clip(const SwatFracturePolygon* in,SwatCutPlane plane,float sign,SwatFracturePolygon* out) {
    memset(out,0,sizeof(*out));
    for(int i=0;i<in->count;i++) {
        int j=(i+1)%in->count;const float* a=in->vertices[i];const float* b=in->vertices[j];
        float av=sign*(plane.y*a[0]+plane.z*a[1]-plane.offset),bv=sign*(plane.y*b[0]+plane.z*b[1]-plane.offset);
        if(av>=0) {if(out->count==SWAT_CUT_VERTICES)return false;memcpy(out->vertices[out->count++],a,2*sizeof(float));}
        if((av>=0)!=(bv>=0)) {
            if(out->count==SWAT_CUT_VERTICES)return false;
            float t=av/(av-bv);
            out->vertices[out->count][0]=a[0]+t*(b[0]-a[0]);out->vertices[out->count++][1]=a[1]+t*(b[1]-a[1]);
        }
    }
    swat_polygon_simplify(out);return true;
}
static inline bool swat_cut_grows(const SwatCut* old,const SwatCut* next) {
    if(!memcmp(old,next,sizeof(*old)))return true;
    if(old->kind<SWAT_CUT_PATCH || old->kind!=next->kind || !swat_cut_valid(next))return false;
    SwatCutPlane a[SWAT_CUT_SIDES],b[SWAT_CUT_SIDES];swat_cut_planes(old,a);swat_cut_planes(next,b);
    // Matching fixed normals make containment a small support comparison.
    for(int i=0;i<8;i++)if(b[i].offset<a[i].offset-1e-6f)return false;
    return true;
}
static inline void swat_source_polygon(const SwatObject* o,SwatFracturePolygon* p) {
    memset(p,0,sizeof(*p));p->count=4;
    if(o->fractured)memcpy(p->vertices,o->corners,sizeof(o->corners));
    else {const float v[4][2]={{-o->half.y,-o->half.z},{o->half.y,-o->half.z},{o->half.y,o->half.z},{-o->half.y,o->half.z}};
        memcpy(p->vertices,v,sizeof(v));}
}
static inline float swat_fracture_turn(const float a[2],const float b[2],const float c[2]) {
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
}
static inline bool swat_polygon_join(const SwatFracturePolygon* a,const SwatFracturePolygon* b,SwatFracturePolygon* joined) {
    float alo[2]={INFINITY,INFINITY},ahi[2]={-INFINITY,-INFINITY};
    float blo[2]={INFINITY,INFINITY},bhi[2]={-INFINITY,-INFINITY};
    for(int i=0;i<a->count;i++)for(int k=0;k<2;k++){alo[k]=fminf(alo[k],a->vertices[i][k]);ahi[k]=fmaxf(ahi[k],a->vertices[i][k]);}
    for(int i=0;i<b->count;i++)for(int k=0;k<2;k++){blo[k]=fminf(blo[k],b->vertices[i][k]);bhi[k]=fmaxf(bhi[k],b->vertices[i][k]);}
    for(int k=0;k<2;k++)if(ahi[k]<blo[k]-.000001f || bhi[k]<alo[k]-.000001f)return false;
    float points[2*SWAT_CUT_VERTICES][2],hull[4*SWAT_CUT_VERTICES][2];int count=0;
    for(int source=0;source<2;source++) {
        const SwatFracturePolygon* p=source?b:a;
        for(int i=0;i<p->count;i++) {
            int at=count++;
            while(at && (points[at-1][0]>p->vertices[i][0] ||
                (points[at-1][0]==p->vertices[i][0] && points[at-1][1]>p->vertices[i][1]))) {
                memcpy(points[at],points[at-1],sizeof(points[at]));at--;
            }
            memcpy(points[at],p->vertices[i],sizeof(points[at]));
        }
    }
    int n=0;
    for(int i=0;i<count;i++) {
        while(n>=2 && swat_fracture_turn(hull[n-2],hull[n-1],points[i])<=0)n--;
        memcpy(hull[n++],points[i],sizeof(points[i]));
    }
    int lower=n;
    for(int i=count-2;i>=0;i--) {
        while(n>lower && swat_fracture_turn(hull[n-2],hull[n-1],points[i])<=0)n--;
        memcpy(hull[n++],points[i],sizeof(points[i]));
    }
    if(--n<3 || n>SWAT_CUT_VERTICES)return false;
    memset(joined,0,sizeof(*joined));joined->count=n;
    memcpy(joined->vertices,hull,(size_t)n*sizeof(*hull));
    // A convex hull may merge neighbors only when it adds no material across
    // an opening. The original pieces are disjoint by construction.
    float area=swat_polygon_area(a)+swat_polygon_area(b);
    return fabsf(swat_polygon_area(joined)-area)<=1e-7f*fmaxf(1,area);
}
static inline int swat_polygons_join(SwatFracturePolygon* pieces,int count) {
    bool changed=true;
    while(changed) {
        changed=false;
        for(int a=0;a<count;a++)for(int b=a+1;b<count;) {
            SwatFracturePolygon joined;
            if(swat_polygon_join(&pieces[a],&pieces[b],&joined)) {
                pieces[a]=joined;pieces[b]=pieces[--count];changed=true;b=a+1;
            } else b++;
        }
    }
    return count;
}
static inline int swat_cut_polygons(const SwatObject* o,const SwatCut* cuts,int count,SwatFracturePolygon out[SWAT_CUT_SECTIONS]) {
    SwatFracturePolygon input[SWAT_CUT_SECTIONS]={0};int n=1;
    if(o->door && o->latch_broken) {
        n=SWAT_DOOR_SECTIONS;
        for(int i=0;i<n;i++){input[i].count=4;swat_door_section(o,i,input[i].vertices);}
    } else swat_source_polygon(o,&input[0]);
    for(int c=0;c<count;c++) {
        if(!swat_cut_valid(&cuts[c]))return -1;
        SwatCutPlane planes[SWAT_CUT_SIDES];swat_cut_planes(&cuts[c],planes);int next=0,sides=swat_cut_plane_count(&cuts[c]);
        for(int p=0;p<n;p++) {
            // A cut's infinite edge planes must not split distant retained
            // material. Only subtract from pieces that overlap the aperture.
            SwatFracturePolygon overlap=input[p];
            for(int e=0;e<sides && overlap.count>=3;e++) {
                SwatFracturePolygon rest;
                if(!swat_polygon_clip(&overlap,planes[e],-1,&rest))return -1;
                overlap=rest;
            }
            if(overlap.count<3 || swat_polygon_area(&overlap)<=1e-7f) {
                if(next==SWAT_CUT_SECTIONS)next=swat_polygons_join(out,next);
                if(next==SWAT_CUT_SECTIONS)return -1;
                out[next++]=input[p];continue;
            }
            SwatFracturePolygon inside=input[p];
            for(int e=0;e<sides && inside.count>=3;e++) {
                SwatFracturePolygon outside,rest;
                if(!swat_polygon_clip(&inside,planes[e],1,&outside) || !swat_polygon_clip(&inside,planes[e],-1,&rest))return -1;
                if(outside.count>=3 && swat_polygon_area(&outside)>1e-6f) {
                    if(next==SWAT_CUT_SECTIONS)next=swat_polygons_join(out,next);
                    if(next==SWAT_CUT_SECTIONS)return -1;
                    out[next++]=outside;
                }
                inside=rest;
            }
        }
        n=next;memcpy(input,out,(size_t)n*sizeof(*out));
    }
    if(!count)memcpy(out,input,(size_t)n*sizeof(*out));
    return swat_polygons_join(out,n);
}
static inline bool swat_polygon_contains(const SwatFracturePolygon* p,float y,float z,float tolerance) {
    for(int i=0;i<p->count;i++) {int j=(i+1)%p->count;
        if((p->vertices[j][0]-p->vertices[i][0])*(z-p->vertices[i][1])-
           (p->vertices[j][1]-p->vertices[i][1])*(y-p->vertices[i][0])< -tolerance)return false;
    }
    return true;
}
static inline bool swat_cut_contains(const SwatCut* cut,float y,float z) {
    SwatCutPlane planes[SWAT_CUT_SIDES];swat_cut_planes(cut,planes);
    for(int i=0;i<swat_cut_plane_count(cut);i++)if(planes[i].y*y+planes[i].z*z-planes[i].offset>0)return false;
    return true;
}
static inline bool swat_fracture_contains(const SwatObject* o,float y,float z) {
    SwatFracturePolygon original;swat_source_polygon(o,&original);
    if(!swat_polygon_contains(&original,y,z,1e-6f))return false;
    if(o->door && o->latch_broken && z>swat_door_edge(o,y))return false;
    if(o->cuts)for(int i=0;i<o->cuts->count;i++)if(swat_cut_contains(&o->cuts->cuts[i],y,z))return false;
    return true;
}
static inline bool swat_fracture_edge_open(const SwatObject* o,const float a[2],const float b[2]) {
    float dy=b[0]-a[0],dz=b[1]-a[1],length=hypotf(dy,dz);if(length<1e-6f)return false;
    return !swat_fracture_contains(o,.5f*(a[0]+b[0])+.0001f*dz/length,.5f*(a[1]+b[1])-.0001f*dy/length);
}
static inline float swat_fracture_exit(const SwatFracture* f,float half_x,b3Vec3 rel,b3Vec3 direction) {
    float intervals[SWAT_CUT_SECTIONS][2];int count=0;
    for(int s=0;s<f->section_count;s++) {
        const SwatFracturePolygon* polygon=&f->sections[s];float lo=-1e6f,hi=1e6f;
        for(int p=0;p<polygon->count+2;p++) {
            float value,rate;
            if(p<2){float sign=p?-1:1;value=half_x-sign*rel.x;rate=-sign*direction.x;}
            else {int i=p-2,j=(i+1)%polygon->count;
                float y=polygon->vertices[j][0]-polygon->vertices[i][0],z=polygon->vertices[j][1]-polygon->vertices[i][1];
                value=y*(rel.z-polygon->vertices[i][1])-z*(rel.y-polygon->vertices[i][0]);rate=y*direction.z-z*direction.y;}
            if(fabsf(rate)<1e-7f){if(value< -1e-7f){hi=-1e6f;break;}}
            else if(rate>0)lo=fmaxf(lo,-value/rate);else hi=fminf(hi,-value/rate);
        }
        if(lo<=hi && hi>=0) {
            int at=count++;while(at && intervals[at-1][0]>lo){memcpy(intervals[at],intervals[at-1],sizeof(intervals[0]));at--;}
            intervals[at][0]=lo;intervals[at][1]=hi;
        }
    }
    float end=0;bool found=false;
    for(int i=0;i<count;i++) {
        if(intervals[i][0]>end+.00001f)break;
        end=fmaxf(end,intervals[i][1]);found=true;
    }
    return found && end<1e6f ? end : 0;
}
static inline uint32_t swat_fracture_hash(const SwatObject* o) {
    if(!o->cuts)return 0;
    uint32_t hash=2166136261u;const unsigned char* bytes=(const unsigned char*)o->cuts->cuts;
    for(size_t i=0;i<(size_t)o->cuts->count*sizeof(SwatCut);i++){hash^=bytes[i];hash*=16777619u;}
    return hash;
}
#endif
