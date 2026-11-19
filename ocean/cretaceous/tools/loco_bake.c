// Bakes the physics raptor's motion into ordinary animation clips: a trained
// locomotion policy (loco/, docs/locomotion/README.md) walks its ragdoll, gets
// shoved, recovers, and every frame of that is written back as the rig's
// joint rotations, appended to a copy of the .dino. The game then plays them
// like any other clip, at no runtime cost (no physics, no policy).
// Run from the repository root.
//   loco_bake --policy CKPT.bin --out OUT.dino [--dino IN.dino] [--clip walk | --clip walk+idle+trot+run]
//             (an all-gaits policy: list every clip it trained on, the one to bake first)
//             [--push 2.0] [--cycles 2] [--tail-flex N] [--passive-tail [--passive-damping 0.4]] [--selftest]
// --tail-flex, --passive-tail and --passive-damping must match the policy's training (config/cretaceous_loco.ini).
// Clips written (their names replace same-named clips already in IN):
//   <clip>_phys   one cycle of the policy's own version of the clip, looping
//   hit_<side>_<k>  side front, back, left or right (where the blow comes
//                 from), k 0..3: the clip starts at walk phase k/4 and a shove
//                 from that side (on the chest, --push m/s for 0.1 s) lands
//                 0.1 s in; then the stumble and the recovery, --cycles cycles
//                 long, so it ends at the walk phase it began at. The game
//                 (render/dino.c, animate) plays the one whose phase is
//                 nearest the walk's when a blow lands. Its speed is the walk's.
// --selftest bakes the reference clip itself (the ragdoll exactly on the clip)
// and checks the baked frames give back the clip's body poses.
//
// How a frame is baked. A body's pose is its joint's skinning matrix (loco_rig.h),
// so body b's rotation q_b fixes its joint's model-space rotation:
// rot(G[j]) = q_b * rot(inv_bind[j])^-1. Down the hierarchy, each body's
// joint gets the local rotation that puts it there under its (already baked)
// parent; the pelvis also gets its translation. Joints inside a body (the
// second bone of a merged pair, fingers, toes, jaw) keep the clip's local
// pose at that instant and so ride along rigidly, as the ragdoll's body does.
// Joints above the pelvis keep the clip's values.
//
// Root motion. The game's clips are in place, and the game moves the animal:
// at the clip's speed, plus the sim's own knockback when it is hit (sim.c,
// damage). So a hit clip keeps the pelvis on the walk's track (the stumble's
// steps, lean and tail stay; the displacement is the sim's), and the heading
// the shove leaves is eased back over the clip's last 40% so it ends facing
// where the animal faces. The looping walk keeps its pelvis path, with the
// cycle's drift taken into the clip's speed.
#include "loco/loco_env.h"
#include "loco/loco_policy.h"

#define BAKE_MAX_SAMPLES 4096

// One recorded ragdoll state per policy step: clip time and body poses (model space, travel removed).
typedef struct Sample { double t; b3Vec3 pos[LOCO_MAX_BODIES]; b3Quat rot[LOCO_MAX_BODIES]; bool fallen; } Sample;
typedef struct Track { int n; Sample* s; } Track;

static inline b3Quat hemi(b3Quat q, b3Quat ref) { return q.v.x * ref.v.x + q.v.y * ref.v.y + q.v.z * ref.v.z + q.s * ref.s < 0 ? b3NegateQuat(q) : q; }

// Body poses at time t, interpolated between the recorded samples.
static void track_at(const Track* tr, double t, b3Vec3* pos, b3Quat* rot, int nbody) {
    int k = 0;
    while (k + 2 < tr->n && tr->s[k + 1].t <= t) k++;
    const Sample *a = &tr->s[k], *b = &tr->s[k + 1 < tr->n ? k + 1 : k];
    float u = b->t > a->t ? (float)((t - a->t) / (b->t - a->t)) : 0; u = fminf(fmaxf(u, 0), 1);
    for (int i = 0; i < nbody; i++) { pos[i] = lv_lerp(a->pos[i], b->pos[i], u); rot[i] = lq_nlerp(a->rot[i], hemi(b->rot[i], a->rot[i]), u); }
}

