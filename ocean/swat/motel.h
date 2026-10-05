#ifndef SWAT_MOTEL_H
#define SWAT_MOTEL_H
#include "world.h"
#define SWAT_MOTEL_ASSETS 40
#define SWAT_MOTEL_INSTANCES 146
typedef struct SwatMotelMaterial { float roughness,metalness; } SwatMotelMaterial;
typedef struct SwatMotelAsset {
    const char* file; b3Vec3 center,half;
    b3Vec3* vertices; int vertex_count; int32_t* indices; int triangle_count;
    const SwatMotelMaterial* materials; int material_count;
} SwatMotelAsset;
typedef struct SwatMotelInstance {
    int asset; b3Pos origin; b3Vec3 scale; float yaw;
    SwatMaterial material; bool door,roof;
} SwatMotelInstance;
const SwatMotelAsset* swat_motel_asset(int index);
const SwatMotelInstance* swat_motel_instance(int index);
void swat_motel_build(SwatWorld* world);
// Reconstruct canonical mesh collision after receiving the ordinary map boxes.
// Every instance transform is checked before replacing any collider.
bool swat_motel_bind_collision(SwatWorld* world);
#endif
