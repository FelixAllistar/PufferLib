// Locomotion by imitation (DeepMimic, Peng et al. 2018): a physics ragdoll
// of a dinosaur (loco_rig.h, loco_sim.h) learns to move like one of its
// animation clips. docs/locomotion/README.md explains the design for the
// owner; this file is the env, independent of the trainer (loco_binding.h
// adapts it to PufferLib, tools/loco_play.c and tests/test_loco.c drive it
// directly).
//
//   Observation (LOCO_OBS floats): for each body, padded to LOCO_MAX_BODIES,
//     its origin relative to the pelvis and its rotation (6D: two columns),
//     centre-of-mass velocity and angular velocity, all in the pelvis
//     heading frame (yaw only); then the pelvis height, the clip phase
//     (sin, cos) and the target speed.
//   Action (LOCO_MAX_DOF floats, the unused tail of it ignored): per joint
//     an offset added to the clip's pose at this instant (residual control):
//     three for a ball joint (a rotation vector in the joint frame), one for
//     a hinge. Each output is action_unit radians per unit, clamped to
//     +-action_scale radians. The trainer starts every action's noise at
//     std 1, so action_unit sets the initial exploration (0.1 rad, near
//     DeepMimic's fixed 0.05) while action_scale caps the offset.
//   Reward: DeepMimic's imitation reward, a weighted sum of
//     exp(-k * error) for the joint rotations, joint angular velocities, end
//     effectors (feet, head, tail tip) and the centre of mass's velocity.
//     Exactly 1 when the body matches the clip.
//   Episode: starts at a random phase of the clip in the clip's pose and
//     velocities (reference state initialisation), ends when a body other
//     than a foot touches the ground (early termination) or after
//     episode_seconds.
//   Shoves (off unless push_interval > 0): every push_interval seconds on
//     average a horizontal push in a random direction hits the pelvis, spine
//     or chest for push_seconds, sized to change the whole animal's speed by
//     push_min..push_max m/s. The reward stays the clip's, so the policy learns
//     to take the hit and get back into its walk: the recoveries are motion the
//     clips do not have (tools/loco_bake.c turns them into game clips).
#ifndef CRET_LOCO_ENV_H
#define CRET_LOCO_ENV_H
#include "loco_sim.h"

#define LOCO_BODY_OBS 15
#define LOCO_OBS (LOCO_MAX_BODIES * LOCO_BODY_OBS + 4)
#define LOCO_MAX_AGENTS 256

typedef struct LocoConfig {
    char dino[256], clip[32];
    int num_agents, policy_hz, sim_hz, substeps;
    float episode_seconds, action_scale, action_unit, lane;
    // Reward weights and scales (DeepMimic's: 0.65, 0.1, 0.15, 0.1; 2, 0.1, 40, 10; here k_ee is 10
    // for a 3 m animal, and the COM term compares velocities, see loco_com_vel).
    float w_pose, w_vel, w_ee, w_com, k_pose, k_vel, k_ee, k_com;
    LocoGains gains;
    float fall_height;     // also fallen when the pelvis is below this share of the clip's pelvis height
    float weld_range;      // joints the clips move less than this (rad) are welded (loco_rig.h); 0 keeps every joint
    float push_interval, push_min, push_max, push_seconds;   // shoves: mean seconds between them (0: none), speed change (m/s), duration (s)
} LocoConfig;

static LocoConfig loco_default_config(void) {
    LocoConfig c; memset(&c, 0, sizeof c);
    snprintf(c.dino, sizeof c.dino, "ocean/cretaceous/assets/dinos/raptor.dino");
    snprintf(c.clip, sizeof c.clip, "walk");
    c.num_agents = 32; c.policy_hz = 30; c.sim_hz = 120; c.substeps = 4;
    c.episode_seconds = 15.0f; c.action_scale = 0.5f; c.action_unit = 0.1f; c.lane = 4.0f;
    c.w_pose = 0.65f; c.w_vel = 0.1f; c.w_ee = 0.15f; c.w_com = 0.1f;
    c.k_pose = 2.0f; c.k_vel = 0.1f; c.k_ee = 10.0f; c.k_com = 10.0f;
    c.gains = loco_default_gains(); c.fall_height = 0.5f; c.weld_range = LOCO_MIN_RANGE;
    c.push_interval = 0; c.push_min = 0.5f; c.push_max = 1.5f; c.push_seconds = 0.1f;
    return c;
}