// The clip's local pose (njoint x 8) at time t, wrapping a looping clip.
static void clip_local(const DinoFile* d, const DfClip* C, double t, float* out) {
    double dt = C->duration / (C->nframes - 1), ft = fmod(t, (double)C->duration) / dt;
    if (ft < 0) ft += C->nframes - 1;
    int f0 = (int)ft; if (f0 >= C->nframes - 1) f0 = C->nframes - 2;
    float u = (float)(ft - f0);
    const float *a = C->frames + (size_t)f0 * d->njoint * 8, *b = a + d->njoint * 8;
    for (int j = 0; j < d->njoint; j++) {
        const float *x = a + j * 8, *y = b + j * 8; float* o = out + j * 8;
        for (int k = 0; k < 3; k++) o[k] = x[k] + u * (y[k] - x[k]);
        b3Quat qa = lq_make(x[3], x[4], x[5], x[6]), qb = hemi(lq_make(y[3], y[4], y[5], y[6]), qa), q = lq_nlerp(qa, qb, u);
        o[3] = q.v.x; o[4] = q.v.y; o[5] = q.v.z; o[6] = q.s; o[7] = x[7] + u * (y[7] - x[7]);
    }
}

// One baked frame: the clip's local pose at its time, with the bodies' joints
// set from the ragdoll's body poses (see the header). local is in and out.
static void bake_frame(const DinoFile* d, const LocoRig* rig, const b3Quat* rib, const b3Vec3* pos, const b3Quat* rot, float* local, float (*G)[12]) {
    for (int j = 0; j < d->njoint; j++) {
        float* l = local + j * 8; int P = d->parent[j], b = rig->joint_body[j];
        if (b >= 0 && rig->body[b].joint == j) {
            b3Quat rg = lq_mul(rot[b], lq_conj(rib[b])), rp = P >= 0 ? lq_from_m34(G[P]) : b3Quat_identity;
            b3Quat q = hemi(lq_inv_mul(rp, rg), lq_make(l[3], l[4], l[5], l[6]));
            l[3] = q.v.x; l[4] = q.v.y; l[5] = q.v.z; l[6] = q.s;
            if (b == 0) {
                b3Vec3 t = pos[0];
                if (P >= 0) { float inv[12]; lm34_inverse(G[P], inv); t = lm34_point(inv, t); }
                l[0] = t.x; l[1] = t.y; l[2] = t.z;
            }
        }
        float L[12]; df_trs(l, l + 3, l[7], L);
        if (P < 0) memcpy(G[j], L, sizeof L); else df_mul(G[P], L, G[j]);
    }
}

typedef struct OutClip { char name[32]; float duration, speed; int loop, nframes; float* frames; } OutClip;

// The pelvis's horizontal offset from the clip's pelvis at clip time t (travel
// removed from both), and its heading's difference (radians).
static b3Vec3 pelvis_drift(const LocoRef* ref, double t, const b3Vec3* pos, const b3Quat* rot, float* yaw) {
    b3Vec3 rp[LOCO_MAX_BODIES]; b3Quat rr[LOCO_MAX_BODIES];
    loco_ref_sample(ref, t, rp, rr, NULL, NULL);
    if (yaw) {
        b3Vec3 fs = lq_rot(rot[0], lv(1, 0, 0)), fr = lq_rot(rr[0], lv(1, 0, 0));
        float y = atan2f(-fs.z, fs.x) - atan2f(-fr.z, fr.x);
        *yaw = y > (float)M_PI ? y - 2 * (float)M_PI : y < -(float)M_PI ? y + 2 * (float)M_PI : y;
    }
    return lv(pos[0].x - (rp[0].x - ref->speed * (float)t), 0, pos[0].z - rp[0].z);
}

