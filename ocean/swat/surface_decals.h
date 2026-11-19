// Surface decals (presentation only). See ENVIRONMENT_ART.md "Surface decals".
//
// Mesh decals clipped to the faces of world objects. A decal is a rectangle on
// a plane (centre, normal, in-plane up). At load it is snapped onto the nearest
// real surface (wall skin, slab, ground) and remembers every coplanar object
// face it overlaps ("supports"). Each rebuild clips the rectangle against every
// support face that is still active, using the fractured outline when a skin
// section is broken, so breached sections take their decals with them exactly
// like the wall art. Nothing here touches collision, navigation, perception,
// the protocol or the simulation RNG: the world is only read.
//
// Pure C (no Raylib) so the math, parsing and placement are unit-tested
// headless (tests/test_surface_decals.c). surface_decals_draw.h owns the GPU side.
//
// Placement file grammar (one statement per line, '#' comments):
//   decal TILE X Y Z NX NY NZ [w= h= scale= rot= alpha= layer= anchor=top|center mirror=1]
//   scatter NAME surface=KIND tiles=A,B mode=area|base|top|corner density=D
//           [seed= scale=a..b rot=a..b alpha=a..b layer= offset= box=x0,y0,z0,x1,y1,z1
//            chance=]
// KIND: wall_exterior, wall_interior, floor_carpet, floor_tile, floor, walkway,
// ground. Ranges are "a..b" or a single value. Density is per metre of face
// width for base/top, per square metre for area, a probability per bottom
// corner for corner. Scatter is deterministic: seeded per rule and surface.
#ifndef SWAT_SURFACE_DECALS_H
#define SWAT_SURFACE_DECALS_H
#include "motel.h"
#include "world.h"
#include "fracture_types.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SWAT_DECAL_TILES 64
#define SWAT_DECAL_RULES 64
#define SWAT_DECAL_MAX 1536
#define SWAT_DECAL_SUPPORTS 40
#define SWAT_DECAL_POLY 40 // Rectangle clipped by a 32-vertex cut fragment.
// Vertices emitted per clipped piece are at most (POLY-2)*3.
#define SWAT_DECAL_SURFACE_OFFSET 0.0015f

typedef enum SwatDecalSurface {
    SWAT_DECAL_WALL_EXTERIOR, SWAT_DECAL_WALL_INTERIOR, SWAT_DECAL_FLOOR_CARPET, SWAT_DECAL_FLOOR_TILE,
    SWAT_DECAL_FLOOR, SWAT_DECAL_WALKWAY, SWAT_DECAL_GROUND, SWAT_DECAL_SURFACE_KINDS
} SwatDecalSurface;
typedef enum SwatDecalMode { SWAT_DECAL_AREA, SWAT_DECAL_BASE, SWAT_DECAL_TOP, SWAT_DECAL_CORNER } SwatDecalMode;

typedef struct SwatDecalTile {
    char name[24];
    float u0,v0,u1,v1;     // Atlas UV rectangle; v0 is the image top row.
    float width,height;    // Default physical size, metres.
} SwatDecalTile;

typedef struct SwatDecal {
    int tile,layer,order;
    float alpha;
    float center[3],normal[3],right[3],up[3];
    float half_w,half_h;
    int supports,support[SWAT_DECAL_SUPPORTS];
    unsigned char face[SWAT_DECAL_SUPPORTS];
} SwatDecal;

typedef struct SwatDecalRule {
    char name[24];
    SwatDecalSurface surface; SwatDecalMode mode;
    int tiles[8],tile_count,layer;
    uint32_t seed;
    float density,chance,offset;
    float scale[2],rot[2],alpha[2];
    bool boxed; float box[6];
} SwatDecalRule;

typedef struct SwatDecalSet {
    char color_file[64],nr_file[64];
    int tile_count,rule_count,count;
    SwatDecalTile tiles[SWAT_DECAL_TILES];
    SwatDecalRule rules[SWAT_DECAL_RULES];
    SwatDecal decals[SWAT_DECAL_MAX];
    int unresolved,dropped,hero,scattered; // Load statistics for review.
} SwatDecalSet;

// ------------------------------------------------------------------ vectors
static inline void swat_decal_v3(float* o,float x,float y,float z){o[0]=x;o[1]=y;o[2]=z;}
static inline float swat_decal_dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static inline void swat_decal_cross(float* o,const float* a,const float* b) {
    float x=a[1]*b[2]-a[2]*b[1],y=a[2]*b[0]-a[0]*b[2],z=a[0]*b[1]-a[1]*b[0];o[0]=x;o[1]=y;o[2]=z;
}
static inline void swat_decal_normalize(float* v) {
    float l=sqrtf(swat_decal_dot(v,v));if(l>1e-12f){v[0]/=l;v[1]/=l;v[2]/=l;}
}

