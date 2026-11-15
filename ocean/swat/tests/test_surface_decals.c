// Surface decal math, placement and breach clipping (presentation only).
// Runs headless on the real motel world; the GPU side is not involved.
#include "surface_decals.h"
#include "sim.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static float area(const float p[][2],int n) {
    float a=0;for(int i=0;i<n;i++){int j=(i+1)%n;a+=p[i][0]*p[j][1]-p[j][0]*p[i][1];}return a*.5f;
}
static char* slurp(const char* path) {
    FILE* f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    char* text=malloc(n+1);assert(text && fread(text,1,n,f)==(size_t)n);text[n]=0;fclose(f);return text;
}
static SwatDecalSet* load(const SwatWorld* w) {
    SwatDecalSet* set=calloc(1,sizeof(*set));assert(set);
    char* atlas=slurp("assets/environment/motel_decals/atlas.txt");
    char* placement=slurp("assets/environment/motel_decals/placement.txt");
    assert(swat_decals_parse_atlas(set,atlas));
    assert(swat_decals_parse_placement(set,placement)); // every tile name and rule is valid
    free(atlas);free(placement);
    swat_decals_scatter(set,w);swat_decals_resolve(set,w);
    return set;
}

static void test_clip(void) {
    const float square[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
    float out[SWAT_DECAL_POLY][2];
    // Clip polygon with either winding gives the same result.
    const float half[4][2]={{0,-2},{2,-2},{2,2},{0,2}},half_cw[4][2]={{0,2},{2,2},{2,-2},{0,-2}};
    int n=swat_decal_clip(square,4,half,4,out);assert(n>=4 && fabsf(area(out,n)-2)<1e-5f && area(out,n)>0);
    n=swat_decal_clip(square,4,half_cw,4,out);assert(n>=4 && fabsf(area(out,n)-2)<1e-5f && area(out,n)>0);
    // Triangle cutting the corner: area 1/2 of the unit-leg triangle inside.
    const float tri[3][2]={{0,-1},{1,-1},{1,0}};
    n=swat_decal_clip(square,4,tri,3,out);assert(n==3 && fabsf(area(out,n)-.5f)<1e-5f);
    // Disjoint and touching-edge clips produce nothing.
    const float away[4][2]={{3,3},{4,3},{4,4},{3,4}},touch[4][2]={{1,-1},{2,-1},{2,1},{1,1}};
    assert(swat_decal_clip(square,4,away,4,out)==0);
    assert(swat_decal_clip(square,4,touch,4,out)==0);
    // Containment keeps the decal rectangle unchanged.
    const float big[4][2]={{-5,-5},{5,-5},{5,5},{-5,5}};
    n=swat_decal_clip(square,4,big,4,out);assert(n==4 && fabsf(area(out,n)-4)<1e-6f);
}

static void test_basis(void) {
    float normals[][3]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{.6f,0,.8f}};
    for(int i=0;i<6;i++)for(int r=0;r<4;r++) {
        SwatDecal d={0};memcpy(d.normal,normals[i],12);swat_decal_basis(&d,r*37.f);
        float c[3];swat_decal_cross(c,d.right,d.up);
        assert(fabsf(swat_decal_dot(c,d.normal)-1)<1e-5f); // right x up = n: CCW faces the viewer
        assert(fabsf(swat_decal_dot(d.right,d.normal))<1e-5f && fabsf(swat_decal_dot(d.up,d.normal))<1e-5f);
        if(r==0 && fabsf(d.normal[1])<.5f)assert(d.up[1]>.999f); // walls: tile top is world up
    }
}

static void test_face_outline(void) {
    SwatObject o={0};o.center=(b3Pos){1,2,3};o.half=(b3Vec3){.05f,1,2};o.yaw=SWAT_PI*.5f;o.part=SWAT_PART_SKIN;
    float n[3];float plane=swat_decal_face_plane(&o,0,n);
    // yaw 90: local +X points to world -Z.
    assert(fabsf(n[2]+1)<1e-5f && fabsf(plane-(-3+.05f))<1e-5f);
    float pts[4][3];assert(swat_decal_face_outline(&o,0,pts)==4);
    for(int k=0;k<4;k++)assert(fabsf(pts[k][2]-(3-.05f))<1e-5f);
    // A fractured skin uses its (Y,Z) outline on the X faces only.
    o.fractured=true;float corners[4][2]={{-1,-2},{0,-2},{0,0},{-1,0}};memcpy(o.corners,corners,sizeof(corners));
    swat_decal_face_outline(&o,0,pts);
    for(int k=0;k<4;k++)assert(pts[k][1]>=1-1e-5f && pts[k][1]<=2+1e-5f);
}

