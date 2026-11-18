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
//     (sin, cos), the target speed, and the way to the reference's heading
//     (sin, cos of the yaw from the animal's heading to it). Everything
//     else is in the animal's own heading frame, so without those two it
//     cannot tell that a shove has turned it: runs 4 to 6p took the hit and
//     walked on 30 to 60 degrees off. They are also the steering command.
//   Action (LOCO_MAX_DOF floats, the unused tail of it ignored): per joint
//     an offset added to the clip's pose at this instant (residual control):
//     three for a ball joint (a rotation vector in the joint frame), one for
//     a hinge. Each output is action_unit radians per unit, clamped to
//     +-action_scale radians. The trainer starts every action's noise at
//     std 1, so action_unit sets the initial exploration (0.1 rad, near
//     DeepMimic's fixed 0.05) while action_scale caps the offset.
//   Reward: DeepMimic's imitation reward, a weighted sum of
//     exp(-k * error) for the joint rotations, joint angular velocities, end
//     effectors (feet, head, tail tip) and the centre of mass's velocity,
//     plus (reward_heading > 0) one for the pelvis's heading alone, which still
//     points the way back when a shove has turned the animal so far that every
//     other term reads 0. Exactly the sum of the weights when the body
//     matches the clip.
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
//     push_adapt > 0 grows the shoves with the policy: each world's level (0..1)
//     sets the top of the range, push_min + level * (push_max - push_min); an
//     episode that ends standing raises it by push_adapt, a fall lowers it by
//     four times that, so it settles where about one episode in five falls.
//     A fresh policy meets gentle shoves until it walks.
//   Several clips (clip = "idle+walk+trot+run"): each episode starts on one
//     of them at random, and with clip_switch > 0 the agent switches to
//     a neighbour in the list (list them slowest first: an animal changes
//     gait through the gaits between, and a run cannot stop in clip_blend
//     seconds) every clip_switch seconds on average, at the matching point
//     of the stride (the clips start their cycles on different feet;
//     phase_off lines them up). The target speed in the observation is the command that
//     says which clip: it jumps at the switch, while the reference the
//     reward compares against blends from the old clip to the new one over
//     clip_blend seconds (a clip's pose jumps otherwise). One name in clip
//     is today's single-clip env, with the same random sequence.
#ifndef CRET_LOCO_ENV_H
#define CRET_LOCO_ENV_H
#include "loco_sim.h"

#define LOCO_BODY_OBS 15
#define LOCO_OBS_V1 (LOCO_MAX_BODIES * LOCO_BODY_OBS + 4)   // before the heading floats (the checkpoints of runs 2 to 7)
#define LOCO_OBS (LOCO_OBS_V1 + 2)
#define LOCO_MAX_AGENTS 256
#define LOCO_MAX_CLIPS 8

typedef struct LocoConfig {
    char dino[256], clip[64];   // clip: one name, or several joined by '+'
    int num_agents, policy_hz, sim_hz, substeps;
    float episode_seconds, action_scale, action_unit, lane;
    // Reward weights and scales (DeepMimic's: 0.65, 0.1, 0.15, 0.1; 2, 0.1, 40, 10; here k_ee is 10
    // for a 3 m animal, and the COM term compares velocities, see loco_com_vel).
    float w_pose, w_vel, w_ee, w_com, k_pose, k_vel, k_ee, k_com;
    float w_heading, k_heading;   // the heading term (rad^2 scale); 0 weight: off
    LocoGains gains;
    float fall_height;     // also fallen when the pelvis is below this share of the clip's pelvis height
    float weld_range;      // joints the clips move less than this (rad) are welded (loco_rig.h); 0 keeps every joint
    int tail_flex;         // tail joints kept from the base, the rest welded into a rod (loco_rig.h); 0 keeps them all
    float push_interval, push_min, push_max, push_seconds;   // shoves: mean seconds between them (0: none), speed change (m/s), duration (s)
    float push_adapt;      // shove curriculum step (0: the full range from the start)
    float clip_switch;     // several clips: mean seconds between switches to another clip (0: none)
    float clip_blend;      // seconds over which the reference blends from the old clip to the new one
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
    c.push_interval = 0; c.push_min = 0.5f; c.push_max = 1.5f; c.push_seconds = 0.1f; c.push_adapt = 0;
    c.w_heading = 0; c.k_heading = 1.0f;
    c.clip_switch = 0; c.clip_blend = 0.3f;
    return c;
}

