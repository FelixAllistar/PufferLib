// Nests and eggs, and the small food animals carry (render/nest.c). Each
// nest is a procedural ground nest: a low mound of soil with a bowl lined
// with leaf and fern litter and sticks around the rim, built on the terrain
// (cret_height) so it sits on slopes, sized to the species, and lit as the
// ground is (shaders/nest.fs); plants are not drawn inside it (veg.c
// clearings). The eggs in it follow the sim's egg count: elongated for
// theropods, rounder for ornithischians, matte, in the sun's shadow cascades. A nest being built rises from the ground over the 5 s
// hold. Eggs and caught critters that lie on the ground or hang from a
// carrier's jaws are drawn here too. Presentation only: it reads the sim and
// never changes it. Compiled by including render/nest.c from view.c (the
// player's build lists stay as they are), next to the agent capsules.
#ifndef CRET_RENDER_NEST_H
#define CRET_RENDER_NEST_H
#include "gfx.h"
#include "csm.h"
#include "../sim.h"

#define NEST_DRAW_RANGE 260.0f   // m from the camera
#define NEST_SHOW_MAX 48         // display-only nests (the gallery)
#define NEST_BUILDS 8            // nests being built at once that are drawn rising
#define NEST_CACHE (CRET_MAX_NESTS + NEST_SHOW_MAX + NEST_BUILDS)

// A nest to show that is not in the sim (the gallery's rows): species, where,
// how many eggs, and how far built (1: finished).
typedef struct CretNestShow { int species, eggs; float x, z, built; } CretNestShow;

typedef struct NestMesh { GLuint vao, vbo; int nverts; int species; float x, z, built; bool used; } NestMesh;
typedef struct CretNestDraw {
    bool ok;
    NestMesh mesh[NEST_CACHE];                 // by draw slot: sim nests, shown nests, builds
    GLuint egg_vao[CRET_SPECIES], egg_vbo[CRET_SPECIES]; int egg_n[CRET_SPECIES];   // one egg mesh per species, at its size
    CretNestShow show[NEST_SHOW_MAX]; int nshow;
    float build_t[CRET_MAX_AGENTS];            // seconds each animal has held the nest action (remote animals carry no progress)
    float last_time;
    int drawn_nests, drawn_eggs;               // last frame
} CretNestDraw;

void cret_nest_draw_init(CretNestDraw* d);
void cret_nest_draw_close(CretNestDraw* d);
// Egg length and width (m) for a species, and whether it is elongated (theropods).
void cret_egg_size(int species, float* length, float* width);
// Advances the build timers and rebuilds changed meshes (once a frame, before drawing).
void cret_nest_draw_update(CretNestDraw* d, const CretSim* sim, const CretWorld* w, const float cam[3], float time);
// Colour pass: mounds and eggs with the nest program `np`, caught critters
// with the object program `op` (both bound with their frame uniforms by view.c).
// head(slot) gives a carrier's drawn head (false: use the sim's mouth point).
typedef bool (*CretHeadFn)(const void* ctx, int slot, float out[3]);
void cret_nest_draw(CretNestDraw* d, const CretSim* sim, const GfxProgram* np, const GfxProgram* op, unsigned int ball_vao, int ball_tris, bool ball_indexed,
                    const float cam[3], CretHeadFn head, const void* head_ctx);
// Shadow casters for one cascade (program p: u_model, u_viewproj already set). Returns draws.
int cret_nest_draw_shadow(CretNestDraw* d, const CretSim* sim, const GfxProgram* p, const CretCsm* c, int k, const float cam[3], CretHeadFn head, const void* head_ctx);
// The circles (x, z, radius) of the nests drawn now, for the plant renderer's clearings. Returns how many.
int cret_nest_clearings(const CretNestDraw* d, const CretSim* sim, float* xzr, int max);
#endif
