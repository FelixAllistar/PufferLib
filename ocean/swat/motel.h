#ifndef SWAT_MOTEL_H
#define SWAT_MOTEL_H
#include "world.h"
#define SWAT_MOTEL_BASE_ASSETS 40
#define SWAT_MOTEL_BASE_INSTANCES 146
#define SWAT_MOTEL_ASSETS SWAT_MOTEL_MESH_CAPACITY
#define SWAT_MOTEL_UTILITY_INSTANCES 154
#define SWAT_MOTEL_INSTANCES 158
#define SWAT_MOTEL_DRESSING_ASSETS 9
#define SWAT_MOTEL_DRESSING_INSTANCES (4*SWAT_MOTEL_DRESSING_ASSETS)
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
typedef struct SwatMotelFencePart {b3Vec3 center,half;int triangles;SwatPart part;} SwatMotelFencePart;
int swat_motel_fence_part_count(void);
int swat_motel_fence_triangle_part(int triangle);
int swat_motel_fence_parent(const SwatWorld*,const SwatObject*);
bool swat_motel_fence_proxy(const SwatWorld*,const SwatObject*);
const SwatMotelAsset* swat_motel_asset(int index);
const SwatMotelInstance* swat_motel_instance(int index);
// Small visual fixtures inherit a real support; they add no hidden colliders.
// Returns the current supporting object, or -1 when that support is gone.
int swat_motel_dressing(const SwatWorld* world,int index,SwatMotelInstance* placement);
int swat_motel_dressing_parent(int index);
// Authored bulb anchor in a supported fixed bedside fixture (guest rooms 1..4).
bool swat_motel_lamp(const SwatWorld*,int room,b3Pos* origin);
bool swat_motel_lamp_switch(const SwatWorld*,int room,b3Pos* position);
// Retired imported wall supporting this physical section, or -1.
int swat_motel_wall_parent(const SwatWorld* world,const SwatObject* piece);
typedef struct SwatMotelEdge {
    int owner,neighbor;
    b3Pos origin;
    float yaw,roll,length,depth;
} SwatMotelEdge;
// Render-only cut edges: +X across core, +Y into survivor, +Z along edge.
int swat_motel_wall_edges(const SwatWorld*,const SwatObject*,SwatMotelEdge*,int capacity);
void swat_motel_build(SwatWorld* world);
// Reconstruct canonical mesh collision after receiving the ordinary map boxes.
// Every instance transform is checked before replacing any collider.
bool swat_motel_bind_collision(SwatWorld* world);
#endif
