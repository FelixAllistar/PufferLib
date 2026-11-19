// Briar Court exterior props v2 (presentation only). Included by
// environment_art.c after room101_model_load. See ENVIRONMENT_ART.md,
// "Exterior props v2", and assets/environment/motel_props_ext_v2/README.md.
//
// Vehicles, utility poles and wires, palms, street furniture and litter for
// the lot, the street and the verges, drawn in world space from the placement
// table in motel_props_ext_v2_data.h with the lit mesh shader, the sun shadow
// pass and frustum culling. Nothing here enters the simulation: no colliders,
// navigation, cover, sight, acoustics, protocol or determinism change (the
// authoritative object list is replicated and validated, so solid props need a
// separate gameplay pass). SWAT_PROPS_EXT_V2=0 disables the layer.
#include "motel_props_ext_v2_data.h"

static struct {
    const SwatEnvironmentArt* owner;
    Model models[SWAT_PE2_PROPS];
    float scales[SWAT_PE2_PROPS][SWAT_ROOM101_MATERIALS];
    BoundingBox bounds[SWAT_PE2_PROPS];
} pe2;

static bool props_ext_v2_enabled(void) {
    const char* e=getenv("SWAT_PROPS_EXT_V2");
    return !e || strcmp(e,"0");
}

static void props_ext_v2_close(const SwatEnvironmentArt* art) {
    if(pe2.owner!=art)return;
    for(int i=0;i<SWAT_PE2_PROPS;i++)swat_art_model_close(pe2.models[i]);
    memset(&pe2,0,sizeof(pe2));
}

static void props_ext_v2_load(const SwatEnvironmentArt* art) {
    if(pe2.owner)props_ext_v2_close(pe2.owner);
    if(!props_ext_v2_enabled())return;
    pe2.owner=art;
    for(int i=0;i<SWAT_PE2_PROPS;i++) {
        char file[160];snprintf(file,sizeof(file),"motel_props_ext_v2/%s.glb",swat_pe2_props[i]);
        // Each file fails independently; a missing prop simply is not drawn.
        if(!room101_model_load(&pe2.models[i],pe2.scales[i],file)) {
            swat_art_model_close(pe2.models[i]);pe2.models[i]=(Model){0};continue;
        }
        pe2.bounds[i]=GetModelBoundingBox(pe2.models[i]);
    }
}

static Matrix props_ext_v2_transform(const SwatPe2Placement* p) {
    return MatrixMultiply(MatrixMultiply(MatrixScale(p->scale,p->scale,p->scale),MatrixRotateY(p->yaw)),MatrixTranslate(p->x,p->y,p->z));
}

static BoundingBox props_ext_v2_world_bounds(BoundingBox b,Matrix m) {
    BoundingBox out={{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};
    for(int k=0;k<8;k++) {
        Vector3 v={k&1?b.max.x:b.min.x,k&2?b.max.y:b.min.y,k&4?b.max.z:b.min.z};
        v=Vector3Transform(v,m);out.min=Vector3Min(out.min,v);out.max=Vector3Max(out.max,v);
    }
    return out;
}

static void props_ext_v2_draw(const SwatEnvironmentArt* art,bool shadow,bool lit,Matrix scene) {
    if(pe2.owner!=art)return;
    for(int i=0;i<SWAT_PE2_PLACEMENTS;i++) {
        const SwatPe2Placement* p=&swat_pe2_placements[i];
        const Model* model=&pe2.models[p->prop];
        if(!model->meshCount || (shadow && !p->shadow))continue;
        Matrix transform=props_ext_v2_transform(p);
        if(art->bounds && bounds_outside(art->bounds,props_ext_v2_world_bounds(pe2.bounds[p->prop],transform),scene))continue;
        for(int m=0;m<model->meshCount;m++) {
            Material material=model->materials[model->meshMaterial[m]];
            MaterialMap maps[SWAT_MATERIAL_MAPS];
            if(shadow) {
                memset(maps,0,sizeof(maps));maps[MATERIAL_MAP_ALBEDO].texture.id=rlGetTextureIdDefault();
                maps[MATERIAL_MAP_ALBEDO].color=WHITE;material.maps=maps;
                material.shader=(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
            } else {
                material.shader=lit?art->lighting->mesh.shader:(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
                if(lit)swat_lighting_material_uv(art->lighting,material,true,pe2.scales[p->prop][model->meshMaterial[m]],0);
            }
            DrawMesh(model->meshes[m],material,transform);
        }
    }
}