// Per-agent episode state and the running sums the logs report.
typedef struct LocoAgent {
    double t;              // clip time (s) of the current clip: the start phase plus the time since the reset (or the switch)
    int clip, prev_clip;   // the clip followed (index into LocoWorld.ref) and, while blending, the one before it
    double prev_t;         // the previous clip's time, run on so the blend reads it where it would be
    float blend_left;      // seconds of the blend from prev_clip still to go (0: none)
    float x_off, prev_x_off;   // added to the clips' forward travel so the reference moves on without a jump at a switch
    float elapsed, ret, err_pose, err_ee; int steps; bool fallen;
    float r_pose, r_vel, r_ee, r_com, r_heading;   // last step's reward terms
    float start_x;         // pelvis x at the reset
    int push_left, push_body; b3Vec3 push_force;   // the shove under way: physics steps left, body, force (N)
} LocoAgent;

// clip_speed: the target speed summed over steps, like reward (divide by steps).
typedef struct LocoStats { double episodes, falls, length, ret, distance, reward, pose_deg, ee_cm, steps, push_level, clip_speed; } LocoStats;

typedef struct LocoWorld {
    LocoConfig cfg; LocoRig rig; LocoPhys phys;
    LocoRef ref[LOCO_MAX_CLIPS]; int nref;   // the clips listed in cfg.clip, in order
    int clip_index;        // the first listed clip's index in the .dino (tools/loco_bake.c)
    unsigned int rng;
    LocoAgent ag[LOCO_MAX_AGENTS];
    LocoStats stats;
    float pelvis_h[LOCO_MAX_CLIPS];   // each clip's mean pelvis height
    float phase_off[LOCO_MAX_CLIPS];  // each moving clip's cycle fraction at the first moving clip's fraction 0 (loco_phase_align)
    float push_level;      // shove curriculum level 0..1 (push_adapt)
    // Scratch for one agent's state and its reference (model space).
    b3Vec3 pos[LOCO_MAX_BODIES], vel[LOCO_MAX_BODIES], angvel[LOCO_MAX_BODIES];
    b3Quat rot[LOCO_MAX_BODIES];
    b3Vec3 rpos[LOCO_MAX_BODIES], rvel[LOCO_MAX_BODIES], rangvel[LOCO_MAX_BODIES];
    b3Quat rrot[LOCO_MAX_BODIES];
    // Scratch for the previous clip's reference while a switch blends (loco_ref_agent).
    b3Vec3 bpos[LOCO_MAX_BODIES], bvel[LOCO_MAX_BODIES], bangvel[LOCO_MAX_BODIES];
    b3Quat brot[LOCO_MAX_BODIES];
} LocoWorld;

