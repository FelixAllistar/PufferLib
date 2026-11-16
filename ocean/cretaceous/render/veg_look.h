// How plant cards are made to look solid (docs/plants/README.md). GL-free:
// the mesh generator (veg_gen.h) and tests/test_veg.c read it, and the
// edge-on fade below is the same curve as veg_edge_keep in common.glsl.
//
// Three switches, each on by default and turned off with an environment
// variable for an A/B comparison:
//   VEG_LOOK_ROUND   CRET_VEG_ROUND=0   shading normals of every leaf card and
//                    blade lean out toward its two edges, as if the card were
//                    the top of a cylinder round its axis (baked in the mesh)
//   VEG_LOOK_CREASE  CRET_VEG_CREASE=0  frond leaflets, fern and grass blades
//                    on the full mesh get a centre crease: the edges fold back
//                    a little, so edge on they show a thin shaded wedge
//   VEG_LOOK_EDGE    CRET_VEG_EDGE=0    leaf cards seen nearly edge on erode
//                    away (veg.fs, veg_depth.fs), so none draws as a line;
//                    their backlit glow then follows the card's own normal
//                    (veg_light.glsl)
#ifndef CRET_VEG_LOOK_H
#define CRET_VEG_LOOK_H
#include <stdlib.h>

enum { VEG_LOOK_ROUND = 1, VEG_LOOK_CREASE = 2, VEG_LOOK_EDGE = 4, VEG_LOOK_ALL = 7 };

// The mesh switches the generator builds with (veg.c sets it from the
// environment before the meshes are built).
static int veg_look_mesh = VEG_LOOK_ALL;
static inline int veg_look_from_env(void) {
    static const char* const name[3] = {"CRET_VEG_ROUND", "CRET_VEG_CREASE", "CRET_VEG_EDGE"};
    int look = 0;
    for (int i = 0; i < 3; i++) { const char* e = getenv(name[i]); if (!e || atoi(e) != 0) look |= 1 << i; }
    return look;
}

// Rounding: each edge's normal leans this far (radians) from the card's
// normal toward its edge, about 26 degrees; across the card the light then
// falls off as across the top of a stem.
#define VEG_ROUND_TILT 0.45f
// Crease: each edge folds back by this much of the half width (a 14 degree
// fold). Seen face on the outline is unchanged: the edges move only along
// the card's normal.
#define VEG_CREASE_LIFT 0.25f
// Leaflets narrower than this (m, at scale 1: the cycads' 2 cm pinnae) stay
// flat: their fold would be under a pixel deep past arm's length.
#define VEG_CREASE_MIN_W 0.05f

// Edge-on fade. |n.v| is the cosine between the card's own (flat) normal and
// the view: a card is as wide on screen as that times its width. Each card
// erodes away over a short band that starts at its own cut, between
// VEG_EDGE_LO and VEG_EDGE_HI - VEG_EDGE_BAND by a per-card key in 0..1, so
// cards do not all leave at one angle. Above VEG_EDGE_HI (about 78 degrees
// off face on) every card is whole.
#define VEG_EDGE_LO 0.03f
#define VEG_EDGE_HI 0.2f
#define VEG_EDGE_BAND 0.05f
static inline float veg_edge_cut(float key) { return VEG_EDGE_LO + (VEG_EDGE_HI - VEG_EDGE_BAND - VEG_EDGE_LO) * key; }
// The share of a card's alpha kept (multiplies its coverage before the alpha test).
static inline float veg_edge_keep(float nv, float key) {
    float c = veg_edge_cut(key), t = (nv - c) / VEG_EDGE_BAND;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}
#endif
