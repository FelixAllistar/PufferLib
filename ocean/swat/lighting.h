#ifndef SWAT_LIGHTING_H
#define SWAT_LIGHTING_H
#include "sim.h"
#include "raylib.h"
#include "environment_art.h"

typedef struct SwatLightingProgram {
    Shader shader;
    int camera,sun_matrix,lamp_matrix,sun_map,lamp_map,rooms,centers,halves,lamp_room,exposure;
    int pbr,normal_map,roughness,metalness;
    int environment,environment_normal,environment_normal_map,environment_roughness_map;
    int environment_size,environment_tile;
    int skinning,skin_sets,skin_palette,skin_influences,emission,normal_green;
} SwatLightingProgram;
typedef struct SwatLighting {
    bool initialized,enabled,prepared;
    SwatLightingProgram batch,mesh;
    RenderTexture2D sun,lamp;
    Matrix sun_matrix,lamp_matrix;
    Vector3 sun_direction;
    float exposure;
    uint32_t geometry;
    int lamp_room,last_tick,updates;
    bool cutaway;
    unsigned int surface_normal,surface_roughness;
} SwatLighting;
typedef void (*SwatShadowScene)(const SwatSim* sim,bool cutaway);
typedef void (*SwatShadowSceneContext)(void* context,const SwatSim* sim,bool cutaway);

void swat_lighting_init(SwatLighting* light);
// Same exact full-influence vertex path for unlit and shadow mesh draws.
Shader swat_lighting_skin_shader(void);
void swat_lighting_close(SwatLighting* light);
// Call outside any 3D/texture mode. Authority is read only; doors/destruction
// invalidate immediately, moving silhouettes refresh at most every four ticks.
void swat_lighting_prepare(SwatLighting* light,const SwatSim* sim,Vector3 eye,
                           bool cutaway,SwatShadowScene draw);
void swat_lighting_prepare_context(SwatLighting* light,const SwatSim* sim,Vector3 eye,
                           bool cutaway,SwatShadowSceneContext draw,void* context);
// Called inside BeginMode3D; the shader never touches text or HUD compositing.
void swat_lighting_begin(SwatLighting* light,SwatEnvironmentArt* art,const SwatWorld* world,Vector3 camera);
void swat_lighting_end(SwatLighting* light,SwatEnvironmentArt* art);
// Original glTF normal and packed G-roughness/B-metalness maps. Reset after
// drawing the material so ordinary scene meshes retain their diffuse shading.
void swat_lighting_material(SwatLighting* light,Material material,bool enabled);
// Independent R8 roughness/OpenGL normals for metric environment surfaces.
// Mesh size/tile remap the original door's unit-space positions to metre UVs.
void swat_lighting_surface(SwatLighting* light,Texture2D normal,Texture2D roughness,
                           Vector3 size,Vector2 tile,bool mesh);
#endif
