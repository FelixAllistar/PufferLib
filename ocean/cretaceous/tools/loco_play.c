// Headless runner for the locomotion env (loco/loco_env.h,
// docs/locomotion/README.md). Run from the repository root.
//   loco_play [--dino PATH] [--clip walk] [--agents 32] [--seconds 20] [--seed 1]
//             [--substeps 4] [--passive-tail] [--policy CHECKPOINT.bin]
//             [--hertz-leg 100] [--hertz-body 60] [--hertz-tail 40] [--damping 1] [--no-weld]
//             [--rig] [--stability] [--baseline] [--bench] [--all]
//   --rig        the ragdoll built from the rig: bodies, shapes, masses, joints and limits
//   --stability  the chain checks at 1, 2, 4, 8 and 16 substeps: the clip's first pose held with
//                the pelvis pinned in the air, then free on the ground, then dropped from 0.5 m
//   --baseline   plain PD tracking (actions zero: the springs chase the clip, no learning):
//                how long the animals stay up and how closely they track
//   --policy     the same run with a trained checkpoint's mean actions (loco/loco_policy.h)
//   --bench      env throughput: agent steps per second on one core
//   --no-weld    keep the joints the clips never move (the full chain; see loco_rig.h)
// Exit status 1 when the rig does not build or a stability check fails at the
// configured substep count.
#include "loco/loco_check.h"
#include "loco/loco_policy.h"

static void print_rig(const LocoWorld* w) {
    const LocoRig* r = &w->rig;
    printf("rig: table %s, %d bodies, %d joints, %d action dimensions, %d end effectors, total mass %.1f kg, model %.2f m long, %.2f m tall\n",
        r->table, r->nbody, r->nbody - 1, r->ndof, r->nee, r->total_mass, r->length, r->height);
    printf("  %-10s %-9s %5s  %-24s %8s  %-6s %s\n", "body", "parent", "verts", "shape (m)", "mass kg", "joint", "limits (deg; the clips' range plus 14 deg)");
    for (int b = 0; b < r->nbody; b++) {
        const LocoBody* B = &r->body[b]; char shp[64], lim[96] = "";
        const LocoShape* S = &B->shape[0];
        if (B->nshape > 1) snprintf(shp, sizeof shp, "%d shapes (welded)", B->nshape);
        else if (S->type == LS_BOX) snprintf(shp, sizeof shp, "box %.2f x %.2f x %.2f", 2 * S->box_h.x, 2 * S->box_h.y, 2 * S->box_h.z);
        else snprintf(shp, sizeof shp, "capsule r %.3f len %.2f", S->radius, sqrtf(lv_len2(b3Sub(S->c2, S->c1))) + 2 * S->radius);
        if (B->jtype == LJ_HINGE) snprintf(lim, sizeof lim, "hinge %.0f..%.0f (fit %.2f)", B->lo * 57.2958f, B->hi * 57.2958f, B->hinge_fit);
        else if (B->jtype == LJ_BALL) snprintf(lim, sizeof lim, "cone %.0f, twist %.0f..%.0f", B->cone * 57.2958f, B->twist_lo * 57.2958f, B->twist_hi * 57.2958f);
        printf("  %-10s %-9s %5d  %-24s %8.2f  %-6s %s%s%s%s\n", B->name, B->parent >= 0 ? r->body[B->parent].name : "-", B->nvert, shp, B->mass,
            B->jtype == LJ_HINGE ? "hinge" : B->jtype == LJ_BALL ? "ball" : "root", lim,
            B->flags & LB_CONTACT ? "  [ground ok]" : "", B->flags & LB_TAIL ? "  [tail]" : "", B->flags & LB_EE ? "  [end effector]" : "");
    }
    float tail = 0, front = 0;
    for (int b = 0; b < r->nbody; b++) { if (loco_is_tail(&r->body[b])) tail += r->body[b].mass; else if (r->body[b].bind.x > r->body[0].bind.x) front += r->body[b].mass; }
    printf("  tail %.1f kg (%.0f%% of the body), parts ahead of the hips %.1f kg\n", tail, 100 * tail / r->total_mass, front);
    printf("reference: clip %s, %.2f s cycle (%d frames), %.2f m/s, pelvis %.2f m up\n", w->ref.clip, w->ref.duration, w->ref.period, w->ref.speed, w->pelvis_h);
}