// Decal basis: up is world +Y projected into the plane (walls) or world -Z
// (floors and ceilings), then rotated by `degrees` about the normal. right x
// up = normal, so CCW (u,v) polygons face the viewer on the normal side.
static inline void swat_decal_basis(SwatDecal* d,float degrees) {
    swat_decal_normalize(d->normal);
    const float* n=d->normal;
    float up[3]={0,1,0};
    if(fabsf(n[1])>.7f)swat_decal_v3(up,0,0,-1);
    float k=swat_decal_dot(up,n);for(int i=0;i<3;i++)up[i]-=k*n[i];
    swat_decal_normalize(up);
    float right[3];swat_decal_cross(right,up,n);swat_decal_normalize(right);
    float a=degrees*3.14159265f/180,c=cosf(a),s=sinf(a);
    for(int i=0;i<3;i++){d->right[i]=c*right[i]+s*up[i];d->up[i]=-s*right[i]+c*up[i];}
}

// ------------------------------------------------------------ face geometry
// Object-local axes in world space (yaw about +Y; pitched objects are skipped).
static inline void swat_decal_axes(const SwatObject* o,float axes[3][3]) {
    float c=cosf(o->yaw),s=sinf(o->yaw);
    swat_decal_v3(axes[0],c,0,-s);swat_decal_v3(axes[1],0,1,0);swat_decal_v3(axes[2],s,0,c);
}
// Face f: axis f/2, sign + for even f. Returns the outward normal and plane offset.
static inline float swat_decal_face_plane(const SwatObject* o,int f,float normal[3]) {
    float axes[3][3];swat_decal_axes(o,axes);int a=f/2;float sign=(f&1)?-1.f:1.f;
    const float half[3]={o->half.x,o->half.y,o->half.z};
    for(int i=0;i<3;i++)normal[i]=sign*axes[a][i];
    float c[3]={(float)o->center.x,(float)o->center.y,(float)o->center.z};
    return swat_decal_dot(normal,c)+half[a];
}
// World corners of a face outline (rectangle, or the fractured (Y,Z) outline of
// a broken skin's X faces). Returns the corner count.
static inline int swat_decal_face_outline(const SwatObject* o,int f,float out[][3]) {
    float axes[3][3];swat_decal_axes(o,axes);
    int a=f/2,b=(a+1)%3,c=(a+2)%3;float sign=(f&1)?-1.f:1.f;
    const float half[3]={o->half.x,o->half.y,o->half.z};
    float base[3]={(float)o->center.x,(float)o->center.y,(float)o->center.z};
    for(int i=0;i<3;i++)base[i]+=sign*half[a]*axes[a][i];
    if(a==0 && o->fractured) {
        for(int k=0;k<4;k++)for(int i=0;i<3;i++)out[k][i]=base[i]+o->corners[k][0]*axes[1][i]+o->corners[k][1]*axes[2][i];
        return 4;
    }
    static const float sx[4]={-1,1,1,-1},sy[4]={-1,-1,1,1};
    for(int k=0;k<4;k++)for(int i=0;i<3;i++)out[k][i]=base[i]+sx[k]*half[b]*axes[b][i]+sy[k]*half[c]*axes[c][i];
    return 4;
}

// World position of an object-local (Y,Z) point on X face f (cut fragments).
static inline void swat_decal_face_point(const SwatObject* o,int f,float y,float z,float out[3]) {
    float axes[3][3];swat_decal_axes(o,axes);float sign=(f&1)?-1.f:1.f;
    for(int i=0;i<3;i++)out[i]=(i==0?(float)o->center.x:i==1?(float)o->center.y:(float)o->center.z)+sign*o->half.x*axes[0][i]+y*axes[1][i]+z*axes[2][i];
}

// --------------------------------------------------------------- clipping
// Sutherland-Hodgman: clip polygon `in` (n points) by convex polygon `clip`
// (m points, any winding). Returns the output count (<= SWAT_DECAL_POLY).
static inline int swat_decal_clip(const float in[][2],int n,const float clip[][2],int m,float out[][2]) {
    float area=0;for(int i=0;i<m;i++){int j=(i+1)%m;area+=clip[i][0]*clip[j][1]-clip[j][0]*clip[i][1];}
    float wind=area<0?-1.f:1.f;
    float a[SWAT_DECAL_POLY][2],b[SWAT_DECAL_POLY][2];int count=n<SWAT_DECAL_POLY?n:SWAT_DECAL_POLY;
    for(int i=0;i<count;i++){a[i][0]=in[i][0];a[i][1]=in[i][1];}
    for(int e=0;e<m && count>0;e++) {
        const float* p=clip[e];const float* q=clip[(e+1)%m];
        float ex=q[0]-p[0],ey=q[1]-p[1];int next=0;
        for(int i=0;i<count;i++) {
            const float* s=a[i];const float* t=a[(i+1)%count];
            float ds=wind*(ex*(s[1]-p[1])-ey*(s[0]-p[0])),dt=wind*(ex*(t[1]-p[1])-ey*(t[0]-p[0]));
            if(ds>=0 && next<SWAT_DECAL_POLY){b[next][0]=s[0];b[next][1]=s[1];next++;}
            if((ds>=0)!=(dt>=0) && next<SWAT_DECAL_POLY) {
                float k=ds/(ds-dt);b[next][0]=s[0]+k*(t[0]-s[0]);b[next][1]=s[1]+k*(t[1]-s[1]);next++;
            }
        }
        count=next;memcpy(a,b,sizeof(float)*2*count);
    }
    // Drop slivers: degenerate output counts as fully clipped.
    float kept=0;for(int i=0;i<count;i++){int j=(i+1)%count;kept+=a[i][0]*a[j][1]-a[j][0]*a[i][1];}
    if(count<3 || fabsf(kept)<1e-8f)return 0;
    for(int i=0;i<count;i++){out[i][0]=a[i][0];out[i][1]=a[i][1];}
    return count;
}