int main(void) {
    test_clip();test_basis();test_face_outline();
    SwatSim* sim=calloc(1,sizeof(*sim));
    SwatConfig config=swat_default_config();config.mission=SWAT_MOTEL;config.layout_seed=42;
    swat_sim_init(sim,config,42);
    SwatWorld* w=&sim->world;
    clock_t started=clock();
    SwatDecalSet* a=load(w);
    double load_ms=(clock()-started)*1000.0/CLOCKS_PER_SEC;
    SwatDecalSet* b=load(w);
    // Deterministic placement and every hero decal finds a surface.
    assert(a->count==b->count && !memcmp(a->decals,b->decals,sizeof(SwatDecal)*a->count));
    assert(a->hero>=40 && a->scattered>100 && a->count>=a->hero && a->dropped==0);
    printf("decals: %d placed (%d hero, %d scattered, %d unsupported)\n",a->count,a->hero,a->scattered,a->unresolved);
    for(int k=1;k<a->count;k++)assert(a->decals[k-1].layer<=a->decals[k].layer);
    int heroes=0;
    for(int k=0;k<a->count;k++) {
        const SwatDecal* d=&a->decals[k];assert(d->supports>0 && d->half_w>0 && d->half_h>0);
        for(int s=0;s<d->supports;s++) {
            const SwatObject* o=&w->objects[d->support[s]];float n[3];
            float plane=swat_decal_face_plane(o,d->face[s],n);
            assert(swat_decal_dot(n,d->normal)>.995f && fabsf(plane-swat_decal_dot(n,d->center))<.005f);
            assert(!o->door && o->material!=SWAT_GLASS);
        }
        if(!strcmp(a->tiles[d->tile].name,"tag_vexo"))heroes++;
    }
    assert(heroes==1);
    // Emitted vertices: inside the decal, on the surface, no NaN.
    SwatDecalVertex* v=malloc(sizeof(*v)*200000);int pieces=0;
    started=clock();
    int before=swat_decals_emit(a,w,v,200000,&pieces);
    double emit_ms=(clock()-started)*1000.0/CLOCKS_PER_SEC;
    started=clock();uint32_t sig=0;for(int i=0;i<100;i++)sig+=swat_decals_signature(a,w)&1;
    printf("cpu: load (parse+scatter+resolve) %.1f ms, rebuild %.2f ms, signature %.4f ms (%x)\n",load_ms,emit_ms,(clock()-started)*1000.0/CLOCKS_PER_SEC/100,sig);
    assert(before>0 && before%3==0 && pieces>0);
    for(int i=0;i<before;i++)for(int j=0;j<3;j++)assert(isfinite(v[i].position[j]) && v[i].uv[0]>=0 && v[i].uv[0]<=1 && v[i].uv[1]>=0 && v[i].uv[1]<=1);
    printf("emit: %d vertices, %d pieces\n",before,pieces);
    // Breach: deactivate every support section of the rear-wall VEXO tag.
    int tag=-1;for(int k=0;k<a->count;k++)if(!strcmp(a->tiles[a->decals[k].tile].name,"tag_vexo"))tag=k;
    const SwatDecal* d=&a->decals[tag];
    assert(d->supports>=2); // the tag spans several wall skin sections
    uint32_t signature=swat_decals_signature(a,w);
    int victim=d->support[0];
    w->objects[victim].active=false;
    assert(swat_decals_signature(a,w)!=signature);
    int after=swat_decals_emit(a,w,v,200000,NULL);assert(after<before);
    // Every emitted vertex lies on the face of a still-active support (the
    // removed section's area survives only where a neighbour overlaps it).
    for(int i=0;i<after;i++) {
        bool covered=false;
        for(int k=0;k<a->count && !covered;k++)for(int s=0;s<a->decals[k].supports && !covered;s++) {
            const SwatObject* o=&w->objects[a->decals[k].support[s]];if(!o->active)continue;
            float axes[3][3];swat_decal_axes(o,axes);int f=a->decals[k].face[s],ax=f/2;
            float q[3]={v[i].position[0]-(float)o->center.x,v[i].position[1]-(float)o->center.y,v[i].position[2]-(float)o->center.z};
            float l[3]={swat_decal_dot(q,axes[0]),swat_decal_dot(q,axes[1]),swat_decal_dot(q,axes[2])};
            const float h[3]={o->half.x,o->half.y,o->half.z};
            covered=fabsf(fabsf(l[ax])-h[ax])<.003f && fabsf(l[(ax+1)%3])<=h[(ax+1)%3]+1e-3f && fabsf(l[(ax+2)%3])<=h[(ax+2)%3]+1e-3f;
        }
        assert(covered);
    }
    // Fracture: a partly broken neighbour keeps only its surviving outline.
    w->objects[victim].active=true;
    int partial=d->support[d->supports-1];SwatObject* o=&w->objects[partial];SwatObject saved=*o;
    if(o->part==SWAT_PART_SKIN) {
        o->fractured=true;
        float c[4][2]={{-o->half.y,-o->half.z},{0,-o->half.z},{0,o->half.z},{-o->half.y,o->half.z}}; // lower half survives
        memcpy(o->corners,c,sizeof(c));
        assert(swat_decals_signature(a,w)!=signature);
        int fractured=swat_decals_emit(a,w,v,200000,NULL);assert(fractured<=before);
        *o=saved;
    }
    assert(swat_decals_signature(a,w)==signature);
    assert(swat_decals_emit(a,w,v,200000,NULL)==before);
    // A real breach (swat_world_cut fragments, sections stay active): the
    // opening is removed from every decal on that wall, bullet holes likewise.
    {
        float c[3]={d->center[0],d->center[1],d->center[2]};
        int section=d->support[0];
        assert(swat_world_breach(w,section,(b3Pos){c[0],c[1],c[2]})>0);
        assert(swat_decals_signature(a,w)!=signature);
        int breached=swat_decals_emit(a,w,v,200000,NULL);assert(breached>0);
        float right[3]={d->right[0],d->right[1],d->right[2]};
        int inside=0;
        for(int i=0;i<breached;i++) {
            float q[3]={v[i].position[0]-c[0],v[i].position[1]-c[1],v[i].position[2]-c[2]};
            if(fabsf(swat_decal_dot(q,d->normal))>.01f)continue;
            // Cut half extents are 1.12 m x 0.92 m with rough edges: test the inner 80 %.
            if(fabsf(q[1])<.85f && fabsf(swat_decal_dot(q,right))<.70f)inside++;
        }
        printf("breach: %d -> %d vertices, %d inside the opening\n",before,breached,inside);
        assert(inside==0);
    }
    free(v);free(a);free(b);swat_sim_close(sim);free(sim);
    printf("PASS surface decals: clip/basis/outline math, deterministic placement, section/fracture/breach-cut clipping\n");
    return 0;
}