// Prints the chain checks (loco/loco_check.h) at one substep count; false on a failure.
static bool stability(const LocoConfig* c, int substeps) {
    LocoStab o; char err[256];
    if (!loco_check_stability(c, substeps, &o, err, sizeof err)) { printf("loco_play: %s\n", err); return false; }
    printf("  substeps %2d  pinned:  drift %.2f deg mean, %.2f deg worst joint, separation %.2f cm, fastest body at the end %.3f m/s  %s\n",
        substeps, o.pin_mean, o.pin_worst, o.pin_sep * 100, o.pin_speed, o.pin_ok ? "ok" : "FAIL");
    printf("  substeps %2d  free:    stood %.2f s (nothing balances it); after 0.5 s drift %.2f deg mean, %.2f deg worst; separation %.2f cm  %s\n",
        substeps, o.free_up, o.free_mean, o.free_worst, o.free_sep * 100, o.free_ok ? "ok" : "FAIL");
    printf("  substeps %2d  dropped: separation %.2f cm at worst, fastest body %.2f m/s (free fall from 0.5 m: 3.1), settled drift %.1f deg  %s;  %.1f us per ragdoll step\n",
        substeps, o.drop_sep * 100, o.drop_speed, o.drop_mean, o.drop_ok ? "ok" : "FAIL", o.us_per_step);
    return o.pin_ok && o.free_ok && o.drop_ok;
}

// Runs every agent for the given time with zero actions (policy NULL) or a
// checkpoint's mean actions; prints the episode statistics.
static void run(const LocoConfig* c, unsigned int seed, float seconds, LocoPolicy* pol, const char* label) {
    LocoWorld* w = (LocoWorld*)calloc(1, sizeof(LocoWorld)); char err[256];
    if (!loco_world_init(w, c, seed, err, sizeof err)) { printf("loco_play: %s\n", err); free(w); return; }
    loco_reset_all(w);
    int n = c->num_agents, steps = (int)(seconds * c->policy_hz);
    float* obs = (float*)calloc((size_t)n * LOCO_OBS, sizeof(float)); float* act = (float*)calloc((size_t)n * LOCO_MAX_DOF, sizeof(float));
    const float* ap[LOCO_MAX_AGENTS]; float term[LOCO_MAX_AGENTS], rew[LOCO_MAX_AGENTS];
    for (int i = 0; i < n; i++) ap[i] = act + (size_t)i * LOCO_MAX_DOF;
    int ups = 0; float best = 0; double rsum = 0;
    for (int s = 0; s < steps; s++) {
        if (pol) {
            for (int i = 0; i < n; i++) loco_observe(w, i, obs + (size_t)i * LOCO_OBS);
            loco_policy_forward(pol, obs, n, act);
        }
        loco_step(w, pol ? ap : NULL, rew, term);
        for (int i = 0; i < n; i++) { rsum += rew[i]; if (term[i] && pol) loco_policy_reset(pol, i); }
    }
    // Agents still up at the end count with the time they have had.
    const LocoStats* st = &w->stats;
    for (int i = 0; i < n; i++) { ups++; best = fmaxf(best, w->ag[i].elapsed); }
    double eps = st->episodes;
    printf("%s: %d agents x %.0f s, %s clip, %d substeps%s\n", label, n, seconds, w->ref.clip, c->substeps, c->gains.passive ? ", passive tail" : "");
    if (eps > 0)
        printf("  %.0f episodes ended (%.0f falls): mean time up %.2f s, mean return %.1f, pelvis travelled %.2f m per episode; tracking error %.1f deg per joint, end effectors %.1f cm\n",
            eps, st->falls, st->length / eps, st->ret / eps, st->distance / eps, st->pose_deg / eps, st->ee_cm / eps);
    printf("  %d agents up at the end, longest current %.1f s; mean reward per step %.3f (1 = perfect tracking)\n", ups, best, rsum / ((double)steps * n));
    free(obs); free(act); loco_world_close(w); free(w);
}

static void bench(const LocoConfig* c) {
    LocoWorld* w = (LocoWorld*)calloc(1, sizeof(LocoWorld)); char err[256];
    if (!loco_world_init(w, c, 7, err, sizeof err)) { printf("loco_play: %s\n", err); free(w); return; }
    loco_reset_all(w);
    int n = c->num_agents; float* obs = (float*)malloc(sizeof(float) * LOCO_OBS); float* act = (float*)calloc((size_t)n * LOCO_MAX_DOF, sizeof(float));
    const float* ap[LOCO_MAX_AGENTS]; float rew[LOCO_MAX_AGENTS], term[LOCO_MAX_AGENTS];
    unsigned int rng = 3;
    for (int i = 0; i < n; i++) ap[i] = act + (size_t)i * LOCO_MAX_DOF;
    for (int k = 0; k < n * LOCO_MAX_DOF; k++) act[k] = 0.3f * (2 * loco_randf(&rng) - 1);
    double t0 = loco_clock(); int steps = 0;
    while (loco_clock() - t0 < 3.0) {
        loco_step(w, ap, rew, term);
        for (int i = 0; i < n; i++) loco_observe(w, i, obs);
        steps++;
    }
    double dt = loco_clock() - t0;
    printf("bench: %d agents per world (%d bodies each), %d substeps at %d Hz, policy %d Hz: %.0f agent steps/s on one core (%.1f simulated seconds per second per agent)\n",
        n, w->rig.nbody, c->substeps, c->sim_hz, c->policy_hz, steps * n / dt, steps / dt / c->policy_hz);
    free(obs); free(act); loco_world_close(w); free(w);
}