static inline float swat_decal_area(const float p[][2],int n) {
    float a=0;for(int i=0;i<n;i++){int j=(i+1)%n;a+=p[i][0]*p[j][1]-p[j][0]*p[i][1];}return fabsf(a)*.5f;
}
static inline bool area_less(const float a[][2],int n,const float b[][2],int m){return swat_decal_area(a,n)<swat_decal_area(b,m);}
// Keeps the part of a convex polygon with sign*(p[axis]-value) <= 0.
static inline int swat_decal_clip_half(float poly[][2],int n,int axis,float value,float sign) {
    float out[SWAT_DECAL_POLY][2];int m=0;
    for(int i=0;i<n && m<SWAT_DECAL_POLY;i++) {
        const float* s=poly[i];const float* t=poly[(i+1)%n];
        float ds=sign*(s[axis]-value),dt=sign*(t[axis]-value);
        if(ds<=0){out[m][0]=s[0];out[m][1]=s[1];m++;}
        if((ds<=0)!=(dt<=0) && m<SWAT_DECAL_POLY){float k=ds/(ds-dt);out[m][0]=s[0]+k*(t[0]-s[0]);out[m][1]=s[1]+k*(t[1]-s[1]);m++;}
    }
    float kept=0;for(int i=0;i<m;i++){int j=(i+1)%m;kept+=out[i][0]*out[j][1]-out[j][0]*out[i][1];}
    if(m<3 || fabsf(kept)<1e-8f)return 0;
    memcpy(poly,out,sizeof(float)*2*m);return m;
}
static inline void swat_decal_bounds(const float p[][2],int n,float b[4]) {
    b[0]=b[1]=1e9f;b[2]=b[3]=-1e9f;
    for(int i=0;i<n;i++){b[0]=fminf(b[0],p[i][0]);b[1]=fminf(b[1],p[i][1]);b[2]=fmaxf(b[2],p[i][0]);b[3]=fmaxf(b[3],p[i][1]);}
}
// Neighbouring wall sections overlap by a few centimetres. Two decal pieces on
// overlapping supports would double the alpha along every joint, so each
// overlapping pair is split at the midline of its overlap (across its thinner
// extent); a piece fully inside another is dropped. Pieces are convex.
static inline void swat_decal_separate(float polys[][SWAT_DECAL_POLY][2],int* counts,int n) {
    for(int i=0;i<n;i++)for(int j=i+1;j<n && counts[i];j++) {
        if(!counts[j])continue;
        float bi[4],bj[4];swat_decal_bounds(polys[i],counts[i],bi);swat_decal_bounds(polys[j],counts[j],bj);
        float ox0=fmaxf(bi[0],bj[0]),oy0=fmaxf(bi[1],bj[1]),ox1=fminf(bi[2],bj[2]),oy1=fminf(bi[3],bj[3]);
        if(ox1-ox0<=1e-5f || oy1-oy0<=1e-5f)continue;
        float o[SWAT_DECAL_POLY][2];int m=swat_decal_clip(polys[i],counts[i],polys[j],counts[j],o);
        if(!m)continue;
        float ob[4];swat_decal_bounds(o,m,ob);
        int axis=(ob[2]-ob[0])<=(ob[3]-ob[1])?0:1;float mid=(ob[axis]+ob[axis+2])*.5f;
        float ci=(bi[axis]+bi[axis+2])*.5f,cj=(bj[axis]+bj[axis+2])*.5f;
        if(fabsf(ci-cj)<1e-5f){if(area_less(polys[i],counts[i],polys[j],counts[j]))counts[i]=0;else counts[j]=0;continue;}
        float sign=ci<cj?1.f:-1.f; // i keeps its own side of the midline
        counts[i]=swat_decal_clip_half(polys[i],counts[i],axis,mid,sign);
        counts[j]=swat_decal_clip_half(polys[j],counts[j],axis,mid,-sign);
    }
}

