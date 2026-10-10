#ifndef SWAT_LIGHTING_H
#define SWAT_LIGHTING_H
#include "sim.h"
#include "raylib.h"
#include "environment_art.h"
#define SWAT_LAMP_FACES 6
#define SWAT_LAMP_SIZE 384

typedef struct SwatLightingProgram {
    Shader shader;
    int lamp_shadows;
    int camera,sun_matrix,sun_map,lamp_map,rooms,centers,halves,origins,room_power,room_direction,exposure;
    int lamp_matrix[SWAT_LAMP_FACES];
    int orm,pbr,normal_map,roughness,metalness,spec_gloss;
    int environment,environment_normal,environment_normal_map,environment_roughness_map;
    int environment_size,environment_tile;
    int ground_blend;
    int skinning,skin_sets,skin_palette,skin_influences,emission,normal_green,normal_scale;
    int ibl,ibl_atlas,sun_direction,sun_energy,occlusion,occlusion_strength;
    int occlusion_uv;
    int contact,contact_map,contact_depth_map,contact_matrix;
} SwatLightingProgram;
typedef struct SwatLighting {
    bool initialized,enabled,prepared;
    SwatLightingProgram batch,mesh;
    Shader sky; int sky_forward,sky_right,sky_up,sky_scale,sky_size,sky_exposure;
    RenderTexture2D sun,sun_static,lamp,lamp_static;
    Matrix sun_matrix,lamp_matrix[SWAT_LAMP_FACES];
    Vector3 sun_direction;
    float exposure;
    uint32_t geometry;
    Vector3 shadow_origin;
    int shadow_face;
    int shadow_room,last_tick,updates,room_updates;
    uint32_t room_geometry[SWAT_MAX_ROOMS];
    uint32_t room_actors[SWAT_MAX_ROOMS];
    uint32_t actor_signature[SWAT_MAX_ACTORS];
    int actor_changed_tick[SWAT_MAX_ACTORS];
    unsigned int sun_geometry_updates,actor_room_updates;
    bool room_ready[SWAT_MAX_ROOMS],split_shadows;
    bool cutaway,lamp_shadows; // Receiving toggle for renderer diagnostics; default on.
    unsigned int surface_normal,surface_roughness;
    Texture2D environment_atlas,environment_sky;
    float environment_scale;
    Vector3 sun_energy;
    int sky_map,sky_ibl,sky_map_scale;
    RenderTexture2D contact_depth,contact_ao;
    Shader contact_shader;
    int contact_projection,contact_inverse,contact_size;
    Matrix contact_matrix;
    bool contact_ready,contact_enabled;
    Vector3 contact_eye;
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
// Static room depths are cached independently; moving actors are composited
// over them. shadow_room selects the room being drawn, or -1 for the sun.
void swat_lighting_prepare_split(SwatLighting* light,const SwatSim* sim,Vector3 eye,
                           bool cutaway,SwatShadowSceneContext geometry,SwatShadowSceneContext actors,void* context);
bool swat_lighting_face_intersects(const SwatLighting* light,Vector3 center,Vector3 half);
bool swat_lighting_room_intersects(const SwatRoom* room,Vector3 center,Vector3 half);
// Screen-space contact occlusion: half-resolution depth, no physics queries.
// Recomputed from the current camera/geometry; only indirect light is darkened.
void swat_lighting_contact(SwatLighting* light,const SwatSim* sim,Camera3D camera,int width,int height,
                           SwatShadowSceneContext draw,void* context);
// Called inside BeginMode3D; the shader never touches text or HUD compositing.
void swat_lighting_begin(SwatLighting* light,SwatEnvironmentArt* art,const SwatWorld* world,Vector3 camera);
void swat_lighting_sky(SwatLighting* light,Camera3D camera,int width,int height);
void swat_lighting_end(SwatLighting* light,SwatEnvironmentArt* art);
// Original glTF normal and packed G-roughness/B-metalness maps. A negative
// roughness value selects source RGB-specular/R-gloss textures in the standard
// specular/roughness map slots. Reset after
// drawing the material so ordinary scene meshes retain their diffuse shading.
void swat_lighting_material(SwatLighting* light,Material material,bool enabled);
void swat_lighting_material_uv(SwatLighting* light,Material material,bool enabled,float normal_scale,int occlusion_uv);
// Ground-only reuse of four inactive material/environment sampler slots keeps
// the fragment program below the OpenGL 3.3 minimum of 16 texture units.
// Call after material_uv; it resets with the next ordinary material binding.
void swat_lighting_ground(SwatLighting* light,Texture2D mask,Texture2D dirt_color);
void swat_lighting_material_scaled(SwatLighting* light,Material material,bool enabled,float normal_scale);
// Independent R8 roughness/OpenGL normals for metric environment surfaces.
// Mesh size/tile remap the original door's unit-space positions to metre UVs.
void swat_lighting_surface(SwatLighting* light,Texture2D normal,Texture2D roughness,
                           Vector3 size,Vector2 tile,bool mesh);
#endif