int main(int argc, char** argv) {
    LocoConfig c = loco_default_config();
    unsigned int seed = 1; float seconds = 20; const char* policy = NULL;
    bool do_rig = false, do_stab = false, do_base = false, do_bench = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i]; bool more = i + 1 < argc;
        if (!strcmp(a, "--dino") && more) snprintf(c.dino, sizeof c.dino, "%s", argv[++i]);
        else if (!strcmp(a, "--clip") && more) snprintf(c.clip, sizeof c.clip, "%s", argv[++i]);
        else if (!strcmp(a, "--agents") && more) c.num_agents = atoi(argv[++i]);
        else if (!strcmp(a, "--seconds") && more) seconds = (float)atof(argv[++i]);
        else if (!strcmp(a, "--seed") && more) seed = (unsigned int)atoi(argv[++i]);
        else if (!strcmp(a, "--substeps") && more) c.substeps = atoi(argv[++i]);
        else if (!strcmp(a, "--policy") && more) policy = argv[++i];
        else if (!strcmp(a, "--passive-tail")) c.gains.passive = true;
        else if (!strcmp(a, "--no-weld")) c.weld_range = 0;
        else if (!strcmp(a, "--hertz-leg") && more) c.gains.leg = (float)atof(argv[++i]);
        else if (!strcmp(a, "--hertz-body") && more) c.gains.body = (float)atof(argv[++i]);
        else if (!strcmp(a, "--hertz-tail") && more) c.gains.tail = (float)atof(argv[++i]);
        else if (!strcmp(a, "--damping") && more) c.gains.damping = (float)atof(argv[++i]);
        else if (!strcmp(a, "--rig")) do_rig = true;
        else if (!strcmp(a, "--stability")) do_stab = true;
        else if (!strcmp(a, "--baseline")) do_base = true;
        else if (!strcmp(a, "--bench")) do_bench = true;
        else if (!strcmp(a, "--all")) do_rig = do_stab = do_base = do_bench = true;
        else { fprintf(stderr, "loco_play: unknown argument %s (see the header of tools/loco_play.c)\n", a); return 2; }
    }
    if (!do_rig && !do_stab && !do_base && !do_bench && !policy) do_rig = do_base = true;
    LocoWorld* w = (LocoWorld*)calloc(1, sizeof(LocoWorld)); char err[256];
    LocoConfig one = c; one.num_agents = 1;
    if (!loco_world_init(w, &one, seed, err, sizeof err)) { fprintf(stderr, "loco_play: %s\n", err); return 1; }
    if (do_rig) print_rig(w);
    loco_world_close(w); free(w);
    bool ok = true;
    if (do_stab) {
        printf("stability (%d Hz physics, spring gains legs %.0f Hz, body %.0f Hz, tail %.0f Hz, damping ratio %.1f):\n", c.sim_hz, c.gains.leg, c.gains.body, c.gains.tail, c.gains.damping);
        int sub[5] = {1, 2, 4, 8, 16}; bool listed = false;
        for (int k = 0; k < 5; k++) { bool r = stability(&c, sub[k]); if (sub[k] == c.substeps) { ok = r; listed = true; } }
        if (!listed) ok = stability(&c, c.substeps);
        printf("stability: %s at %d substeps (the configured count)\n", ok ? "ok" : "FAILED", c.substeps);
    }
    if (do_base) run(&c, seed, seconds, NULL, "PD-tracking baseline");
    if (policy) {
        LocoPolicy* p = loco_policy_load(policy, c.num_agents, err, sizeof err);
        if (!p) { fprintf(stderr, "loco_play: %s\n", err); return 1; }
        run(&c, seed, seconds, p, "policy");
        loco_policy_free(p);
    }
    if (do_bench) bench(&c);
    return ok ? 0 : 1;
}