// ------------------------------------------------------------- parsing
static inline int swat_decal_tile(const SwatDecalSet* set,const char* name) {
    for(int i=0;i<set->tile_count;i++)if(!strcmp(set->tiles[i].name,name))return i;
    return -1;
}
static inline void swat_decal_range(const char* v,float* r) {
    const char* dots=strstr(v,"..");r[0]=strtof(v,NULL);r[1]=dots?strtof(dots+2,NULL):r[0];
}
// Splits `line` in place into whitespace tokens.
static inline int swat_decal_tokens(char* line,char** tokens,int max) {
    int n=0;char* save=NULL;
    for(char* t=strtok_r(line," \t\r\n",&save);t && n<max;t=strtok_r(NULL," \t\r\n",&save))tokens[n++]=t;
    return n;
}
// Reads atlas.txt (generated with the atlas). Returns false when malformed.
static inline bool swat_decals_parse_atlas(SwatDecalSet* set,const char* text) {
    int cols=0,rows=0;set->tile_count=0;
    const char* p=text;
    while(p && *p) {
        const char* end=strchr(p,'\n');size_t len=end?(size_t)(end-p):strlen(p);
        char line[256];if(len>=sizeof(line))len=sizeof(line)-1;memcpy(line,p,len);line[len]=0;
        p=end?end+1:NULL;
        char* hash=strchr(line,'#');if(hash)*hash=0;
        char* t[12];int n=swat_decal_tokens(line,t,12);if(!n)continue;
        if(!strcmp(t[0],"atlas") && n>=5) {
            snprintf(set->color_file,sizeof(set->color_file),"%s",t[1]);
            snprintf(set->nr_file,sizeof(set->nr_file),"%s",t[2]);
            cols=atoi(t[3]);rows=atoi(t[4]);
        } else if(!strcmp(t[0],"tile") && n>=8 && cols>0 && rows>0 && set->tile_count<SWAT_DECAL_TILES) {
            SwatDecalTile* tile=&set->tiles[set->tile_count++];
            snprintf(tile->name,sizeof(tile->name),"%s",t[1]);
            float x=atof(t[2]),y=atof(t[3]),w=atof(t[4]),h=atof(t[5]);
            // Half-texel inset at the coarsest useful mip keeps neighbours out.
            float ix=.5f/(cols*32),iy=.5f/(rows*32);
            tile->u0=x/cols+ix;tile->u1=(x+w)/cols-ix;tile->v0=y/rows+iy;tile->v1=(y+h)/rows-iy;
            tile->width=atof(t[6]);tile->height=atof(t[7]);
        }
    }
    return cols>0 && rows>0 && set->tile_count>0 && set->color_file[0] && set->nr_file[0];
}

static inline uint32_t swat_decal_hash(uint32_t x) {
    x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;
}
static inline float swat_decal_rand(uint32_t* state) {
    *state=swat_decal_hash(*state+0x9e3779b9U);return (*state>>8)*(1.0f/16777216.0f);
}
static inline float swat_decal_pick(uint32_t* state,const float* r){return r[0]+(r[1]-r[0])*swat_decal_rand(state);}

static inline SwatDecal* swat_decal_add(SwatDecalSet* set,int tile,const float* center,const float* normal,
                                        float width,float height,float degrees,float alpha,int layer) {
    if(set->count>=SWAT_DECAL_MAX){set->dropped++;return NULL;}
    SwatDecal* d=&set->decals[set->count];memset(d,0,sizeof(*d));
    d->tile=tile;d->layer=layer;d->order=set->count;d->alpha=alpha;
    for(int i=0;i<3;i++){d->center[i]=center[i];d->normal[i]=normal[i];}
    d->half_w=width*.5f;d->half_h=height*.5f;
    swat_decal_basis(d,degrees);
    set->count++;return d;
}