static inline unsigned int loco_rand(unsigned int* s) { unsigned int x = *s ? *s : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static inline float loco_randf(unsigned int* s) { return (float)(loco_rand(s) >> 8) * (1.0f / 16777216.0f); }

// The clips' cycles start on different feet (the raptor's run starts 0.45 of a
// cycle from its walk), so a switch at the same cycle fraction would blend a
// leg through half a stride. For each moving clip this finds the cycle offset
// at which its joint rotations match the first moving clip's best over the
// whole cycle. Standing clips (idle) have no stride and keep 0.
static void loco_phase_align(LocoWorld* w) {
    int anchor = -1; const int nb = w->rig.nbody, NP = 16, NO = 64;
    for (int k = 0; k < w->nref && anchor < 0; k++) if (w->ref[k].speed > 0.1f) anchor = k;
    for (int k = anchor + 1; anchor >= 0 && k < w->nref; k++) {
        if (w->ref[k].speed <= 0.1f) continue;
        float best = 1e30f;
        for (int o = 0; o < NO; o++) {
            float d = 0;
            for (int p = 0; p < NP; p++) {
                double fa = (double)p / NP, fk = fa + (double)o / NO; fk -= floor(fk);
                loco_ref_sample(&w->ref[anchor], fa * w->ref[anchor].duration, NULL, w->rrot, NULL, NULL);
                loco_ref_sample(&w->ref[k], fk * w->ref[k].duration, NULL, w->brot, NULL, NULL);
                for (int b = 1; b < nb; b++) {
                    int pa = w->rig.body[b].parent;
                    float ang = lq_angle(lq_inv_mul(w->rrot[pa], w->rrot[b]), lq_inv_mul(w->brot[pa], w->brot[b]));
                    d += ang * ang;
                }
            }
            if (d < best) { best = d; w->phase_off[k] = (float)o / NO; }
        }
    }
}

// Loads the .dino, builds the rig, the references and the world. False with a
// message in err when the file, a clip or the rig cannot be used.
static bool loco_world_init(LocoWorld* w, const LocoConfig* cfg, unsigned int seed, char* err, size_t errn) {
    memset(w, 0, sizeof *w);
    w->cfg = *cfg; w->rng = seed ? seed : 1;
    if (cfg->num_agents < 1 || cfg->num_agents > LOCO_MAX_AGENTS) { snprintf(err, errn, "num_agents must be 1..%d", LOCO_MAX_AGENTS); return false; }
    if (cfg->sim_hz % cfg->policy_hz) { snprintf(err, errn, "sim_hz must be a multiple of policy_hz"); return false; }
    DinoFile* d = (DinoFile*)calloc(1, sizeof(DinoFile));
    if (!df_read(d, cfg->dino)) { snprintf(err, errn, "cannot read %.200s", cfg->dino); free(d); return false; }
    if (loco_unscale_bind(d) == 0) { snprintf(err, errn, "%.200s: its joints' bind scales differ", cfg->dino); df_free(d); free(d); return false; }
    // The clip list: names joined by '+', each a looping clip of the file, none twice.
    int listed[LOCO_MAX_CLIPS], nl = 0;
    for (const char* p = cfg->clip;;) {
        const char* e = strchr(p, '+'); size_t len = e ? (size_t)(e - p) : strlen(p);
        char name[32]; snprintf(name, sizeof name, "%.*s", (int)(len < sizeof name ? len : sizeof name - 1), p);
        int ci = len < sizeof name ? loco_clip_index(d, name) : -1;
        bool bad = ci < 0 || !d->clip[ci].loop, twice = false;
        for (int k = 0; k < nl && !bad; k++) twice |= listed[k] == ci;
        if (bad || twice || nl == LOCO_MAX_CLIPS) {
            if (nl == LOCO_MAX_CLIPS) snprintf(err, errn, "clip lists more than %d clips", LOCO_MAX_CLIPS);
            else snprintf(err, errn, twice ? "%.200s: clip %.31s is listed twice" : "%.200s has no looping clip %.31s", cfg->dino, name);
            df_free(d); free(d); return false;
        }
        listed[nl++] = ci;
        if (!e) break;
        p = e + 1;
    }
    w->clip_index = listed[0];
    // The joint limits cover the first clip and the gaits (loco_limit_clips) and every listed clip;
    // past LOCO_MAX_CLIPS the unlisted gaits at the end give way.
    int clips[16], nc = loco_limit_clips(d, w->clip_index, clips);
    for (int k = 1; k < nl; k++) { bool have = false; for (int j = 0; j < nc; j++) have |= clips[j] == listed[k]; if (!have) clips[nc++] = listed[k]; }
    for (int j = nc - 1; j > 0 && nc > LOCO_MAX_CLIPS; j--) {
        bool keep = false; for (int k = 0; k < nl; k++) keep |= clips[j] == listed[k];
        if (!keep) { memmove(clips + j, clips + j + 1, sizeof(int) * (size_t)(nc - j - 1)); nc--; }
    }
    bool ok = loco_rig_build(&w->rig, d, clips, nc, cfg->weld_range, cfg->tail_flex, err, errn);
    for (int k = 0; k < nl && ok; k++) {
        ok = loco_ref_build(&w->ref[k], d, &w->rig, listed[k]);
        if (ok) w->nref = k + 1; else snprintf(err, errn, "%.200s: clip %.31s is too short", cfg->dino, d->clip[listed[k]].name);
    }
    df_free(d); free(d);
    if (!ok) return false;
    if (w->rig.ndof > LOCO_MAX_DOF) { snprintf(err, errn, "rig has %d action dimensions (max %d)", w->rig.ndof, LOCO_MAX_DOF); return false; }
    for (int k = 0; k < w->nref; k++) {
        const LocoRef* R = &w->ref[k];
        for (int f = 0; f < R->period; f++) w->pelvis_h[k] += R->pos[f * R->nbody].y / R->period;
    }
    loco_phase_align(w);
    b3Vec3 origin[LOCO_MAX_AGENTS];
    for (int i = 0; i < cfg->num_agents; i++) origin[i] = lv(0, 0, cfg->lane * i);
    loco_phys_create(&w->phys, &w->rig, cfg->num_agents, origin, cfg->gains, (float)cfg->sim_hz * cfg->substeps);
    return true;
}
static void loco_world_close(LocoWorld* w) { loco_phys_destroy(&w->phys); for (int k = 0; k < w->nref; k++) loco_ref_free(&w->ref[k]); w->nref = 0; }

// Yaw-only heading of a rotation (the pelvis's forward, +X, projected on the ground).
static inline b3Quat loco_heading(b3Quat q) {
    b3Vec3 f = lq_rot(q, lv(1, 0, 0));
    return lq_axis_angle(lv(0, 1, 0), atan2f(-f.z, f.x));
}
// Agent a's reference state ahead seconds past its clip time (model space, the
// doll's origin removed); any output may be NULL. Its clip's, with x_off added
// to the travel; while a switch blends (blend_left), the previous clip's at
// the same instant eased into it (smoothstep), so the pose does not jump. Not
// blending, exactly loco_ref_sample of the clip.
static void loco_ref_agent(LocoWorld* w, const LocoAgent* a, double ahead, b3Vec3* pos, b3Quat* rot, b3Vec3* vel, b3Vec3* angvel) {
    const int nb = w->rig.nbody;
    loco_ref_sample(&w->ref[a->clip], a->t + ahead, pos, rot, vel, angvel);
    if (pos && a->x_off != 0) for (int b = 0; b < nb; b++) pos[b].x += a->x_off;
    float T = w->cfg.clip_blend, left = a->blend_left - (float)ahead;
    if (T <= 0 || left <= 0) return;
    loco_ref_sample(&w->ref[a->prev_clip], a->prev_t + ahead, pos ? w->bpos : NULL, rot ? w->brot : NULL, vel ? w->bvel : NULL, angvel ? w->bangvel : NULL);
    float s = fminf(fmaxf(1.0f - left / T, 0.0f), 1.0f); s = s * s * (3.0f - 2.0f * s);
    for (int b = 0; b < nb; b++) {
        if (pos) { b3Vec3 o = w->bpos[b]; o.x += a->prev_x_off; pos[b] = lv_lerp(o, pos[b], s); }
        if (rot) { b3Quat q0 = w->brot[b], q1 = rot[b]; if (q0.v.x * q1.v.x + q0.v.y * q1.v.y + q0.v.z * q1.v.z + q0.s * q1.s < 0) q1 = b3NegateQuat(q1); rot[b] = lq_nlerp(q0, q1, s); }
        if (vel) vel[b] = lv_lerp(w->bvel[b], vel[b], s);
        if (angvel) angvel[b] = lv_lerp(w->bangvel[b], angvel[b], s);
    }
}
// Agent a's reference state now, into w->rpos...
static void loco_ref_at(LocoWorld* w, const LocoAgent* a) { loco_ref_agent(w, a, 0, w->rpos, w->rrot, w->rvel, w->rangvel); }
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

// Reference state initialisation: agent i starts on clip (clip < 0: one at
// random when there are several) at a random phase (or at phase, when phase
// >= 0) in the clip's pose and velocities, placed so its pelvis starts near its
// lane's origin. One clip draws no random number for it, so a single-clip run
// keeps its random sequence.
static void loco_reset_agent_clip(LocoWorld* w, int i, int clip, float phase) {
    LocoAgent* a = &w->ag[i];
    if (clip < 0 || clip >= w->nref) {
        clip = 0;
        if (w->nref > 1) { clip = (int)(loco_randf(&w->rng) * w->nref); if (clip >= w->nref) clip = w->nref - 1; }
    }
    const LocoRef* R = &w->ref[clip];
    float ph = phase >= 0 ? phase : loco_randf(&w->rng);
    double t = (double)ph * R->duration;
    memset(a, 0, sizeof *a);
    a->clip = a->prev_clip = clip; a->t = a->prev_t = t;
    loco_ref_at(w, a);
    // The doll's origin moves back by the travel so far, so every episode starts at x near 0. The
    // clips let feet sink into the ground by a centimetre or two; starting a light foot inside the
    // ground would have the contact solver flip it, so the doll is lifted to stand on the ground.
    float lift = fmaxf(0.0f, -loco_lowest(&w->rig, w->rpos, w->rrot));
    w->phys.doll[i].origin = lv(-R->speed * (float)t, lift, w->cfg.lane * i);
    loco_doll_set(&w->phys, i, w->rpos, w->rrot, w->rvel, w->rangvel);
    b3Quat dev[LOCO_MAX_BODIES]; loco_targets(w, w->rrot, NULL, dev);
    loco_doll_targets(&w->phys, i, dev);
    a->start_x = w->rpos[0].x;
}
static void loco_reset_agent(LocoWorld* w, int i, float phase) { loco_reset_agent_clip(w, i, -1, phase); }

// Switches agent i to clip at the matching point of the stride (phase_off). The reference's travel
// carries on from where it is (x_off) and its pose blends over clip_blend
// seconds from the old clip, which keeps running beside it (prev_*).
static void loco_switch_clip(LocoWorld* w, int i, int clip) {
    LocoAgent* a = &w->ag[i];
    if (clip < 0 || clip >= w->nref || clip == a->clip) return;
    const LocoRef *o = &w->ref[a->clip], *n = &w->ref[clip];
    double f = fmod(a->t, (double)o->duration) / o->duration - w->phase_off[a->clip] + w->phase_off[clip]; f -= floor(f);
    double tn = f * n->duration;
    a->prev_clip = a->clip; a->prev_t = a->t; a->prev_x_off = a->x_off;
    a->x_off = a->x_off + (float)(o->speed * a->t) - (float)(n->speed * tn);
    a->clip = clip; a->t = tn; a->blend_left = w->cfg.clip_blend > 0 ? w->cfg.clip_blend : 0;
}
// Advances agent a's clip clocks and its blend by dt seconds.
static inline void loco_advance(LocoAgent* a, double dt) { a->t += dt; a->prev_t += dt; a->blend_left = fmaxf(a->blend_left - (float)dt, 0.0f); }
// A random switch to a neighbour in the clip list when one is due (none while a blend runs).
static void loco_switch_maybe(LocoWorld* w, int i) {
    const LocoConfig* c = &w->cfg; LocoAgent* a = &w->ag[i];
    if (w->nref < 2 || c->clip_switch <= 0 || a->blend_left > 0 || loco_randf(&w->rng) * c->clip_switch * c->policy_hz >= 1.0f) return;
    int k = a->clip + ((loco_rand(&w->rng) >> 16) & 1 ? 1 : -1);
    loco_switch_clip(w, i, k < 0 ? 1 : k >= w->nref ? w->nref - 2 : k);
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
    float top = c->push_adapt > 0 ? w->push_level : 1.0f;
    float ang = 2.0f * (float)M_PI * loco_randf(&w->rng), sp = c->push_min + (c->push_max - c->push_min) * top * loco_randf(&w->rng);
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
    float eh = lq_angle(hs, hr); eh *= eh;
    a->r_pose = expf(-c->k_pose * ep); a->r_vel = expf(-c->k_vel * ev); a->r_ee = expf(-c->k_ee * ee); a->r_com = expf(-c->k_com * ec); a->r_heading = expf(-c->k_heading * eh);
    a->err_pose += sqrtf(ep / r->nbody); a->err_ee += sqrtf(ee / (r->nee > 0 ? r->nee : 1));
    return c->w_pose * a->r_pose + c->w_vel * a->r_vel + c->w_ee * a->r_ee + c->w_com * a->r_com + c->w_heading * a->r_heading;
}

// Agent i's observation (LOCO_OBS floats) from its current state in w->pos...
static void loco_observe_state(LocoWorld* w, const LocoAgent* a, float* o) {
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
    // The current clip's phase (counted from the shared stride start, phase_off) and speed: the speed
    // is the command (which clip), and jumps at a switch.
    float* t = o + LOCO_MAX_BODIES * LOCO_BODY_OBS; const LocoRef* R = &w->ref[a->clip];
    double ph = (fmod(a->t, (double)R->duration) / R->duration - w->phase_off[a->clip]) * 2.0 * M_PI;
    t[0] = pel.y; t[1] = (float)sin(ph); t[2] = (float)cos(ph); t[3] = 0.2f * R->speed;
    // The reference's heading seen from the animal's: its forward in the animal's heading frame is (cos, 0, -sin) of the yaw to it.
    b3Quat rr[LOCO_MAX_BODIES]; loco_ref_agent(w, a, 0, NULL, rr, NULL, NULL);
    b3Vec3 f = lq_rot(lq_inv_mul(hd, loco_heading(rr[0])), lv(1, 0, 0));
    t[4] = -f.z; t[5] = f.x;
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
    for (int i = 0; i < n; i++) { w->ag[i].fallen = false; loco_push_maybe(w, i); loco_switch_maybe(w, i); }
    for (int s = 0; s < k; s++) {
        for (int i = 0; i < n; i++) {
            LocoAgent* a = &w->ag[i];
            loco_ref_agent(w, a, (s + 1) * h, NULL, w->rrot, NULL, NULL);
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
        loco_advance(a, 1.0 / c->policy_hz); a->elapsed += 1.0f / c->policy_hz; a->steps++;
        loco_doll_get(&w->phys, i, w->pos, w->rot, w->vel, w->angvel);
        loco_ref_at(w, a);
        if (w->pos[0].y < c->fall_height * w->pelvis_h[a->clip]) a->fallen = true;
        float r = loco_reward(w, a);
        a->ret += r;
        bool done = a->fallen || a->elapsed >= c->episode_seconds - 1e-4f;
        if (reward) reward[i] = r;
        if (terminal) terminal[i] = done ? 1.0f : 0.0f;
        w->stats.reward += r; w->stats.steps += 1; w->stats.clip_speed += w->ref[a->clip].speed;
        if (done) {
            LocoStats* st = &w->stats;
            st->episodes += 1; st->falls += a->fallen; st->length += a->elapsed; st->ret += a->ret;
            st->distance += w->pos[0].x - a->start_x;
            st->pose_deg += a->err_pose / a->steps * (180.0 / M_PI); st->ee_cm += a->err_ee / a->steps * 100.0;
            if (c->push_adapt > 0) w->push_level = fminf(fmaxf(w->push_level + (a->fallen ? -4.0f : 1.0f) * c->push_adapt, 0.0f), 1.0f);
            st->push_level += w->push_level;
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
    X(w_pose, reward_pose, float) X(w_vel, reward_vel, float) X(w_ee, reward_ee, float) X(w_com, reward_com, float) X(w_heading, reward_heading, float) X(k_heading, k_heading, float) \
    X(k_pose, k_pose, float) X(k_vel, k_vel, float) X(k_ee, k_ee, float) X(k_com, k_com, float) \
    X(gains.leg, hertz_leg, float) X(gains.body, hertz_body, float) X(gains.tail, hertz_tail, float) \
    X(gains.passive_tail, hertz_passive_tail, float) X(gains.damping, damping, float) X(gains.passive_damping, damping_passive_tail, float) X(gains.tail_stiffen, tail_stiffen, float) \
    X(gains.friction, friction, float) X(gains.passive, passive_tail, bool) X(fall_height, fall_height, float) X(weld_range, weld_range, float) X(tail_flex, tail_flex, int) \
    X(push_interval, push_interval, float) X(push_min, push_min, float) X(push_max, push_max, float) X(push_seconds, push_seconds, float) X(push_adapt, push_adapt, float) \
    X(clip_switch, clip_switch, float) X(clip_blend, clip_blend, float)
#endif
