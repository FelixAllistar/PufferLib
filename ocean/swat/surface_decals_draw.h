// Surface decal batch (presentation only). Included by environment_art.c.
// See surface_decals.h for the model and ENVIRONMENT_ART.md "Surface decals".
//
// One atlas pair (sRGB colour + alpha, normal + roughness), one vertex buffer,
// one DrawMesh per lit camera pass with the ordinary lit mesh program: decals
// receive sun/lamp shadows, IBL, contact AO, fog and grade like the surface
// under them. Depth test on, depth writes off, polygon offset towards the
// camera plus a 1.5 mm lift, straight alpha blending in layer order. Not drawn
// into shadow, contact or plan-depth passes. The clipped mesh is rebuilt only
// when an active/fractured flag or outline of a supporting section changes.
// SWAT_SURFACE_DECALS=0 disables it (SWAT_DECALS remains the ground mask).
#include "surface_decals.h"

struct SwatSurfaceDecals {
    SwatDecalSet* set;
    Texture2D color,nr;
    Material material;
    Mesh mesh;
    SwatDecalVertex* scratch;
    int capacity,vertices,pieces,location,world_count;
    uint32_t signature;
    bool failed,ready;
    unsigned int rebuilds;
};

static bool surface_decals_enabled(void) {
    const char* e=getenv("SWAT_SURFACE_DECALS");return !e || strcmp(e,"0");
}
static char* surface_decals_text(const char* name) {
    char path[4096];if(!asset_path(path,sizeof(path),name))return NULL;
    return LoadFileText(path);
}
static void surface_decals_release(SwatSurfaceDecals* d) {
    if(!d)return;
    if(d->mesh.vaoId || d->mesh.vertices)UnloadMesh(d->mesh);
    d->mesh=(Mesh){0};
    if(d->color.id)UnloadTexture(d->color);
    if(d->nr.id)UnloadTexture(d->nr);
    d->color=d->nr=(Texture2D){0};
    // The material borrows the lighting program; free only its map array.
    if(d->material.maps)RL_FREE(d->material.maps);
    d->material=(Material){0};
    free(d->set);free(d->scratch);
    d->set=NULL;d->scratch=NULL;d->capacity=d->vertices=d->pieces=0;d->ready=false;d->signature=0;
}
static Texture2D surface_decals_texture(const char* directory,const char* file) {
    char name[256];snprintf(name,sizeof(name),"%s/%s",directory,file);
    char path[4096];Texture2D t={0};
    if(!asset_path(path,sizeof(path),name))return t;
    Image image=LoadImage(path);
    // Budget: at most 2048 px on a side, RGBA8.
    if(image.data && image.width<=2048 && image.height<=2048 && image.format==PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        t=LoadTextureFromImage(image);
    UnloadImage(image);
    if(t.id) {
        GenTextureMipmaps(&t);
        SetTextureFilter(t,TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(t,TEXTURE_FILTER_ANISOTROPIC_4X);
        SetTextureWrap(t,TEXTURE_WRAP_CLAMP);
    }
    return t;
}
// Per-location placement. Other levels add a row (directory with atlas.txt and
// placement.txt); a missing directory simply leaves that level undecorated.
static const char* surface_decals_directory(const SwatWorld* w) {
    if(w->motel)return "motel_decals";
    if(w->hollow)return "hollow_decals";
    return NULL;
}
static bool surface_decals_load(SwatSurfaceDecals* d,const SwatWorld* w,int location) {
    surface_decals_release(d);
    d->location=location;d->world_count=w->count;d->failed=true;
    const char* directory=surface_decals_directory(w);
    if(!directory || !surface_decals_enabled())return false;
    char name[256];
    snprintf(name,sizeof(name),"%s/atlas.txt",directory);char* atlas=surface_decals_text(name);
    snprintf(name,sizeof(name),"%s/placement.txt",directory);char* placement=surface_decals_text(name);
    d->set=calloc(1,sizeof(*d->set));
    bool ok=atlas && placement && d->set && swat_decals_parse_atlas(d->set,atlas);
    if(ok && !swat_decals_parse_placement(d->set,placement))
        TraceLog(LOG_WARNING,"SWAT: %s/placement.txt has unknown tiles or malformed rules (skipped)",directory);
    if(atlas)UnloadFileText(atlas);
    if(placement)UnloadFileText(placement);
    if(ok) {
        swat_decals_scatter(d->set,w);
        swat_decals_resolve(d->set,w);
        d->color=surface_decals_texture(directory,d->set->color_file);
        d->nr=surface_decals_texture(directory,d->set->nr_file);
        ok=d->color.id && d->nr.id;
    }
    if(!ok) {
        TraceLog(LOG_WARNING,"SWAT: surface decals for %s unavailable",directory);
        surface_decals_release(d);return false;
    }
    d->material=LoadMaterialDefault();
    d->material.maps[MATERIAL_MAP_ALBEDO].texture=d->color;
    d->material.maps[MATERIAL_MAP_ALBEDO].color=WHITE;
    d->material.maps[MATERIAL_MAP_NORMAL].texture=d->nr;
    // Signed UV derivatives (no tangents); atlas green points up the image,
    // i.e. against +V, hence the flipped green.
    d->material.maps[MATERIAL_MAP_NORMAL].value=-2;
    d->material.maps[MATERIAL_MAP_ROUGHNESS].value=1;
    d->material.maps[MATERIAL_MAP_METALNESS].value=0;
    d->failed=false;d->ready=true;
    TraceLog(LOG_INFO,"SWAT: surface decals %s: %d placed (%d hero, %d scattered, %d unsupported, %d over budget)",
        directory,d->set->count,d->set->hero,d->set->scattered,d->set->unresolved,d->set->dropped);
    return true;
}
static void surface_decals_rebuild(SwatSurfaceDecals* d,const SwatWorld* w,uint32_t signature) {
    if(d->mesh.vaoId || d->mesh.vertices)UnloadMesh(d->mesh);
    d->mesh=(Mesh){0};d->vertices=0;d->signature=signature;d->rebuilds++;
    int need=0;for(int k=0;k<d->set->count;k++)need+=d->set->decals[k].supports*(SWAT_DECAL_POLY-2)*3;
    if(need>d->capacity) {
        free(d->scratch);d->scratch=malloc(sizeof(*d->scratch)*(size_t)need);d->capacity=d->scratch?need:0;
    }
    if(!d->scratch)return;
    int count=swat_decals_emit(d->set,w,d->scratch,d->capacity,&d->pieces);
    if(count<3)return;
    Mesh m={0};m.vertexCount=count;m.triangleCount=count/3;
    m.vertices=RL_MALLOC(sizeof(float)*3*count);m.texcoords=RL_MALLOC(sizeof(float)*2*count);
    m.normals=RL_MALLOC(sizeof(float)*3*count);m.colors=RL_MALLOC(4*(size_t)count);
    for(int i=0;i<count;i++) {
        const SwatDecalVertex* v=&d->scratch[i];
        memcpy(m.vertices+3*i,v->position,12);memcpy(m.texcoords+2*i,v->uv,8);
        memcpy(m.normals+3*i,v->normal,12);memcpy(m.colors+4*i,v->color,4);
    }
    UploadMesh(&m,false);
    d->mesh=m;d->vertices=count;
}

void swat_environment_surface_decals(const SwatEnvironmentArt* art,const SwatWorld* world) {
    SwatSurfaceDecals* d=art->surface_decals;
    if(!d || !art->lit || !art->lighting || !art->lighting->enabled)return;
    int location=world->hollow?3:world->storefront?2:world->motel?1:0;
    if(!d->set && !d->failed && !surface_decals_directory(world))return;
    if(location!=d->location || world->count!=d->world_count || (!d->ready && !d->failed))surface_decals_load(d,world,location);
    if(!d->ready)return;
    uint32_t signature=swat_decals_signature(d->set,world);
    if(signature!=d->signature || (!d->vertices && !d->rebuilds))surface_decals_rebuild(d,world,signature);
    if(!d->vertices)return;
    SwatLighting* light=art->lighting;
    rlDrawRenderBatchActive();
    swat_lighting_surface_decals(light,true);
    d->material.shader=light->mesh.shader;
    swat_lighting_material_uv(light,d->material,true,1,0);
    // Re-assert after material binding (it resets unrelated mesh uniforms).
    swat_lighting_surface_decals(light,true);
    rlDisableDepthMask();
    glEnable(SWAT_GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-2);
    DrawMesh(d->mesh,d->material,MatrixIdentity());
    glDisable(SWAT_GL_POLYGON_OFFSET_FILL);
    rlEnableDepthMask();
    swat_lighting_material(light,d->material,false);
    swat_lighting_surface_decals(light,false);
}

SwatSurfaceDecalStats swat_environment_surface_decal_stats(const SwatEnvironmentArt* art) {
    SwatSurfaceDecalStats s={0};const SwatSurfaceDecals* d=art->surface_decals;
    if(!d || !d->ready)return s;
    s.decals=d->set->count;s.hero=d->set->hero;s.scattered=d->set->scattered;s.unresolved=d->set->unresolved;
    s.pieces=d->pieces;s.vertices=d->vertices;s.rebuilds=d->rebuilds;s.draws=d->vertices?1:0;
    return s;
}