static inline bool swat_decals_parse_placement(SwatDecalSet* set,const char* text) {
    const char* p=text;bool ok=true;
    while(p && *p) {
        const char* end=strchr(p,'\n');size_t len=end?(size_t)(end-p):strlen(p);
        char line[512];if(len>=sizeof(line))len=sizeof(line)-1;memcpy(line,p,len);line[len]=0;
        p=end?end+1:NULL;
        char* hash=strchr(line,'#');if(hash)*hash=0;
        char* t[24];int n=swat_decal_tokens(line,t,24);if(!n)continue;
        if(!strcmp(t[0],"decal") && n>=8) {
            int tile=swat_decal_tile(set,t[1]);if(tile<0){ok=false;continue;}
            float c[3]={atof(t[2]),atof(t[3]),atof(t[4])},nn[3]={atof(t[5]),atof(t[6]),atof(t[7])};
            float w=set->tiles[tile].width,h=set->tiles[tile].height,rot=0,alpha=1,scale=1;int layer=1;bool top=false,mirror=false;
            for(int i=8;i<n;i++) {
                char* eq=strchr(t[i],'=');if(!eq)continue;*eq=0;const char* v=eq+1;
                if(!strcmp(t[i],"w"))w=atof(v);else if(!strcmp(t[i],"h"))h=atof(v);
                else if(!strcmp(t[i],"scale"))scale=atof(v);else if(!strcmp(t[i],"rot"))rot=atof(v);
                else if(!strcmp(t[i],"alpha"))alpha=atof(v);else if(!strcmp(t[i],"layer"))layer=atoi(v);
                else if(!strcmp(t[i],"anchor"))top=!strcmp(v,"top");
                else if(!strcmp(t[i],"mirror"))mirror=atoi(v)!=0;
            }
            SwatDecal* d=swat_decal_add(set,tile,c,nn,w*scale,h*scale,rot,alpha,layer);
            if(d && top)for(int i=0;i<3;i++)d->center[i]-=d->up[i]*d->half_h;
            if(d && mirror)for(int i=0;i<3;i++)d->right[i]=-d->right[i];
            if(d)set->hero++;
        } else if(!strcmp(t[0],"scatter") && n>=2 && set->rule_count<SWAT_DECAL_RULES) {
            SwatDecalRule* r=&set->rules[set->rule_count];memset(r,0,sizeof(*r));
            snprintf(r->name,sizeof(r->name),"%s",t[1]);
            r->scale[0]=r->scale[1]=1;r->alpha[0]=r->alpha[1]=1;r->layer=0;r->chance=1;r->seed=1;
            bool valid=true;
            for(int i=2;i<n;i++) {
                char* eq=strchr(t[i],'=');if(!eq)continue;*eq=0;char* v=eq+1;
                if(!strcmp(t[i],"surface")) {
                    static const char* kinds[SWAT_DECAL_SURFACE_KINDS]={"wall_exterior","wall_interior","floor_carpet","floor_tile","floor","walkway","ground"};
                    int k=-1;for(int j=0;j<SWAT_DECAL_SURFACE_KINDS;j++)if(!strcmp(v,kinds[j]))k=j;
                    if(k<0)valid=false;else r->surface=(SwatDecalSurface)k;
                } else if(!strcmp(t[i],"mode")) {
                    r->mode=!strcmp(v,"base")?SWAT_DECAL_BASE:!strcmp(v,"top")?SWAT_DECAL_TOP:!strcmp(v,"corner")?SWAT_DECAL_CORNER:SWAT_DECAL_AREA;
                } else if(!strcmp(t[i],"tiles")) {
                    char* save=NULL;
                    for(char* name=strtok_r(v,",",&save);name && r->tile_count<8;name=strtok_r(NULL,",",&save)) {
                        int tile=swat_decal_tile(set,name);if(tile<0)valid=false;else r->tiles[r->tile_count++]=tile;
                    }
                } else if(!strcmp(t[i],"density"))r->density=atof(v);
                else if(!strcmp(t[i],"chance"))r->chance=atof(v);
                else if(!strcmp(t[i],"offset"))r->offset=atof(v);
                else if(!strcmp(t[i],"seed"))r->seed=(uint32_t)strtoul(v,NULL,10);
                else if(!strcmp(t[i],"layer"))r->layer=atoi(v);
                else if(!strcmp(t[i],"scale"))swat_decal_range(v,r->scale);
                else if(!strcmp(t[i],"rot"))swat_decal_range(v,r->rot);
                else if(!strcmp(t[i],"alpha"))swat_decal_range(v,r->alpha);
                else if(!strcmp(t[i],"box")) {
                    r->boxed=sscanf(v,"%f,%f,%f,%f,%f,%f",&r->box[0],&r->box[1],&r->box[2],&r->box[3],&r->box[4],&r->box[5])==6;
                    if(!r->boxed)valid=false;
                }
            }
            if(valid && r->tile_count)set->rule_count++;else ok=false;
        }
    }
    return ok;
}

// ------------------------------------------------------- world surfaces
static inline bool swat_decal_support_part(const SwatObject* o) {
    if(o->door || o->motion || o->rubble || fabsf(o->pitch)>1e-4f || o->material==SWAT_GLASS || o->material==SWAT_OPAQUE_GLASS)return false;
    return o->part==SWAT_PART_SOLID || o->part==SWAT_PART_SKIN || o->part==SWAT_PART_OBSTACLE || o->part==SWAT_PART_FIXTURE;
}
static inline int swat_decal_room_at(const SwatWorld* w,const float* p) {
    for(int i=0;i<w->room_count;i++) {
        const SwatRoom* r=&w->rooms[i];
        if(fabsf(p[0]-(float)r->center.x)<=r->half.x && fabsf(p[1]-(float)r->center.y)<=r->half.y+.05f &&
           fabsf(p[2]-(float)r->center.z)<=r->half.z)return i;
    }
    return -1;
}
// Scatter surfaces are logical walls (motel wall parents: inactive SOLID with a
// wall group; or active plain wall solids) and large upward slabs. Returns the
// surface kind of face f of object o, or -1.
static inline int swat_decal_surface_kind(const SwatWorld* w,const SwatObject* o,int f) {
    if(o->door || o->motion || o->rubble || fabsf(o->pitch)>1e-4f || o->part!=SWAT_PART_SOLID)return -1;
    // Court rows (MOTEL_LAYOUT.md) are mesh-collided kit pieces whose boxes
    // include trim and openings: only their walkway slabs take scatter.
    SwatMotelInstance court;
    if(swat_motel_court(w,o->tag.index,&court,NULL) && court.asset!=6)return -1;
    bool parent=!o->active && o->wall_group;
    if(!o->active && !parent)return -1;
    float n[3];float d=swat_decal_face_plane(o,f,n);(void)d;
    const float half[3]={o->half.x,o->half.y,o->half.z};int a=f/2,b=(a+1)%3,c=(a+2)%3;
    float centre[3]={(float)o->center.x,(float)o->center.y,(float)o->center.z};
    float face[3];for(int i=0;i<3;i++)face[i]=centre[i]+n[i]*half[a];
    if(fabsf(n[1])<.05f) {
        // Wall: the thin horizontal axis is the face axis; at least 1.6 m tall.
        float thin=fminf(half[0],half[2]);
        if(a==1 || half[a]>thin+1e-4f || half[1]<.8f || fmaxf(half[b],half[c])<.5f || half[a]>.35f)return -1;
        if(!(o->material==SWAT_PLASTER || o->material==SWAT_BRICK || o->material==SWAT_DRYWALL || o->material==SWAT_CONCRETE))return -1;
        float probe[3]={face[0]+n[0]*.35f,face[1],face[2]+n[2]*.35f};
        return swat_decal_room_at(w,probe)>=0?SWAT_DECAL_WALL_INTERIOR:SWAT_DECAL_WALL_EXTERIOR;
    }
    if(n[1]>.99f && !parent) {
        if(half[0]*half[2]*4<1.0f)return -1;
        float probe[3]={centre[0],face[1]+.3f,centre[2]};
        int room=swat_decal_room_at(w,probe);
        if(room>=0)return w->rooms[room].floor==SWAT_CARPET?SWAT_DECAL_FLOOR_CARPET:w->rooms[room].floor==SWAT_TILE?SWAT_DECAL_FLOOR_TILE:SWAT_DECAL_FLOOR;
        if(face[1]< -.05f)return SWAT_DECAL_GROUND;
        if(face[1]<.25f)return SWAT_DECAL_WALKWAY;
    }
    return -1;
}

