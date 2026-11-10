// Critter placeholders (critters.h in the sim): each small animal drawn as a
// few scaled spheres with the object shader, tinted per kind and scaled to
// its body length, bobbing or hopping while it moves. Presentation only; it
// reads the sim's critter pool and never changes it. Swap cret_critters_draw
// for real models later: it is the only entry point.
// Compiled by including render/critters.c from view.c (build.sh lists the
// player sources itself), next to the agent capsules it shares a pass with.
#ifndef CRET_RENDER_CRITTERS_H
#define CRET_RENDER_CRITTERS_H
#include "gfx.h"
#include "../sim.h"

#define CRET_CRITTER_DRAW_RANGE 70.0f   // m from the camera

// Draws the critters near the camera with program p (u_model, u_color) and
// the sphere mesh `ball` (radius 1). Positions are smoothed per pool slot
// toward the sim's (a client gets them at 15 Hz). Returns spheres drawn.
int cret_critters_draw(const CretSim* sim, const GfxProgram* p, unsigned int ball_vao, int ball_tris, bool indexed, const float cam[3], float time);
#endif