// Per-agent episode state and the running sums the logs report.
typedef struct LocoAgent {
    double t;              // clip time (s): the start phase plus the time since the reset
    float elapsed, ret, err_pose, err_ee; int steps; bool fallen;
    float r_pose, r_vel, r_ee, r_com;   // last step's reward terms
    float start_x;         // pelvis x at the reset
    int push_left, push_body; b3Vec3 push_force;   // the shove under way: physics steps left, body, force (N)
} LocoAgent;

typedef struct LocoStats { double episodes, falls, length, ret, distance, reward, pose_deg, ee_cm, steps; } LocoStats;

typedef struct LocoWorld {
    LocoConfig cfg; LocoRig rig; LocoRef ref; LocoPhys phys;
    int clip_index; unsigned int rng;
    LocoAgent ag[LOCO_MAX_AGENTS];
    LocoStats stats;
    float pelvis_h;        // the clip's mean pelvis height
    // Scratch for one agent's state and its reference (model space).
    b3Vec3 pos[LOCO_MAX_BODIES], vel[LOCO_MAX_BODIES], angvel[LOCO_MAX_BODIES];
    b3Quat rot[LOCO_MAX_BODIES];
    b3Vec3 rpos[LOCO_MAX_BODIES], rvel[LOCO_MAX_BODIES], rangvel[LOCO_MAX_BODIES];
    b3Quat rrot[LOCO_MAX_BODIES];
} LocoWorld;