// Scattered decals are not placed where furniture, fixtures or props stand on
// (or against) the surface: a point 5 cm off the surface inside any other
// active object is covered. Hero decals are placed by hand and not filtered.
static inline bool swat_decal_hidden(const SwatWorld* w,const SwatObject* surface,const float* c,const float* n) {
    float p[3]={c[0]+n[0]*.05f,c[1]+n[1]*.05f,c[2]+n[2]*.05f};
    for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];
        if(o==surface || !o->active || o->part==SWAT_PART_SKIN || o->part==SWAT_PART_LIGHT || o->door)continue;
        float axes[3][3];swat_decal_axes(o,axes);
        float q[3]={p[0]-(float)o->center.x,p[1]-(float)o->center.y,p[2]-(float)o->center.z};
        if(fabsf(swat_decal_dot(q,axes[0]))<o->half.x && fabsf(q[1])<o->half.y && fabsf(swat_decal_dot(q,axes[2]))<o->half.z)return true;
    }
    return false;
}
static inline void swat_decal_scatter_face(SwatDecalSet* set,const SwatWorld* world,const SwatDecalRule* r,const SwatObject* o,int f,uint32_t state) {
    float axes[3][3];swat_decal_axes(o,axes);float n[3];swat_decal_face_plane(o,f,n);
    const float half[3]={o->half.x,o->half.y,o->half.z};int a=f/2;
    float centre[3]={(float)o->center.x,(float)o->center.y,(float)o->center.z};
    // Face frame: s horizontal along the face, t vertical (walls) or the other horizontal.
    float S[3],T[3];float hs,ht;bool wall=fabsf(n[1])<.05f;
    if(wall) {
        int s=a==0?2:0;for(int i=0;i<3;i++){S[i]=axes[s][i];T[i]=axes[1][i];}hs=half[s];ht=half[1];
        // Keep +S to the viewer's right (right = up x n).
        float right[3];swat_decal_cross(right,T,n);if(swat_decal_dot(right,S)<0)for(int i=0;i<3;i++)S[i]=-S[i];
    } else {
        for(int i=0;i<3;i++){S[i]=axes[0][i];T[i]=axes[2][i];}hs=half[0];ht=half[2];
    }
    float origin[3];for(int i=0;i<3;i++)origin[i]=centre[i]+n[i]*half[a];
    float width=2*hs,area=4*hs*ht;
    int count=0;
    if(r->mode==SWAT_DECAL_CORNER)count=2;
    else {
        float expected=r->density*(r->mode==SWAT_DECAL_AREA?area:width);
        count=(int)expected;if(swat_decal_rand(&state)<expected-count)count++;
    }
    for(int k=0;k<count;k++) {
        if(r->chance<1 && swat_decal_rand(&state)>=r->chance)continue;
        int tile=r->tiles[(int)(swat_decal_rand(&state)*r->tile_count)%r->tile_count];
        float scale=swat_decal_pick(&state,r->scale),rot=swat_decal_pick(&state,r->rot),alpha=swat_decal_pick(&state,r->alpha);
        float w=set->tiles[tile].width*scale,h=set->tiles[tile].height*scale;
        float s,t;
        if(r->mode==SWAT_DECAL_CORNER) {
            if(!wall)continue;
            s=(k==0?-1:1)*(hs-w*.5f);t=-ht+h*.5f+r->offset;
            rot=k==0?0:0; // Tile corner (u=0,v=1) is the growth origin; mirror by width sign below.
            if(k==1)w=-w;
        } else {
            float span=hs-fabsf(w)*.5f;s=span>0?(2*swat_decal_rand(&state)-1)*span:0;
            if(r->mode==SWAT_DECAL_BASE)t=-ht+h*.5f+r->offset;
            else if(r->mode==SWAT_DECAL_TOP)t=ht-h*.5f-r->offset;
            else {float tspan=ht-h*.5f;t=tspan>0?(2*swat_decal_rand(&state)-1)*tspan:0;}
        }
        float c[3];for(int i=0;i<3;i++)c[i]=origin[i]+S[i]*s+T[i]*t;
        if(r->boxed && (c[0]<r->box[0]||c[1]<r->box[1]||c[2]<r->box[2]||c[0]>r->box[3]||c[1]>r->box[4]||c[2]>r->box[5]))continue;
        if(swat_decal_hidden(world,o,c,n))continue;
        SwatDecal* d=swat_decal_add(set,tile,c,n,fabsf(w),h,rot,alpha,r->layer);
        if(!d)return;
        if(w<0)for(int i=0;i<3;i++)d->right[i]=-d->right[i]; // Mirrored: flip winding back in emit.
        set->scattered++;
    }
}