// Bakes the track's poses at the clip's frame times from t0 for ncycles
// cycles. hit: the pelvis kept on the walk's track (the game's knockback moves
// the animal), and the heading the shove leaves eased back over the last 40%.
// loop: the drift over the cycle goes into the clip's speed and the end's
// mismatch is spread over the cycle, so the clip loops.
static OutClip bake_clip(const DinoFile* d, const LocoWorld* w, const b3Quat* rib, const Track* tr, double t0, int ncycles, const char* name, bool hit, bool loop) {
    const DfClip* C = &d->clip[w->clip_index]; const LocoRig* rig = &w->rig;
    int per = C->nframes - 1, nf = per * ncycles + 1; double dt = C->duration / per;
    OutClip o; memset(&o, 0, sizeof o);
    snprintf(o.name, sizeof o.name, "%s", name); o.duration = (float)(dt * (nf - 1)); o.speed = C->speed; o.loop = loop; o.nframes = nf;
    o.frames = (float*)malloc(sizeof(float) * 8 * d->njoint * nf);
    float (*G)[12] = (float (*)[12])malloc(sizeof(float) * 12 * d->njoint);
    const LocoRef* ref = &w->ref[0]; b3Vec3 d0 = lv(0, 0, 0), dn = lv(0, 0, 0);
    if (loop) {
        b3Vec3 pos[LOCO_MAX_BODIES]; b3Quat rot[LOCO_MAX_BODIES];
        track_at(tr, t0, pos, rot, rig->nbody); d0 = pelvis_drift(ref, t0, pos, rot, NULL);
        track_at(tr, t0 + (nf - 1) * dt, pos, rot, rig->nbody); dn = pelvis_drift(ref, t0 + (nf - 1) * dt, pos, rot, NULL);
        o.speed += (dn.x - d0.x) / o.duration;
    }
    for (int f = 0; f < nf; f++) {
        double t = t0 + f * dt; b3Vec3 pos[LOCO_MAX_BODIES]; b3Quat rot[LOCO_MAX_BODIES];
        track_at(tr, t, pos, rot, rig->nbody);
        float u = (float)f / (nf - 1), yaw; b3Vec3 off = pelvis_drift(ref, t, pos, rot, &yaw);
        if (loop) off = lv_lerp(d0, dn, u);
        b3Quat turn = b3Quat_identity;
        if (hit) { float k = u < 0.6f ? 0 : (u - 0.6f) / 0.4f; k = k * k * (3 - 2 * k); turn = lq_axis_angle(lv(0, 1, 0), -k * yaw); }
        b3Vec3 piv = b3Sub(pos[0], off);
        for (int b = 0; b < rig->nbody; b++) { pos[b] = b3Add(piv, lq_rot(turn, b3Sub(b3Sub(pos[b], off), piv))); rot[b] = lq_mul(turn, rot[b]); }
        float* l = o.frames + (size_t)f * d->njoint * 8;
        clip_local(d, C, t, l);
        bake_frame(d, rig, rib, pos, rot, l, G);
    }
    if (loop) {   // spread the seam over the cycle: frame f gets the share f/(nf-1) of last -> first
        float* first = o.frames; float* last = o.frames + (size_t)(nf - 1) * d->njoint * 8;
        for (int j = 0; j < d->njoint; j++) {
            b3Quat q0 = lq_make(first[j * 8 + 3], first[j * 8 + 4], first[j * 8 + 5], first[j * 8 + 6]);
            b3Quat qn = hemi(lq_make(last[j * 8 + 3], last[j * 8 + 4], last[j * 8 + 5], last[j * 8 + 6]), q0);
            b3Vec3 fix = lq_log(lq_mul(q0, lq_conj(qn))); float dp[3];
            for (int k = 0; k < 3; k++) dp[k] = first[j * 8 + k] - last[j * 8 + k];
            for (int f = 1; f < nf; f++) {
                float u = (float)f / (nf - 1), *l = o.frames + ((size_t)f * d->njoint + j) * 8;
                b3Quat q = lq_mul(lq_exp(b3MulSV(u, fix)), lq_make(l[3], l[4], l[5], l[6]));
                l[3] = q.v.x; l[4] = q.v.y; l[5] = q.v.z; l[6] = q.s;
                for (int k = 0; k < 3; k++) l[k] += u * dp[k];
            }
        }
    }
    free(G);
    return o;
}