static inline unsigned int loco_rand(unsigned int* s) { unsigned int x = *s ? *s : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static inline float loco_randf(unsigned int* s) { return (float)(loco_rand(s) >> 8) * (1.0f / 16777216.0f); }

// Loads the .dino, builds the rig, the reference and the world. False with a
// message in err when the file, the clip or the rig cannot be used.
static bool loco_world_init(LocoWorld* w, const LocoConfig* cfg, unsigned int seed, char* err, size_t errn) {
    memset(w, 0, sizeof *w);
    w->cfg = *cfg; w->rng = seed ? seed : 1;
    if (cfg->num_agents < 1 || cfg->num_agents > LOCO_MAX_AGENTS) { snprintf(err, errn, "num_agents must be 1..%d", LOCO_MAX_AGENTS); return false; }
    if (cfg->sim_hz % cfg->policy_hz) { snprintf(err, errn, "sim_hz must be a multiple of policy_hz"); return false; }
    DinoFile* d = (DinoFile*)calloc(1, sizeof(DinoFile));
    if (!df_read(d, cfg->dino)) { snprintf(err, errn, "cannot read %.200s", cfg->dino); free(d); return false; }
    if (loco_unscale_bind(d) == 0) { snprintf(err, errn, "%.200s: its joints' bind scales differ", cfg->dino); df_free(d); free(d); return false; }
    w->clip_index = loco_clip_index(d, cfg->clip);
    if (w->clip_index < 0 || !d->clip[w->clip_index].loop) { snprintf(err, errn, "%.200s has no looping clip %.31s", cfg->dino, cfg->clip); df_free(d); free(d); return false; }
    int clips[8], nc = loco_limit_clips(d, w->clip_index, clips);
    bool ok = loco_rig_build(&w->rig, d, clips, nc, cfg->weld_range, err, errn) && loco_ref_build(&w->ref, d, &w->rig, w->clip_index);
    df_free(d); free(d);
    if (!ok) return false;
    if (w->rig.ndof > LOCO_MAX_DOF) { snprintf(err, errn, "rig has %d action dimensions (max %d)", w->rig.ndof, LOCO_MAX_DOF); return false; }
    for (int f = 0; f < w->ref.period; f++) w->pelvis_h += w->ref.pos[f * w->ref.nbody].y / w->ref.period;
    b3Vec3 origin[LOCO_MAX_AGENTS];
    for (int i = 0; i < cfg->num_agents; i++) origin[i] = lv(0, 0, cfg->lane * i);
    loco_phys_create(&w->phys, &w->rig, cfg->num_agents, origin, cfg->gains, (float)cfg->sim_hz * cfg->substeps);
    return true;
}
static void loco_world_close(LocoWorld* w) { loco_phys_destroy(&w->phys); loco_ref_free(&w->ref); }

// Yaw-only heading of a rotation (the pelvis's forward, +X, projected on the ground).
static inline b3Quat loco_heading(b3Quat q) {
    b3Vec3 f = lq_rot(q, lv(1, 0, 0));
    return lq_axis_angle(lv(0, 1, 0), atan2f(-f.z, f.x));
}
// Reference state of agent i at its clip time t (model space, the doll's origin removed).
static void loco_ref_at(LocoWorld* w, double t) { loco_ref_sample(&w->ref, t, w->rpos, w->rrot, w->rvel, w->rangvel); }
// A joint target clamped inside the joint's limits. Box3D's limits are soft
// constraints, and a stiff spring aimed past one pushes straight through it
// (by 80 degrees under random actions), so no target lies outside: a ball
// joint's target is split into swing and twist about the bone (as Box3D
// measures them) and each is clamped; a hinge's angle is clamped.
static b3Quat loco_clamp_target(const LocoBody* B, b3Quat d) {
    if (d.s < 0) d = b3NegateQuat(d);
    float tl = sqrtf(d.v.z * d.v.z + d.s * d.s);
    b3Quat tw = tl > 1e-6f ? lq_make(0, 0, d.v.z / tl, d.s / tl) : b3Quat_identity;
    float ta = lq_twist_z(tw);
    if (B->jtype == LJ_HINGE) return lq_axis_angle(lv(0, 0, 1), fminf(fmaxf(ta, B->lo), B->hi));
    b3Quat sw = lq_mul(d, lq_conj(tw));
    b3Vec3 sv = lq_log(sw); float sa = sqrtf(lv_len2(sv));
    if (sa > B->cone) sw = lq_exp(b3MulSV(B->cone / sa, sv));
    return lq_mul(sw, lq_axis_angle(lv(0, 0, 1), fminf(fmaxf(ta, B->twist_lo), B->twist_hi)));
}

// Target joint deviations: the clip at time t plus the action offsets, inside the joint limits.
static void loco_targets(const LocoWorld* w, const b3Quat* rrot, const float* act, b3Quat* dev) {
    const LocoRig* r = &w->rig; const float s = w->cfg.action_scale, u = w->cfg.action_unit;
    for (int b = 1; b < r->nbody; b++) {
        const LocoBody* B = &r->body[b];
        if (w->cfg.gains.passive && loco_is_tail(B)) { dev[b] = b3Quat_identity; continue; }
        b3Quat ref = loco_joint_dev(B, rrot[B->parent], rrot[b]);
        if (!act) { dev[b] = loco_clamp_target(B, ref); continue; }
        const float* a = act + B->act0;
        float a0 = fminf(fmaxf(a[0] * u, -s), s);
        if (B->jtype == LJ_HINGE) dev[b] = loco_clamp_target(B, lq_mul(ref, lq_axis_angle(lv(0, 0, 1), a0)));
        else {
            float a1 = fminf(fmaxf(a[1] * u, -s), s), a2 = fminf(fmaxf(a[2] * u, -s), s);
            dev[b] = loco_clamp_target(B, lq_mul(ref, lq_exp(lv(a0, a1, a2))));
        }
    }
}

// Reference state initialisation: agent i starts at a random phase of the
// clip (or at phase, when phase >= 0) in the clip's pose and velocities,
// placed so its pelvis starts near its lane's origin.
static void loco_reset_agent(LocoWorld* w, int i, float phase) {
    LocoAgent* a = &w->ag[i];
    float ph = phase >= 0 ? phase : loco_randf(&w->rng);
    double t = (double)ph * w->ref.duration;
    loco_ref_at(w, t);
    // The doll's origin moves back by the travel so far, so every episode starts at x near 0. The
    // clips let feet sink into the ground by a centimetre or two; starting a light foot inside the
    // ground would have the contact solver flip it, so the doll is lifted to stand on the ground.
    float lift = fmaxf(0.0f, -loco_lowest(&w->rig, w->rpos, w->rrot));
    w->phys.doll[i].origin = lv(-w->ref.speed * (float)t, lift, w->cfg.lane * i);
    loco_doll_set(&w->phys, i, w->rpos, w->rrot, w->rvel, w->rangvel);
    b3Quat dev[LOCO_MAX_BODIES]; loco_targets(w, w->rrot, NULL, dev);
    loco_doll_targets(&w->phys, i, dev);
    memset(a, 0, sizeof *a);
    a->t = t; a->start_x = w->rpos[0].x;
}
// Starts a shove on agent i: body b's force for the push's duration changes the
// whole animal's velocity by dv (m/s, the impulse spread over the duration).
static void loco_push(LocoWorld* w, int i, int b, b3Vec3 dv, float seconds) {
    LocoAgent* a = &w->ag[i]; int n = (int)lroundf(seconds * w->cfg.sim_hz); if (n < 1) n = 1;
    a->push_body = b; a->push_left = n; a->push_force = b3MulSV(w->rig.total_mass * w->cfg.sim_hz / n, dv);
}
// A random shove for agent i when one is due (none while one is under way).
static void loco_push_maybe(LocoWorld* w, int i) {
    const LocoConfig* c = &w->cfg; LocoAgent* a = &w->ag[i];
    if (c->push_interval <= 0 || a->push_left > 0 || loco_randf(&w->rng) * c->push_interval * c->policy_hz >= 1.0f) return;
    int nb = w->rig.nbody < 3 ? w->rig.nbody : 3;   // pelvis, spine, chest: the first bodies of every table
    int b = (int)(loco_randf(&w->rng) * nb); if (b >= nb) b = nb - 1;
    float ang = 2.0f * (float)M_PI * loco_randf(&w->rng), sp = c->push_min + (c->push_max - c->push_min) * loco_randf(&w->rng);
    loco_push(w, i, b, lv(sp * cosf(ang), 0, sp * sinf(ang)), c->push_seconds);
}
static void loco_reset_all(LocoWorld* w) { for (int i = 0; i < w->cfg.num_agents; i++) loco_reset_agent(w, i, -1); }

// Mass-weighted velocity of the centre of mass of a state. A velocity, not a
// position: a position term would compare the forward travel too, and any
// small speed difference grows into a distance that zeroes the term within
// seconds (run 2 walked 29 m to the clip's 27 m and scored 0 on it). The
// velocity still charges an animal that walks in place or too fast.
// The sim reports each body's velocity at its centre of mass; the reference's
// are at the bone (body origin), so they get the offset's spin added (ref).
static b3Vec3 loco_com_vel(const LocoWorld* w, const b3Quat* rot, const b3Vec3* vel, const b3Vec3* angvel, bool ref) {
    b3Vec3 c = lv(0, 0, 0);
    for (int b = 0; b < w->rig.nbody; b++) {
        b3Vec3 v = ref ? b3Add(vel[b], b3Cross(angvel[b], lq_rot(rot[b], w->rig.body[b].com))) : vel[b];
        c = b3MulAdd(c, w->rig.body[b].mass, v);
    }
    return b3MulSV(1.0f / w->rig.total_mass, c);
}
// End-effector tip of body b relative to the pelvis in its heading frame, with the absolute height.
static inline b3Vec3 loco_ee(const LocoBody* B, b3Vec3 p, b3Quat q, b3Vec3 pel, b3Quat hd) {
    b3Vec3 e = b3Add(p, lq_rot(q, B->ee)), r = lq_inv_rot(hd, b3Sub(e, pel));
    r.y = e.y; return r;
}

// Imitation reward of agent i's current state (w->pos...) against the
// reference (w->rpos...), and its terms. Tail bodies are left out of the
// pose, velocity and end-effector terms when the tail is passive.
static float loco_reward(LocoWorld* w, LocoAgent* a) {
    const LocoRig* r = &w->rig; const LocoConfig* c = &w->cfg; bool skip_tail = c->gains.passive;
    float ep = lq_angle(w->rot[0], w->rrot[0]); ep *= ep;
    b3Vec3 dw0 = b3Sub(w->angvel[0], w->rangvel[0]); float ev = lv_len2(dw0);
    for (int b = 1; b < r->nbody; b++) {
        const LocoBody* B = &r->body[b];
        if (skip_tail && loco_is_tail(B)) continue;
        float ang = lq_angle(lq_inv_mul(w->rot[B->parent], w->rot[b]), lq_inv_mul(w->rrot[B->parent], w->rrot[b]));
        ep += ang * ang;
        b3Vec3 ws = b3Sub(w->angvel[b], w->angvel[B->parent]), wr = b3Sub(w->rangvel[b], w->rangvel[B->parent]);
        ev += lv_len2(b3Sub(ws, wr));
    }
    b3Quat hs = loco_heading(w->rot[0]), hr = loco_heading(w->rrot[0]); float ee = 0;
    for (int k = 0; k < r->nee; k++) {
        int b = r->ee_body[k];
        if (skip_tail && loco_is_tail(&r->body[b])) continue;
        b3Vec3 es = loco_ee(&r->body[b], w->pos[b], w->rot[b], w->pos[0], hs), er = loco_ee(&r->body[b], w->rpos[b], w->rrot[b], w->rpos[0], hr);
        ee += lv_len2(b3Sub(es, er));
    }
    float ec = lv_len2(b3Sub(loco_com_vel(w, w->rot, w->vel, w->angvel, false), loco_com_vel(w, w->rrot, w->rvel, w->rangvel, true)));
    a->r_pose = expf(-c->k_pose * ep); a->r_vel = expf(-c->k_vel * ev); a->r_ee = expf(-c->k_ee * ee); a->r_com = expf(-c->k_com * ec);
    a->err_pose += sqrtf(ep / r->nbody); a->err_ee += sqrtf(ee / (r->nee > 0 ? r->nee : 1));
    return c->w_pose * a->r_pose + c->w_vel * a->r_vel + c->w_ee * a->r_ee + c->w_com * a->r_com;
}

// Agent i's observation (LOCO_OBS floats) from its current state in w->pos...
static void loco_observe_state(const LocoWorld* w, const LocoAgent* a, float* o) {
    const LocoRig* r = &w->rig; b3Quat hd = loco_heading(w->rot[0]); b3Vec3 pel = w->pos[0];
    memset(o, 0, sizeof(float) * LOCO_OBS);
    for (int b = 0; b < r->nbody; b++) {
        float* q = o + b * LOCO_BODY_OBS;
        b3Vec3 p = lq_inv_rot(hd, b3Sub(w->pos[b], pel));
        b3Quat lr = lq_inv_mul(hd, w->rot[b]);
        b3Vec3 c0 = lq_rot(lr, lv(1, 0, 0)), c1 = lq_rot(lr, lv(0, 1, 0));
        b3Vec3 v = lq_inv_rot(hd, w->vel[b]), av = lq_inv_rot(hd, w->angvel[b]);
        q[0] = p.x; q[1] = p.y; q[2] = p.z; q[3] = c0.x; q[4] = c0.y; q[5] = c0.z; q[6] = c1.x; q[7] = c1.y; q[8] = c1.z;
        q[9] = 0.2f * v.x; q[10] = 0.2f * v.y; q[11] = 0.2f * v.z; q[12] = 0.1f * av.x; q[13] = 0.1f * av.y; q[14] = 0.1f * av.z;
    }
    float* t = o + LOCO_MAX_BODIES * LOCO_BODY_OBS;
    double ph = fmod(a->t, (double)w->ref.duration) / w->ref.duration * 2.0 * M_PI;
    t[0] = pel.y; t[1] = (float)sin(ph); t[2] = (float)cos(ph); t[3] = 0.2f * w->ref.speed;
}
static void loco_observe(LocoWorld* w, int i, float* o) {
    loco_doll_get(&w->phys, i, w->pos, w->rot, w->vel, w->angvel);
    loco_observe_state(w, &w->ag[i], o);
}

// One policy step for every agent: actions (LOCO_MAX_DOF floats each, or
// NULL for the plain PD-tracking baseline: the springs chase the clip),
// physics at sim_hz with the clip targets advanced every physics step, then
// rewards and terminals (1 on a fall or at the episode's end, after which
// the agent has already been reset). Rewards and terminals may be NULL.
static void loco_step(LocoWorld* w, const float* const* act, float* reward, float* terminal) {
    const LocoConfig* c = &w->cfg; int n = c->num_agents, k = c->sim_hz / c->policy_hz;
    float h = 1.0f / c->sim_hz; b3Quat dev[LOCO_MAX_BODIES];
    for (int i = 0; i < n; i++) { w->ag[i].fallen = false; loco_push_maybe(w, i); }
    for (int s = 0; s < k; s++) {
        for (int i = 0; i < n; i++) {
            LocoAgent* a = &w->ag[i];
            loco_ref_sample(&w->ref, a->t + (s + 1) * h, NULL, w->rrot, NULL, NULL);
            loco_targets(w, w->rrot, act ? act[i] : NULL, dev);
            loco_doll_targets(&w->phys, i, dev);
            if (a->push_left > 0) {   // Box3D clears forces after every step
                b3Body_ApplyForceToCenter(w->phys.doll[i].body[a->push_body], a->push_force, true);
                a->push_left--;
            }
        }
        b3World_Step(w->phys.world, h, c->substeps);
        for (int i = 0; i < n; i++) if (!w->ag[i].fallen && loco_doll_fallen(&w->phys, i)) w->ag[i].fallen = true;
    }
    for (int i = 0; i < n; i++) {
        LocoAgent* a = &w->ag[i];
        a->t += 1.0 / c->policy_hz; a->elapsed += 1.0f / c->policy_hz; a->steps++;
        loco_doll_get(&w->phys, i, w->pos, w->rot, w->vel, w->angvel);
        loco_ref_at(w, a->t);
        if (w->pos[0].y < c->fall_height * w->pelvis_h) a->fallen = true;
        float r = loco_reward(w, a);
        a->ret += r;
        bool done = a->fallen || a->elapsed >= c->episode_seconds - 1e-4f;
        if (reward) reward[i] = r;
        if (terminal) terminal[i] = done ? 1.0f : 0.0f;
        w->stats.reward += r; w->stats.steps += 1;
        if (done) {
            LocoStats* st = &w->stats;
            st->episodes += 1; st->falls += a->fallen; st->length += a->elapsed; st->ret += a->ret;
            st->distance += w->pos[0].x - a->start_x;
            st->pose_deg += a->err_pose / a->steps * (180.0 / M_PI); st->ee_cm += a->err_ee / a->steps * 100.0;
            loco_reset_agent(w, i, -1);
        }
    }
}

// FNV-1a over every body's position and rotation: equal for equal runs.
static uint64_t loco_hash(const LocoWorld* w) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < w->cfg.num_agents; i++) for (int b = 0; b < w->rig.nbody; b++) {
        b3WorldTransform t = b3Body_GetTransform(w->phys.doll[i].body[b]);
        float v[7] = {t.p.x, t.p.y, t.p.z, t.q.v.x, t.q.v.y, t.q.v.z, t.q.s};
        const unsigned char* p = (const unsigned char*)v;
        for (size_t k = 0; k < sizeof v; k++) { h ^= p[k]; h *= 1099511628211ull; }
    }
    return h;
}