static inline void swat_decals_scatter(SwatDecalSet* set,const SwatWorld* w) {
    for(int ri=0;ri<set->rule_count;ri++) {
        const SwatDecalRule* r=&set->rules[ri];
        for(int i=0;i<w->count;i++)for(int f=0;f<6;f++) {
            if(swat_decal_surface_kind(w,&w->objects[i],f)!=(int)r->surface)continue;
            swat_decal_scatter_face(set,w,r,&w->objects[i],f,swat_decal_hash(r->seed*2654435761U+(uint32_t)(i*6+f)));
        }
    }
}

// Decal rectangle corners in its own (u,v) frame, CCW.
static inline void swat_decal_rect(const SwatDecal* d,float rect[4][2]) {
    rect[0][0]=-d->half_w;rect[0][1]=-d->half_h;rect[1][0]=d->half_w;rect[1][1]=-d->half_h;
    rect[2][0]=d->half_w;rect[2][1]=d->half_h;rect[3][0]=-d->half_w;rect[3][1]=d->half_h;
}
static inline int swat_decal_project(const SwatDecal* d,const float pts[][3],int n,float out[][2]) {
    for(int k=0;k<n;k++) {
        float q[3]={pts[k][0]-d->center[0],pts[k][1]-d->center[1],pts[k][2]-d->center[2]};
        out[k][0]=swat_decal_dot(q,d->right);out[k][1]=swat_decal_dot(q,d->up);
    }
    return n;
}
static inline float swat_decal_overlap(const SwatDecal* d,const SwatObject* o,int f) {
    float pts[4][3],uv[4][2],rect[4][2],out[SWAT_DECAL_POLY][2];
    int n=swat_decal_face_outline(o,f,pts);swat_decal_project(d,pts,n,uv);swat_decal_rect(d,rect);
    int m=swat_decal_clip(rect,4,uv,n,out);float area=0;
    for(int i=0;i<m;i++){int j=(i+1)%m;area+=out[i][0]*out[j][1]-out[j][0]*out[i][1];}
    return fabsf(area)*.5f;
}
// Snap each decal onto the outermost real surface within [-8 cm, +3 cm] of its
// plane and record every coplanar overlapping face. Decals without any support
// are dropped (counted in `unresolved`). Uses only load-time state.
static inline void swat_decals_resolve(SwatDecalSet* set,const SwatWorld* w) {
    int kept=0;
    for(int k=0;k<set->count;k++) {
        SwatDecal* d=&set->decals[k];
        float plane=swat_decal_dot(d->normal,d->center),best=-1e9f,best_area=0;
        for(int pass=0;pass<2;pass++) {
            d->supports=0;
            for(int i=0;i<w->count;i++) {
                const SwatObject* o=&w->objects[i];if(!o->active || !swat_decal_support_part(o))continue;
                float dx=(float)o->center.x-d->center[0],dy=(float)o->center.y-d->center[1],dz=(float)o->center.z-d->center[2];
                float reach=sqrtf(o->half.x*o->half.x+o->half.y*o->half.y+o->half.z*o->half.z)+d->half_w+d->half_h+.1f;
                if(dx*dx+dy*dy+dz*dz>reach*reach)continue;
                for(int f=0;f<6;f++) {
                    float n[3];float off=swat_decal_face_plane(o,f,n);
                    if(swat_decal_dot(n,d->normal)<.995f)continue;
                    float delta=off-plane;
                    if(pass==0) {
                        if(delta< -.08f || delta>.03f)continue;
                        float area=swat_decal_overlap(d,o,f);
                        // Outermost surface with real overlap wins; ties by area.
                        if(area>1e-4f && (delta>best+.004f || (fabsf(delta-best)<=.004f && area>best_area))){best=delta;best_area=area;}
                    } else if(fabsf(delta)<=.004f && d->supports<SWAT_DECAL_SUPPORTS && swat_decal_overlap(d,o,f)>1e-5f) {
                        d->support[d->supports]=i;d->face[d->supports]=(unsigned char)f;d->supports++;
                    }
                }
            }
            if(pass==0) {
                if(best<-1e8f)break;
                for(int i=0;i<3;i++)d->center[i]+=d->normal[i]*best;plane+=best;
            }
        }
        if(best<-1e8f || !d->supports){set->unresolved++;continue;}
        set->decals[kept++]=*d;
    }
    set->count=kept;
    // Stable layer sort: insertion order within a layer, so overlaps resolve
    // the same way every run (one batch, drawn back-to-front by layer).
    for(int i=1;i<set->count;i++) {
        SwatDecal tmp=set->decals[i];int j=i-1;
        while(j>=0 && (set->decals[j].layer>tmp.layer || (set->decals[j].layer==tmp.layer && set->decals[j].order>tmp.order))){set->decals[j+1]=set->decals[j];j--;}
        set->decals[j+1]=tmp;
    }
}

