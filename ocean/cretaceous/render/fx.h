// Combat feedback ("game feel"): client-side presentation of the sim's hit
// log (sim.h, CretHit; clients get theirs from snapshots, protocol v10). It
// reads the sim and never changes it, so rules, rewards and the RL contract
// are untouched.
//   When you land a blow: a hit-stop of 40-80 ms (by damage; the local view
//   freezes, the sim runs on), a camera shake by damage and by both animals'
//   mass, a hit sound and a hit marker on the reticle, and the target's health
//   bar for a few seconds.
//   When you are hit: a damage arc toward the attacker at the screen edge, a
//   red vignette pulse and a camera kick away from the blow.
//   Every hit nearby: blood droplets at the strike point, ground decals that
//   fade over ~30 s (FX_DECALS at most), a directional flinch and knockback
//   lean on the victim (dino.c); bleeding animals drip. Tail swipes leave a
//   faint arc trail.
// Settings (menu.c): camera shake and hit-stop, each 0 (off) to 1.
#ifndef CRET_FX_H
#define CRET_FX_H
#include "gfx.h"
#include "view.h"
#include "../hud.h"
#include "../audio.h"

#define FX_PARTICLES 768
#define FX_DECALS 64
#define FX_TRAIL 10          // samples per tail trail (about 0.18 s)
#define FX_RANGE 140.0f      // effects further from the camera are skipped

typedef struct FxParticle { float p[3], v[3], age, life, size; int drip; } FxParticle;
typedef struct FxDecal { float x, z, r, rot, age, life, alpha, seed; } FxDecal;
typedef struct FxTrail { float a[FX_TRAIL][3], b[FX_TRAIL][3], t[FX_TRAIL]; int n; } FxTrail;

typedef struct CretFxSettings { float shake, hitstop; } CretFxSettings;   // 0 off .. 1 full

typedef struct CretFx {
    const CretWorld* world; CretDinos* dinos;   // dinos: flinches and joint positions (NULL: capsules)
    uint32_t seen; bool primed;                 // hits consumed from the log
    FxParticle part[FX_PARTICLES]; int npart;
    FxDecal decal[FX_DECALS]; int decal_next, ndecal;
    FxTrail trail[CRET_MAX_AGENTS];
    float drip_t[CRET_MAX_AGENTS];
    float stop;                                 // hit-stop left (s)
    float trauma, time;                         // camera shake 0..1 (strength trauma^2)
    float kick_t, kick_amp, kick_dir[2];        // camera kick away from a blow (world xz)
    uint32_t rng;
    CretHudFeedback hud;
    // Last camera (billboards) and GL.
    float cam_pos[3], cam_right[3], cam_up[3];
    GfxProgram prog; GLuint vao, vbo; float* verts; int nverts, vcap; bool gl_ok;
    int stats_hits;                             // hits seen (tests, logs)
} CretFx;

bool cret_fx_init(CretFx* f, const CretWorld* w, CretDinos* dinos);
// Drops every effect and the HUD feedback (captures: each still starts clean).
void cret_fx_clear(CretFx* f);
// Once per frame, before the camera: takes new hits from sim's log (the live
// sim; a client's is filled from snapshots) and advances the effects.
// draw: the interpolated copy (positions). me: the local animal (-1: none).
void cret_fx_update(CretFx* f, const CretSim* sim, const CretSim* draw, int me, const CretFxSettings* st, CretAudio* au, float dt);
// True while a hit-stop holds the local view still.
bool cret_fx_frozen(const CretFx* f);
// Shake and kick on top of the game camera; also records it for billboards.
void cret_fx_camera(CretFx* f, CretCamera* cam);
// The view's draw hook (CretView.fx_draw): blood, decals and trails, in the
// HDR scene after the animals.
void cret_fx_draw(void* ctx, const CretView* v, const VegFrameDesc* fd);
void cret_fx_close(CretFx* f);
#endif
