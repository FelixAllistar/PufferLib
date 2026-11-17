// Chain stability checks for the locomotion ragdoll (tools/loco_play.c
// --stability, tests/test_loco.c): the clip's first pose held by the PD
// springs with the pelvis pinned 1 m up (3 s), then free on the ground (up to
// 5 s, until it falls: nothing balances it), then dropped from 0.5 m (3 s).
// They test the numerics, not balance: the chain must not come apart, blow
// up or drift from the pose it is told to hold.
#ifndef CRET_LOCO_CHECK_H
#define CRET_LOCO_CHECK_H
#include <time.h>
#include "loco_env.h"

typedef struct LocoStab {
    float pin_mean, pin_worst, pin_sep, pin_speed;   // joint drift (deg), largest joint separation (m), fastest body at the end (m/s)
    float free_up, free_mean, free_worst, free_sep;  // seconds standing; drift after 0.5 s (deg); separation (m)
    float drop_sep, drop_speed, drop_mean;           // separation (m), fastest body (m/s), drift once settled (deg)
    double us_per_step;                              // microseconds per ragdoll physics step
    bool pin_ok, free_ok, drop_ok;
} LocoStab;

// Mean and largest joint error (deg) of doll i against target rotations rrot (model space).
static void loco_joint_error(LocoWorld* w, int i, const b3Quat* rrot, float* mean, float* worst) {
    loco_doll_get(&w->phys, i, w->pos, w->rot, w->vel, w->angvel);
    float s = 0, m = 0; int n = 0;
    for (int b = 1; b < w->rig.nbody; b++) {
        const LocoBody* B = &w->rig.body[b];
        float a = lq_angle(lq_inv_mul(w->rot[B->parent], w->rot[b]), lq_inv_mul(rrot[B->parent], rrot[b])) * 57.2958f;
        s += a; m = fmaxf(m, a); n++;
    }
    *mean = n ? s / n : 0; *worst = m;
}
static float loco_max_speed(LocoWorld* w, int i) {
    loco_doll_get(&w->phys, i, NULL, NULL, w->vel, w->angvel); float m = 0;
    for (int b = 0; b < w->rig.nbody; b++) m = fmaxf(m, sqrtf(lv_len2(w->vel[b])));
    return m;
}
static double loco_clock(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

// Runs the three checks with cfg at the given substep count. Pass marks: the
// pinned pose within 5 degrees on average with joints apart by under 1.5 cm
// and at rest; standing and dropped, joints apart by under 2 and 5 cm, no body
// faster than 20 m/s (a 0.5 m fall reaches 3.1; more is a whip or a blow-up),
// everything finite. False if the world cannot be built.
static bool loco_check_stability(const LocoConfig* base, int substeps, LocoStab* o, char* err, size_t errn) {
    memset(o, 0, sizeof *o);
    LocoConfig c = *base; c.num_agents = 1; c.substeps = substeps;
    LocoWorld* w = (LocoWorld*)calloc(1, sizeof(LocoWorld));
    if (!loco_world_init(w, &c, 1, err, errn)) { free(w); return false; }
    float h = 1.0f / c.sim_hz; int steps = 0;
    b3Vec3 pos[LOCO_MAX_BODIES], zero[LOCO_MAX_BODIES], lifted[LOCO_MAX_BODIES]; b3Quat rot[LOCO_MAX_BODIES], dev[LOCO_MAX_BODIES];
    loco_ref_sample(&w->ref, 0, pos, rot, NULL, NULL);
    for (int b = 0; b < w->rig.nbody; b++) zero[b] = lv(0, 0, 0);
    loco_targets(w, rot, NULL, dev);
    double t0 = loco_clock();
    // Pinned 1 m up.
    for (int b = 0; b < w->rig.nbody; b++) lifted[b] = b3Add(pos[b], lv(0, 1.0f, 0));
    loco_doll_set(&w->phys, 0, lifted, rot, zero, zero); loco_doll_pin(&w->phys, 0, true); loco_doll_targets(&w->phys, 0, dev);
    float mean = 0, worst = 0;
    for (int s = 0; s < 3 * c.sim_hz; s++, steps++) {
        b3World_Step(w->phys.world, h, substeps);
        o->pin_sep = fmaxf(o->pin_sep, loco_doll_separation(&w->phys, 0));
    }
    loco_joint_error(w, 0, rot, &o->pin_mean, &o->pin_worst);
    o->pin_speed = loco_max_speed(w, 0);
    o->pin_ok = o->pin_sep < 0.015f && o->pin_mean < 5.0f && isfinite(o->pin_mean) && o->pin_speed < 0.1f;
    // Free on the ground.
    loco_doll_pin(&w->phys, 0, false);
    loco_doll_set(&w->phys, 0, pos, rot, zero, zero);
    o->free_up = 5.0f;
    for (int s = 0; s < 5 * c.sim_hz; s++, steps++) {
        b3World_Step(w->phys.world, h, substeps);
        o->free_sep = fmaxf(o->free_sep, loco_doll_separation(&w->phys, 0));
        loco_joint_error(w, 0, rot, &mean, &worst);
        if (s + 1 == c.sim_hz / 2) { o->free_mean = mean; o->free_worst = worst; }
        if (loco_doll_fallen(&w->phys, 0) || w->pos[0].y < c.fall_height * w->pelvis_h) { o->free_up = (s + 1) * h; break; }
    }
    o->free_ok = o->free_sep < 0.02f && isfinite(mean) && loco_max_speed(w, 0) < 20.0f;
    // Dropped from 0.5 m.
    for (int b = 0; b < w->rig.nbody; b++) lifted[b] = b3Add(pos[b], lv(0, 0.5f, 0));
    loco_doll_set(&w->phys, 0, lifted, rot, zero, zero);
    for (int s = 0; s < 3 * c.sim_hz; s++, steps++) {
        b3World_Step(w->phys.world, h, substeps);
        o->drop_sep = fmaxf(o->drop_sep, loco_doll_separation(&w->phys, 0));
        o->drop_speed = fmaxf(o->drop_speed, loco_max_speed(w, 0));
    }
    loco_joint_error(w, 0, rot, &o->drop_mean, &worst);
    o->drop_ok = o->drop_sep < 0.05f && o->drop_speed < 20.0f && isfinite(o->drop_mean);
    o->us_per_step = (loco_clock() - t0) / steps * 1e6;
    loco_world_close(w); free(w);
    return true;
}
#endif