// Change signature of everything the clipped mesh depends on.
static inline uint32_t swat_decals_signature(const SwatDecalSet* set,const SwatWorld* w) {
    uint32_t h=2166136261U;
    for(int k=0;k<set->count;k++)for(int s=0;s<set->decals[k].supports;s++) {
        const SwatObject* o=&w->objects[set->decals[k].support[s]];
        h=(h^(uint32_t)(o->active|(o->fractured<<1)|((o->cuts!=NULL)<<2)))*16777619U;
        if(o->cuts) {
            // Fragments are derived from the cut list (bullet holes, breaches).
            h=(h^(uint32_t)o->cuts->count^((uint32_t)o->cuts->section_count<<12)^((uint32_t)o->cuts->support_filtered<<24))*16777619U;
            const unsigned char* bytes=(const unsigned char*)o->cuts->cuts;
            for(size_t b=0;b<sizeof(SwatCut)*(size_t)o->cuts->count;b++)h=(h^bytes[b])*16777619U;
        }
        if(o->fractured)for(int c=0;c<8;c++){uint32_t bits;float v=o->corners[c/2][c%2];memcpy(&bits,&v,4);h=(h^bits)*16777619U;}
    }
    return h;
}

typedef struct SwatDecalVertex { float position[3],uv[2],normal[3]; unsigned char color[4]; } SwatDecalVertex;

// Emits triangles for every decal piece on an active support, in layer order.
// Returns the vertex count (<= max); `pieces` receives the clipped piece count.
static inline int swat_decals_emit(const SwatDecalSet* set,const SwatWorld* w,SwatDecalVertex* out,int max,int* pieces) {
    int count=0,piece_count=0;
    for(int k=0;k<set->count;k++) {
        const SwatDecal* d=&set->decals[k];const SwatDecalTile* tile=&set->tiles[d->tile];
        float rect[4][2];swat_decal_rect(d,rect);
        // A mirrored decal (right flipped) has reversed winding in world space.
        float check[3];swat_decal_cross(check,d->right,d->up);bool mirrored=swat_decal_dot(check,d->normal)<0;
        unsigned char alpha=(unsigned char)lroundf(255*fminf(fmaxf(d->alpha,0),1));
        float polys[SWAT_DECAL_SUPPORTS][SWAT_DECAL_POLY][2];int counts[SWAT_DECAL_SUPPORTS];
        for(int s=0;s<d->supports;s++) {
            const SwatObject* o=&w->objects[d->support[s]];counts[s]=0;if(!o->active)continue;
            float pts[4][3],uv[4][2];
            int n=swat_decal_face_outline(o,d->face[s],pts);swat_decal_project(d,pts,n,uv);
            counts[s]=swat_decal_clip(rect,4,uv,n,polys[s]);
        }
        swat_decal_separate(polys,counts,d->supports);
        for(int s=0;s<d->supports;s++) {
            if(!counts[s])continue;
            const SwatObject* o=&w->objects[d->support[s]];
            // Damaged skins/solids: only their surviving convex fragments
            // (breach openings and bullet holes are cut out of the decal too).
            bool cut=o->cuts && d->face[s]/2==0;
            int fragments=cut?o->cuts->section_count:1;
            for(int g=0;g<fragments;g++) {
            float piece[SWAT_DECAL_POLY][2];int m=counts[s];float (*poly)[2]=polys[s];
            if(cut) {
                const SwatFracturePolygon* fp=&o->cuts->sections[g];
                float pts[SWAT_CUT_VERTICES][3],uv[SWAT_CUT_VERTICES][2];int n=fp->count<SWAT_CUT_VERTICES?fp->count:SWAT_CUT_VERTICES;
                for(int k=0;k<n;k++)swat_decal_face_point(o,d->face[s],fp->vertices[k][0],fp->vertices[k][1],pts[k]);
                swat_decal_project(d,pts,n,uv);
                m=n>=3?swat_decal_clip(polys[s],counts[s],uv,n,piece):0;poly=piece;
                if(!m)continue;
            }
            if(count+(m-2)*3>max)return count;
            piece_count++;
            for(int t=1;t+1<m;t++) {
                int idx[3]={0,t,t+1};if(mirrored){idx[1]=t+1;idx[2]=t;}
                for(int v=0;v<3;v++) {
                    const float* p=poly[idx[v]];SwatDecalVertex* x=&out[count++];
                    for(int i=0;i<3;i++){x->position[i]=d->center[i]+p[0]*d->right[i]+p[1]*d->up[i]+d->normal[i]*SWAT_DECAL_SURFACE_OFFSET;x->normal[i]=d->normal[i];}
                    float fu=(p[0]+d->half_w)/(2*d->half_w),fv=(d->half_h-p[1])/(2*d->half_h);
                    x->uv[0]=tile->u0+fu*(tile->u1-tile->u0);x->uv[1]=tile->v0+fv*(tile->v1-tile->v0);
                    x->color[0]=x->color[1]=x->color[2]=255;x->color[3]=alpha;
                }
            }
            }
        }
    }
    if(pieces)*pieces=piece_count;
    return count;
}
#endif
