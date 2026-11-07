// World: the baked terrain shared by every sim, view and server in a process.
#ifndef CRET_WORLD_H
#define CRET_WORLD_H
#include "terra/terra.h"
#include "box3d/box3d.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct CretWorld {
    TerraParams params;
    TerraMap map;
    float size;                 // metres per side
    b3HeightFieldData* field;   // collision, shared by all physics worlds
    int refs;
} CretWorld;

// Loads build/cretaceous/worlds/<seed>_<hash>.terra or bakes and caches it.
// Returned worlds are shared per (seed, n, cell); release with cret_world_release.
CretWorld* cret_world_acquire(uint32_t seed, int n, float cell, int verbose);
void cret_world_release(CretWorld* w);
// Physics body holding the shared heightfield.
b3BodyId cret_world_add_ground(const CretWorld* w, b3WorldId world);

static inline float cret_height(const CretWorld* w, float x, float z) { return terra_height(&w->map, x, z); }
static inline float cret_water(const CretWorld* w, float x, float z) { return terra_water(&w->map, x, z); }
#ifdef __cplusplus
}
#endif
#endif