// Reads the [env] keys of loco mode into cfg (absent keys keep their values).
// get(key, &value) returns false for an absent key; gets(key) returns the
// string value or NULL.
#define LOCO_KEYS(X) \
    X(num_agents, num_agents, int) X(policy_hz, policy_hz, int) X(sim_hz, sim_hz, int) X(substeps, substeps, int) \
    X(episode_seconds, episode_seconds, float) X(action_scale, action_scale, float) X(action_unit, action_unit, float) X(lane, lane, float) \
    X(w_pose, reward_pose, float) X(w_vel, reward_vel, float) X(w_ee, reward_ee, float) X(w_com, reward_com, float) \
    X(k_pose, k_pose, float) X(k_vel, k_vel, float) X(k_ee, k_ee, float) X(k_com, k_com, float) \
    X(gains.leg, hertz_leg, float) X(gains.body, hertz_body, float) X(gains.tail, hertz_tail, float) \
    X(gains.passive_tail, hertz_passive_tail, float) X(gains.damping, damping, float) X(gains.passive_damping, damping_passive_tail, float) \
    X(gains.friction, friction, float) X(gains.passive, passive_tail, bool) X(fall_height, fall_height, float) X(weld_range, weld_range, float) \
    X(push_interval, push_interval, float) X(push_min, push_min, float) X(push_max, push_max, float) X(push_seconds, push_seconds, float)
#endif
