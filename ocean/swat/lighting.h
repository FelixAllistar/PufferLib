#ifndef SWAT_LIGHTING_H
#define SWAT_LIGHTING_H
#include "sim.h"
#include "raylib.h"
#include "environment_art.h"

typedef struct SwatLightingProgram {
    Shader shader;
    int camera,sun_matrix,lamp_matrix,sun_map,lamp_map,rooms,centers,halves,lamp_room,exposure;
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
} SwatLighting;
typedef void (*SwatShadowScene)(const SwatSim* sim,bool cutaway);

void swat_lighting_init(SwatLighting* light);
void swat_lighting_close(SwatLighting* light);
// Call outside any 3D/texture mode. Authority is read only; doors/destruction
// invalidate immediately, moving silhouettes refresh at most every four ticks.
void swat_lighting_prepare(SwatLighting* light,const SwatSim* sim,Vector3 eye,
                           bool cutaway,SwatShadowScene draw);
// Called inside BeginMode3D; the shader never touches text or HUD compositing.
void swat_lighting_begin(SwatLighting* light,SwatEnvironmentArt* art,const SwatWorld* world,Vector3 camera);
void swat_lighting_end(SwatLighting* light,SwatEnvironmentArt* art);
#endif