// Runs one ragdoll from phase 0 with the policy (NULL: the reference itself,
// placed exactly, recorded at the clip's frame times), recording every policy step; a shove dv on body pb at
// push_t (seconds of clip time; < 0: none). Returns the track; *fell when the
// ragdoll went down.
static Track run_track(LocoWorld* w, LocoPolicy* pol, double seconds, double push_t, int pb, b3Vec3 dv, bool* fell) {
    double ref_step = w->ref[0].dt;   // without a policy, the reference at its own frame times
    Track tr; tr.n = 0; tr.s = (Sample*)calloc(BAKE_MAX_SAMPLES, sizeof(Sample));
    w->cfg.episode_seconds = 1e9f;
    loco_reset_agent_clip(w, 0, 0, 0.0f);
    if (pol) loco_policy_reset(pol, 0);
    static float obs[LOCO_OBS], act[LOCO_MAX_DOF]; const float* ap[1] = {act};
    *fell = false; bool pushed = false; int steps = (int)(seconds * (pol ? w->cfg.policy_hz : 1.0 / ref_step)) + 2;
    for (int s = 0; s <= steps && tr.n < BAKE_MAX_SAMPLES; s++) {
        Sample* S = &tr.s[tr.n++]; LocoAgent* a = &w->ag[0];
        S->t = a->t;
        if (pol) {
            loco_doll_get(&w->phys, 0, S->pos, S->rot, NULL, NULL);
            for (int b = 0; b < w->rig.nbody; b++) S->pos[b].x -= w->ref[0].speed * (float)a->t;
        } else {
            loco_ref_sample(&w->ref[0], a->t, S->pos, S->rot, NULL, NULL);
            for (int b = 0; b < w->rig.nbody; b++) S->pos[b].x -= w->ref[0].speed * (float)a->t;
        }
        if (s == steps) break;
        if (!pushed && push_t >= 0 && a->t + 1e-6 >= push_t) { loco_push(w, 0, pb, dv, 0.1f); pushed = true; }
        if (pol) { loco_observe(w, 0, obs); loco_policy_forward(pol, obs, 1, act); }
        float term = 0;
        if (pol) loco_step(w, ap, NULL, &term);
        else a->t += ref_step;
        if (term) { *fell = true; tr.s[tr.n - 1].fallen = true; break; }
    }
    return tr;
}

// Writes in's bytes with its clips replaced: every clip of d not named in add, then add.
static bool write_dino(const char* in, const char* out, const DinoFile* d, const OutClip* add, int nadd) {
    FILE* f = fopen(in, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc((size_t)size);
    bool ok = fread(buf, 1, (size_t)size, f) == (size_t)size; fclose(f);
    long clips = 0;
    for (int c = 0; c < d->nclip; c++) clips += 48 + 32L * d->njoint * d->clip[c].nframes;
    long at = size - clips - 4; uint32_t nc = 0;
    if (ok && at > 0) memcpy(&nc, buf + at, 4);
    if (!ok || at <= 0 || nc != (uint32_t)d->nclip) { fprintf(stderr, "loco_bake: %s: clip section not where expected\n", in); free(buf); return false; }
    int keep = 0;
    for (int c = 0; c < d->nclip; c++) { bool dup = false; for (int k = 0; k < nadd; k++) dup |= !strcmp(d->clip[c].name, add[k].name); keep += !dup; }
    if (keep + nadd > 48) { fprintf(stderr, "loco_bake: %d clips is over the game's 48\n", keep + nadd); free(buf); return false; }
    // Write next to out, then rename: out may be a hard link shared with another checkout, which must not change.
    char tmp[600]; snprintf(tmp, sizeof tmp, "%s.tmp", out);
    FILE* o = fopen(tmp, "wb"); if (!o) { free(buf); return false; }
    fwrite(buf, 1, (size_t)at, o);
    uint32_t n = (uint32_t)(keep + nadd); fwrite(&n, 4, 1, o);
    for (int c = 0; c < d->nclip; c++) {
        bool dup = false; for (int k = 0; k < nadd; k++) dup |= !strcmp(d->clip[c].name, add[k].name);
        if (dup) continue;
        const DfClip* C = &d->clip[c]; char nm[32] = {0}; snprintf(nm, sizeof nm, "%s", C->name);
        uint32_t fl = (uint32_t)C->loop, nf = (uint32_t)C->nframes;
        fwrite(nm, 32, 1, o); fwrite(&C->duration, 4, 1, o); fwrite(&C->speed, 4, 1, o); fwrite(&fl, 4, 1, o); fwrite(&nf, 4, 1, o);
        fwrite(C->frames, sizeof(float), (size_t)8 * d->njoint * C->nframes, o);
    }
    for (int k = 0; k < nadd; k++) {
        const OutClip* C = &add[k]; char nm[32] = {0}; snprintf(nm, sizeof nm, "%s", C->name);
        uint32_t fl = (uint32_t)C->loop, nf = (uint32_t)C->nframes;
        fwrite(nm, 32, 1, o); fwrite(&C->duration, 4, 1, o); fwrite(&C->speed, 4, 1, o); fwrite(&fl, 4, 1, o); fwrite(&nf, 4, 1, o);
        fwrite(C->frames, sizeof(float), (size_t)8 * d->njoint * C->nframes, o);
    }
    ok = fclose(o) == 0;
    free(buf);
    if (ok && rename(tmp, out) != 0) ok = false;
    return ok;
}

// Largest error (deg, cm) between the body poses a baked clip gives and the
// track's, relative to the pelvis (the shape of the pose: the bake moves and
// turns the whole animal on purpose).
static void check_clip(const DinoFile* d, const LocoRig* rig, const OutClip* o, const Track* tr, double t0, double dt, bool travel, float* deg, float* cm) {
    float (*G)[12] = (float (*)[12])malloc(sizeof(float) * 12 * d->njoint);
    *deg = 0; *cm = 0;
    for (int f = 0; f < o->nframes; f++) {
        df_globals(d, o->frames + (size_t)f * d->njoint * 8, G);
        b3Vec3 pos[LOCO_MAX_BODIES]; b3Quat rot[LOCO_MAX_BODIES];
        track_at(tr, t0 + f * dt, pos, rot, rig->nbody);
        b3Quat q0; b3Vec3 p0; loco_body_pose(d, &rig->body[0], (const float (*)[12])G, &q0, &p0);
        for (int b = 0; b < rig->nbody; b++) {
            b3Quat q; b3Vec3 p; loco_body_pose(d, &rig->body[b], (const float (*)[12])G, &q, &p);
            *deg = fmaxf(*deg, lq_angle(lq_inv_mul(q0, q), lq_inv_mul(rot[0], rot[b])) * 57.2958f);
            *cm = fmaxf(*cm, sqrtf(lv_len2(b3Sub(lq_inv_rot(q0, b3Sub(p, p0)), lq_inv_rot(rot[0], b3Sub(pos[b], pos[0])))) * 100));
        }
    }
    (void)travel;
    free(G);
}

int main(int argc, char** argv) {
    LocoConfig c = loco_default_config(); c.num_agents = 1;
    const char* policy = NULL; const char* out = NULL; float push = 2.0f; int cycles = 2; bool selftest = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i]; bool more = i + 1 < argc;
        if (!strcmp(a, "--dino") && more) snprintf(c.dino, sizeof c.dino, "%s", argv[++i]);
        else if (!strcmp(a, "--clip") && more) snprintf(c.clip, sizeof c.clip, "%s", argv[++i]);
        else if (!strcmp(a, "--policy") && more) policy = argv[++i];
        else if (!strcmp(a, "--out") && more) out = argv[++i];
        else if (!strcmp(a, "--push") && more) push = (float)atof(argv[++i]);
        else if (!strcmp(a, "--cycles") && more) cycles = atoi(argv[++i]);
        else if (!strcmp(a, "--selftest")) selftest = true;
        else if (!strcmp(a, "--tail-flex") && more) c.tail_flex = atoi(argv[++i]);
        else if (!strcmp(a, "--passive-tail")) c.gains.passive = true;
        else if (!strcmp(a, "--passive-damping") && more) c.gains.passive_damping = (float)atof(argv[++i]);
        else if (!strcmp(a, "--cadence") && more) c.cadence = (float)atof(argv[++i]);
        else { fprintf(stderr, "loco_bake: unknown argument %s (see the header of tools/loco_bake.c)\n", a); return 2; }
    }
    if (!selftest && (!policy || !out)) { fprintf(stderr, "loco_bake: needs --policy and --out (or --selftest)\n"); return 2; }
    LocoWorld* w = (LocoWorld*)calloc(1, sizeof(LocoWorld)); char err[256];
    if (!loco_world_init(w, &c, 1, err, sizeof err)) { fprintf(stderr, "loco_bake: %s\n", err); return 1; }
    // A policy trained on several clips needs its whole list to build the same rig: the first one listed is baked.
    if (w->nref > 1) printf("loco_bake: --clip %s lists %d clips; baking the first\n", c.clip, w->nref);
    // The file with its bind space in metres as the rig has it (loco_unscale_bind changes only the
    // inverse binds and vertices, so the clips' locals, which get rewritten, are the file's own).
    DinoFile* d = (DinoFile*)calloc(1, sizeof(DinoFile));
    if (!df_read(d, c.dino) || loco_unscale_bind(d) == 0) { fprintf(stderr, "loco_bake: cannot read %s\n", c.dino); return 1; }
    const LocoRig* rig = &w->rig; b3Quat rib[LOCO_MAX_BODIES];
    for (int b = 0; b < rig->nbody; b++) rib[b] = lq_from_m34(d->inv_bind[rig->body[b].joint]);
    const DfClip* C = &d->clip[w->clip_index]; double dt = C->duration / (C->nframes - 1);
    printf("loco_bake: %s, clip %s (%d frames, %.2f s, %.2f m/s), %d bodies\n", c.dino, C->name, C->nframes, C->duration, C->speed, rig->nbody);

    if (selftest) {
        bool fell; Track tr = run_track(w, NULL, 3 * C->duration + 0.2, -1, 0, lv(0, 0, 0), &fell);
        OutClip o = bake_clip(d, w, rib, &tr, C->duration, 1, "selftest", false, false);
        float deg, cm; check_clip(d, rig, &o, &tr, C->duration, dt, false, &deg, &cm);
        // And the baked locals against the clip's own (the clip's last frame repeats its first).
        float ldeg = 0;
        for (int f = 0; f < o.nframes; f++) for (int j = 0; j < d->njoint; j++) {
            const float *x = o.frames + ((size_t)f * d->njoint + j) * 8, *y = C->frames + ((size_t)f * d->njoint + j) * 8;
            ldeg = fmaxf(ldeg, lq_angle(lq_make(x[3], x[4], x[5], x[6]), lq_make(y[3], y[4], y[5], y[6])) * 57.2958f);
        }
        bool ok = deg < 0.05f && cm < 0.05f && ldeg < 0.1f;
        printf("selftest: baking the clip itself: body poses within %.4f deg, %.4f cm; joint locals within %.4f deg of the clip  %s\n", deg, cm, ldeg, ok ? "ok" : "FAIL");
        return ok ? 0 : 1;
    }

    LocoPolicy* pol = loco_policy_load(policy, 1, err, sizeof err);
    if (!pol) { fprintf(stderr, "loco_bake: %s\n", err); return 1; }
    OutClip add[24]; int nadd = 0;
    // The policy's own walk: one cycle after three to settle.
    {
        bool fell; Track tr = run_track(w, pol, 4 * C->duration + 0.2, -1, 0, lv(0, 0, 0), &fell);
        if (fell) { fprintf(stderr, "loco_bake: the policy fell walking\n"); return 1; }
        char nm[32]; snprintf(nm, sizeof nm, "%.26s_phys", C->name);
        add[nadd] = bake_clip(d, w, rib, &tr, 3 * C->duration, 1, nm, false, true);
        float deg, cm; check_clip(d, rig, &add[nadd], &tr, 3 * C->duration, dt, false, &deg, &cm);
        printf("  %-12s %5.2f s loop at %.2f m/s, pose within %.2f deg, %.2f cm of the ragdoll's (the seam spread over the cycle)\n", nm, add[nadd].duration, add[nadd].speed, deg, cm);
        nadd++; free(tr.s);
    }
    // Shoves on the chest from each side. The model faces +X with up +Y, so its right is +Z.
    int chest = 0;
    for (int b = 0; b < rig->nbody; b++) if (!strcmp(rig->body[b].name, "chest")) chest = b;
    // A blow from the front pushes the animal back (-X), from the left toward its right (+Z).
    struct { const char* name; b3Vec3 dir; } hits[4] = {{"front", {-1, 0, 0}}, {"back", {1, 0, 0}}, {"left", {0, 0, 1}}, {"right", {0, 0, -1}}};
    int failed = 0;
    for (int h = 0; h < 16; h++) {
        int side = h / 4, k = h % 4; char nm[32]; snprintf(nm, sizeof nm, "hit_%s_%d", hits[side].name, k);
        double t0 = (3 + 0.25 * k) * C->duration;   // three cycles to settle, then from phase k/4; the shove 0.1 s in
        bool fell; Track tr = run_track(w, pol, t0 + cycles * C->duration + 0.2, t0 + 0.1, chest, b3MulSV(push, hits[side].dir), &fell);
        if (fell) { printf("  %-12s fell (shove %.1f m/s too hard for this policy): not written\n", nm, push); free(tr.s); failed++; continue; }
        add[nadd] = bake_clip(d, w, rib, &tr, t0, cycles, nm, true, false);
        // How far the shove moved and turned the animal at its worst (taken out: see the header), and the heading left at the end.
        float drift = 0, yaw_end = 0, yaw_max = 0, deg, cm;
        for (int k = 0; k < tr.n; k++) {
            if (tr.s[k].t < t0) continue;
            float yaw; b3Vec3 off = pelvis_drift(&w->ref[0], tr.s[k].t, tr.s[k].pos, tr.s[k].rot, &yaw);
            drift = fmaxf(drift, sqrtf(lv_len2(off))); yaw_max = fmaxf(yaw_max, fabsf(yaw) * 57.2958f); yaw_end = yaw * 57.2958f;
        }
        check_clip(d, rig, &add[nadd], &tr, t0, dt, false, &deg, &cm);
        printf("  %-12s %5.2f s, %.1f m/s shove on the chest: knocked %.0f cm off its line, turned up to %.0f deg (%.0f at the end, eased out); pose within %.1f deg, %.1f cm\n",
            nm, add[nadd].duration, push, drift * 100, yaw_max, yaw_end, deg, cm);
        nadd++; free(tr.s);
    }
    if (failed) { fprintf(stderr, "loco_bake: %d hit clips fell; nothing written (the game wants all sixteen)\n", failed); return 1; }
    if (!write_dino(c.dino, out, d, add, nadd)) { fprintf(stderr, "loco_bake: cannot write %s\n", out); return 1; }
    printf("loco_bake: wrote %s (%d clips added)\n", out, nadd);
    loco_policy_free(pol); df_free(d); free(d); loco_world_close(w); free(w);
    return 0;
}
