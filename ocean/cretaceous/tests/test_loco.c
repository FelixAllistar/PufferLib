// The locomotion env (loco/, docs/locomotion/README.md) on the biped test rig
// (tests/fixtures/fixture_biped.dino): the ragdoll builds with sane bodies and
// joint limits that hold the clips' range; the joint limits hold under
// random actions; the chain stability checks pass at the default substeps;
// reference state initialisation puts the body exactly in the clip's pose and
// velocities at its phase; the reward is 1 at a perfect track and less off
// it; same seed, same run; the CPU policy runner reads the trainer's
// checkpoint layout and reproduces a real trained policy's actions;
// and the PufferLib binding (built here with CRET_LOCO, as the trainer is)
// runs: observations finite and padded, rewards in [0, 1], terminals and logs;
// config/cretaceous_loco.ini loads the way the trainer loads it, takes
// command-line overrides, and every key in its [env] is one the binding reads.
// When the licensed raptor is installed (tools/fetch_dinos.sh) its rig must
// build with the raptor table too, at metre scale (its bind pose is in cm).
//   test_loco
#include "test_util.h"
#include "cretaceous.h"
#include "loco/loco_check.h"
#include "loco/loco_policy.h"

#define FIXTURE "ocean/cretaceous/tests/fixtures/fixture_biped.dino"
#define RAPTOR "ocean/cretaceous/assets/dinos/raptor.dino"
#define POLICY_RUN2 "ocean/cretaceous/tests/fixtures/loco_policy_run2"

static LocoWorld* make_world(const char* dino, int agents, unsigned int seed) {
    LocoConfig c = loco_default_config();
    snprintf(c.dino, sizeof c.dino, "%s", dino); c.num_agents = agents;
    LocoWorld* w = (LocoWorld*)calloc(1, sizeof(LocoWorld)); char err[256];
    if (!loco_world_init(w, &c, seed, err, sizeof err)) { fprintf(stderr, "test_loco: %s: %s\n", dino, err); free(w); return NULL; }
    return w;
}
static void free_world(LocoWorld* w) { if (w) { loco_world_close(w); free(w); } }

// Swing (deg) and twist (deg) of a ball joint's current deviation, or a hinge's angle.
static void joint_angles(const LocoBody* B, b3Quat qa, b3Quat qb, float* swing, float* twist) {
    b3Quat d = loco_joint_dev(B, qa, qb);
    b3Vec3 z = lq_rot(d, lv(0, 0, 1));
    *swing = acosf(fminf(fmaxf(z.z, -1.0f), 1.0f)); *twist = lq_twist_z(d);
}

static void test_rig(void) {
    LocoWorld* w = make_world(FIXTURE, 1, 1);
    CHECK(w != NULL, "fixture rig builds");
    if (!w) return;
    const LocoRig* r = &w->rig;
    CHECK(!strcmp(r->table, "testrig"), "fixture uses the testrig table (got %s)", r->table);
    // The fixture's clips hold its spine, neck, head and toes still: those joints are welded, leaving the
    // pelvis (with the torso, neck and head as shapes), three tail segments and three bodies per leg.
    CHECK(r->nbody == 10, "10 bodies (got %d)", r->nbody);
    CHECK(r->ndof == 19 && r->ndof <= LOCO_MAX_DOF, "19 action dimensions (got %d)", r->ndof);
    CHECK(r->nee == 3, "3 end effectors: head and two feet (got %d)", r->nee);
    CHECK(r->body[0].nshape == 4, "the pelvis keeps a shape per welded group (got %d)", r->body[0].nshape);
    CHECK(r->total_mass > 50 && r->total_mass < 200, "total mass %.1f kg", r->total_mass);
    int hinges = 0, tails = 0, contact = 0;
    for (int b = 0; b < r->nbody; b++) {
        const LocoBody* B = &r->body[b];
        CHECK(B->mass > 0.1f && isfinite(B->mass), "%s mass %.2f", B->name, B->mass);
        CHECK(b == 0 ? B->parent == -1 : (B->parent >= 0 && B->parent < b), "%s parent %d", B->name, B->parent);
        hinges += B->jtype == LJ_HINGE; tails += loco_is_tail(B); contact += (B->flags & LB_CONTACT) != 0;
    }
    CHECK(hinges == 4 && tails == 3 && contact == 2, "hinges %d (4), tail bodies %d (3), ground-contact bodies %d (2)", hinges, tails, contact);
    // The mass is the shapes' volume at LOCO_DENSITY.
    float m = 0;
    for (int b = 0; b < r->nbody; b++) for (int k = 0; k < r->body[b].nshape; k++) m += r->body[b].shape[k].volume * LOCO_DENSITY;
    CHECK(fabsf(m - r->total_mass) < 1e-3f * r->total_mass, "mass from volume (%.2f, %.2f)", m, r->total_mass);
    // Every frame of every limit clip lies inside its joint's limits (they are the clips' range plus a margin).
    for (int f = 0; f < w->ref.period; f++) {
        const b3Quat* q = w->ref.rot + (size_t)f * r->nbody;
        for (int b = 1; b < r->nbody; b++) {
            const LocoBody* B = &r->body[b]; float sw, tw;
            joint_angles(B, q[B->parent], q[b], &sw, &tw);
            if (B->jtype == LJ_HINGE) CHECK_CAPPED(tw >= B->lo && tw <= B->hi, 4, "%s frame %d hinge %.1f outside %.1f..%.1f", B->name, f, tw * 57.3f, B->lo * 57.3f, B->hi * 57.3f);
            else CHECK_CAPPED(sw <= B->cone && tw >= B->twist_lo && tw <= B->twist_hi, 4, "%s frame %d swing %.1f twist %.1f outside", B->name, f, sw * 57.3f, tw * 57.3f);
        }
    }
    free_world(w);
}

// Random full-scale actions for 20 s (a stress test: a policy's actions are smoother): the joints stay
// near their limits. Box3D's limits are soft and solved once per substep, so a light segment whipped by
// random targets passes one briefly (on the fixture at 4 substeps: 6.5% of joint samples by over 3
// degrees, 0.3% by over 10, the worst 31). Guards: at most 1% beyond 10 degrees, none beyond 45.
static void test_limits(void) {
    LocoWorld* w = make_world(FIXTURE, 8, 5);
    if (!w) return;
    loco_reset_all(w);
    static float act[8][LOCO_MAX_DOF]; const float* ap[8]; uint32_t rng = 11; float worst = 0; long samples = 0, out = 0;
    for (int i = 0; i < 8; i++) ap[i] = act[i];
    for (int s = 0; s < 20 * 30; s++) {
        for (int i = 0; i < 8; i++) for (int k = 0; k < LOCO_MAX_DOF; k++) act[i][k] = (2 * t_frand(&rng) - 1) * w->cfg.action_scale / w->cfg.action_unit;
        loco_step(w, ap, NULL, NULL);
        for (int i = 0; i < 8; i++) {
            loco_doll_get(&w->phys, i, w->pos, w->rot, NULL, NULL);
            for (int b = 1; b < w->rig.nbody; b++) {
                const LocoBody* B = &w->rig.body[b]; float sw, tw, over;
                joint_angles(B, w->rot[B->parent], w->rot[b], &sw, &tw);
                if (B->jtype == LJ_HINGE) over = fmaxf(B->lo - tw, tw - B->hi);
                else over = fmaxf(sw - B->cone, fmaxf(B->twist_lo - tw, tw - B->twist_hi));
                worst = fmaxf(worst, over); samples++; out += over > 10.0f / 57.3f;
            }
        }
    }
    CHECK(out * 100 <= samples && worst < 45.0f / 57.3f, "joint limits hold under random actions: %ld of %ld samples over by 10 deg, worst %.1f deg", out, samples, worst * 57.3f);
    free_world(w);
}

static void test_stability(void) {
    LocoConfig c = loco_default_config(); snprintf(c.dino, sizeof c.dino, "%s", FIXTURE);
    LocoStab o; char err[256];
    bool built = loco_check_stability(&c, c.substeps, &o, err, sizeof err);
    CHECK(built, "stability world builds: %s", built ? "" : err);
    CHECK(o.pin_ok, "pinned: drift %.2f deg, separation %.2f cm, speed %.3f m/s", o.pin_mean, o.pin_sep * 100, o.pin_speed);
    CHECK(o.free_ok, "free: separation %.2f cm", o.free_sep * 100);
    CHECK(o.free_up > 0.5f, "free: stands at least 0.5 s on the PD springs (%.2f s)", o.free_up);
    CHECK(o.drop_ok, "dropped: separation %.2f cm, fastest %.1f m/s", o.drop_sep * 100, o.drop_speed);
}

// Reference state initialisation and the reward at a perfect track.
static void test_rsi_reward(void) {
    LocoWorld* w = make_world(FIXTURE, 4, 3);
    if (!w) return;
    const float phases[4] = {0.0f, 0.25f, 0.6f, 0.97f};
    for (int i = 0; i < 4; i++) {
        loco_reset_agent(w, i, phases[i]);
        double t = (double)phases[i] * w->ref.duration;
        CHECK(fabs(w->ag[i].t - t) < 1e-9, "agent %d starts at clip time %.3f", i, t);
        loco_doll_get(&w->phys, i, w->pos, w->rot, w->vel, w->angvel);
        loco_ref_at(w, t);
        float dp = 0, dq = 0, dv = 0, dw = 0;
        for (int b = 0; b < w->rig.nbody; b++) {
            dp = fmaxf(dp, sqrtf(lv_len2(b3Sub(w->pos[b], w->rpos[b]))));
            dq = fmaxf(dq, lq_angle(w->rot[b], w->rrot[b]));
            b3Vec3 vc = b3Add(w->rvel[b], b3Cross(w->rangvel[b], lq_rot(w->rrot[b], w->rig.body[b].com)));
            dv = fmaxf(dv, sqrtf(lv_len2(b3Sub(w->vel[b], vc)))); dw = fmaxf(dw, sqrtf(lv_len2(b3Sub(w->angvel[b], w->rangvel[b]))));
        }
        CHECK(dp < 1e-4f && dq < 1e-3f, "agent %d RSI pose matches the clip at phase %.2f (%.2g m, %.2g rad)", i, phases[i], dp, dq);
        CHECK(dv < 1e-3f && dw < 1e-3f, "agent %d RSI velocities match (%.2g m/s, %.2g rad/s)", i, dv, dw);
        float wx = w->pos[0].x + w->phys.doll[i].origin.x;
        CHECK(wx > -0.5f && wx < 0.5f, "agent %d pelvis starts near its lane origin (world x %.2f)", i, wx);
        // Perfect track: the state is the reference, with centre-of-mass velocities as the sim reports them.
        for (int b = 0; b < w->rig.nbody; b++) {
            w->pos[b] = w->rpos[b]; w->rot[b] = w->rrot[b]; w->angvel[b] = w->rangvel[b];
            w->vel[b] = b3Add(w->rvel[b], b3Cross(w->rangvel[b], lq_rot(w->rrot[b], w->rig.body[b].com)));
        }
        LocoAgent a = w->ag[i];
        float r = loco_reward(w, &a);
        CHECK(fabsf(r - 1.0f) < 1e-5f, "reward is 1 at a perfect track (%.7f)", r);
        w->rot[3] = lq_mul(w->rot[3], lq_axis_angle(lv(1, 0, 0), 0.3f));
        float r2 = loco_reward(w, &a);
        CHECK(r2 < r - 0.01f && r2 > 0, "a bent joint lowers it (%.4f)", r2);
    }
    // The reward weights sum to 1, so the reward lies in [0, 1].
    const LocoConfig* c = &w->cfg;
    CHECK(fabsf(c->w_pose + c->w_vel + c->w_ee + c->w_com - 1.0f) < 1e-6f, "reward weights sum to 1");
    free_world(w);
}

static uint64_t run_hash(unsigned int seed, int steps) {
    LocoWorld* w = make_world(FIXTURE, 6, seed);
    if (!w) return 0;
    loco_reset_all(w);
    static float act[6][LOCO_MAX_DOF]; const float* ap[6]; uint32_t rng = seed * 7 + 1;
    for (int i = 0; i < 6; i++) ap[i] = act[i];
    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < 6; i++) for (int k = 0; k < LOCO_MAX_DOF; k++) act[i][k] = 0.5f * (2 * t_frand(&rng) - 1);
        loco_step(w, ap, NULL, NULL);
    }
    uint64_t h = loco_hash(w);
    free_world(w);
    return h;
}
static void test_determinism(void) {
    uint64_t a = run_hash(9, 150), b = run_hash(9, 150), c = run_hash(10, 150);
    CHECK(a == b, "same seed, same hash (%016llx, %016llx)", (unsigned long long)a, (unsigned long long)b);
    CHECK(a != c, "another seed, another run");
}

// tail_flex: the tail keeps that many joints from the base, the rest one rigid
// rod carrying the tip's end-effector flag (if any), and the mass is unchanged.
static void test_tail_flex(void) {
    LocoWorld* a = make_world(FIXTURE, 1, 1);
    LocoConfig c = loco_default_config(); snprintf(c.dino, sizeof c.dino, "%s", FIXTURE); c.num_agents = 1; c.tail_flex = 1;
    LocoWorld* b = (LocoWorld*)calloc(1, sizeof(LocoWorld)); char err[256];
    bool ok = a && loco_world_init(b, &c, 1, err, sizeof err);
    CHECK(ok, "tail_flex 1 builds");
    if (ok) {
        int ta = 0, tb = 0, ee = -1; bool had_ee = false;
        for (int k = 0; k < a->rig.nbody; k++) if (loco_is_tail(&a->rig.body[k])) { ta++; had_ee |= (a->rig.body[k].flags & LB_EE) != 0; }
        for (int k = 0; k < b->rig.nbody; k++) if (loco_is_tail(&b->rig.body[k])) { tb++; if (b->rig.body[k].flags & LB_EE) ee = k; }
        CHECK(ta > 1 && tb == 1 && b->rig.nbody == a->rig.nbody - (ta - 1), "the tail's %d segments become one rod (%d bodies of %d)", ta, b->rig.nbody, a->rig.nbody);
        CHECK((ee >= 0) == had_ee && a->rig.nee == b->rig.nee, "the rod keeps the tail's end effector, if it had one (%d, %d)", a->rig.nee, b->rig.nee);
        CHECK(fabsf(b->rig.total_mass - a->rig.total_mass) < 1e-3f * a->rig.total_mass, "same mass (%.2f, %.2f kg)", b->rig.total_mass, a->rig.total_mass);
        loco_world_close(b);
    }
    free(b); free_world(a);
}

// A shove: twin worlds, one pushed along +x by 1 m/s over 0.1 s; after it the
// pushed animal's centre of mass moves about 1 m/s faster (the feet's friction
// and the springs take some). Random shoves (push_interval) start and end.
static void test_push(void) {
    LocoWorld* a = make_world(FIXTURE, 1, 3); LocoWorld* b = make_world(FIXTURE, 1, 3);
    CHECK(a && b, "push worlds build");
    if (!a || !b) { free_world(a); free_world(b); return; }
    loco_reset_agent(a, 0, 0.0f); loco_reset_agent(b, 0, 0.0f);
    loco_push(b, 0, 0, lv(1, 0, 0), 0.1f);
    for (int s = 0; s < 3; s++) { loco_step(a, NULL, NULL, NULL); loco_step(b, NULL, NULL, NULL); }
    loco_doll_get(&a->phys, 0, a->pos, a->rot, a->vel, a->angvel); loco_doll_get(&b->phys, 0, b->pos, b->rot, b->vel, b->angvel);
    b3Vec3 va = loco_com_vel(a, a->rot, a->vel, a->angvel, false), vb = loco_com_vel(b, b->rot, b->vel, b->angvel, false);
    float dx = vb.x - va.x, dz = vb.z - va.z;
    CHECK(dx > 0.7f && dx < 1.1f && fabsf(dz) < 0.2f, "a 1 m/s shove: COM %.2f m/s faster along x, %.2f across", dx, dz);
    CHECK(b->ag[0].push_left <= 0, "the shove has ended after its 0.1 s");
    free_world(a); free_world(b);
    LocoConfig c = loco_default_config();
    snprintf(c.dino, sizeof c.dino, "%s", FIXTURE); c.num_agents = 4; c.push_interval = 0.2f;
    LocoWorld* w = (LocoWorld*)calloc(1, sizeof(LocoWorld)); char err[256];
    if (!loco_world_init(w, &c, 5, err, sizeof err)) { CHECK(0, "push world: %s", err); free(w); return; }
    loco_reset_all(w); int started = 0;
    for (int s = 0; s < 60; s++) { loco_step(w, NULL, NULL, NULL); for (int i = 0; i < 4; i++) started += w->ag[i].push_left > 0; }
    CHECK(started > 0, "random shoves happen with push_interval set (%d agent steps under a shove)", started);
    free_world(w);
}

// The CPU runner: a hand-made checkpoint (hidden 32, one layer) with zero
// encoder and MinGRU weights and an all-ones decoder gives every action
// hidden * 0.125 (h~ = 0.5, gate 0.5, h = 0.25, skip gate 0.5, x = 0).
static void test_policy(void) {
    int H = 32; long n = loco_policy_count(H, 1);
    float* wts = (float*)calloc((size_t)n, sizeof(float));
    float* dec = wts + (long)H * LOCO_OBS;
    for (long k = 0; k < (long)(LOCO_MAX_DOF + 1) * H; k++) dec[k] = 1.0f;
    char err[256]; LocoPolicy* p = loco_policy_create(wts, n, 2, err, sizeof err);
    CHECK(p && p->hidden == H && p->layers == 1, "checkpoint size gives hidden 32, one layer");
    if (p) {
        static float obs[2 * LOCO_OBS], act[2 * LOCO_MAX_DOF];
        for (int k = 0; k < 2 * LOCO_OBS; k++) obs[k] = 0.1f * (k % 7);
        loco_policy_forward(p, obs, 2, act);
        CHECK(fabsf(act[0] - H * 0.125f) < 1e-4f && fabsf(act[2 * LOCO_MAX_DOF - 1] - H * 0.125f) < 1e-4f, "known output (%.4f)", act[0]);
        loco_policy_free(p);
    }
    float* bad = (float*)calloc(100, sizeof(float));
    CHECK(loco_policy_create(bad, 100, 1, err, sizeof err) == NULL, "a wrong-sized file is refused");
    // A real checkpoint against the trainer's own actions: run 2's final policy and 60 steps of one
    // agent recorded inside the trainer (observation, then the action the trainer sampled for it).
    // The runner's mean must sit within the policy's std of every sampled action on average.
    LocoPolicy* rp = loco_policy_load(POLICY_RUN2 ".bin", 1, err, sizeof err);
    FILE* tf = fopen(POLICY_RUN2 "_trace.bin", "rb");
    CHECK(rp && tf, "run 2 checkpoint and trace load");
    if (rp && tf) {
        const float* logstd = rp->dec + (long)(LOCO_MAX_DOF + 1) * rp->hidden;
        static float o[LOCO_OBS], ta[LOCO_MAX_DOF + 1], ma[LOCO_MAX_DOF]; double z2 = 0; long nz = 0;
        while (fread(o, 4, LOCO_OBS, tf) == LOCO_OBS && fread(ta, 4, LOCO_MAX_DOF + 1, tf) == LOCO_MAX_DOF + 1) {
            loco_policy_forward(rp, o, 1, ma);
            for (int k = 0; k < LOCO_MAX_DOF; k++) if (logstd[k] != 0) { float z = (ta[k] - ma[k]) / expf(logstd[k]); z2 += z * z; nz++; }
        }
        double rms = nz ? sqrt(z2 / nz) : 99;
        CHECK(nz > 1000 && rms > 0.8 && rms < 1.2, "the runner reproduces the trainer's actions: rms z %.3f over %ld values (1 = within the policy's noise)", rms, nz);
    }
    if (tf) fclose(tf);
    loco_policy_free(rp);
}

// The binding as the trainer drives it: config keys, reset, steps with random actions.
static void test_binding(void) {
    Dict kw = {0};
    dict_set_str(&kw, "dino", FIXTURE); dict_set_str(&kw, "clip", "walk");
    dict_set(&kw, "num_agents", 8); dict_set(&kw, "episode_seconds", 2); dict_set(&kw, "substeps", 4);
    Env* env = (Env*)calloc(1, sizeof(Env)); env->rng = 17;
    puf_init(env, &kw);
    CHECK(env->num_agents == 8, "num_agents from the config");
    CHECK(env->w->cfg.episode_seconds == 2.0f && env->w->cfg.substeps == 4, "loco keys read");
    int n = env->num_agents;
    float* obs = (float*)calloc((size_t)n * OBS_SIZE, sizeof(float)); float* act = (float*)calloc((size_t)n * NUM_ATNS, sizeof(float));
    float rew[8], term[8];
    for (int i = 0; i < n; i++) {
        env->agents[i].observations = obs + (size_t)i * OBS_SIZE; env->agents[i].actions = act + (size_t)i * NUM_ATNS;
        env->agents[i].rewards = rew + i; env->agents[i].terminals = term + i;
    }
    int act_sizes[] = ACT_SIZES, ones = 0;
    for (int k = 0; k < NUM_ATNS; k++) ones += act_sizes[k] == 1;
    CHECK(ones == NUM_ATNS && NUM_ATNS == LOCO_MAX_DOF && OBS_SIZE == LOCO_OBS, "continuous heads: %d of %d size 1", ones, NUM_ATNS);
    puf_reset(env);
    uint32_t rng = 5; long terms = 0; bool finite = true, inrange = true, padded = true;
    int used = env->w->rig.nbody * LOCO_BODY_OBS;
    for (int s = 0; s < 300; s++) {
        for (int k = 0; k < n * NUM_ATNS; k++) act[k] = 3 * (2 * t_frand(&rng) - 1);   // out of range on purpose: the env clamps
        puf_step(env);
        for (int i = 0; i < n; i++) {
            inrange &= rew[i] >= 0 && rew[i] <= 1 && (term[i] == 0 || term[i] == 1);
            terms += term[i] != 0;
            for (int k = 0; k < OBS_SIZE; k++) {
                float v = obs[(size_t)i * OBS_SIZE + k];
                finite &= isfinite(v) && fabsf(v) < 100;
                if (k >= used && k < LOCO_MAX_BODIES * LOCO_BODY_OBS) padded &= v == 0;
            }
        }
    }
    CHECK(finite, "observations finite and bounded");
    CHECK(padded, "observation padding beyond the rig's bodies is zero");
    CHECK(inrange, "rewards in [0, 1], terminals 0 or 1");
    CHECK(terms >= n * 4, "episodes end (falls or the 2 s limit): %ld terminals", terms);
    CHECK(env->log.n > 0 && env->log.episode_length > 0 && env->log.episode_length / env->log.n <= 2.0f + 1e-3f,
        "log: %.0f episodes, mean length %.2f s", env->log.n, env->log.n > 0 ? env->log.episode_length / env->log.n : 0);
    Dict out = {0}; puf_log(&env->log, &out);
    CHECK(dict_find(&out, "pose_deg") && dict_find(&out, "perf"), "puf_log writes the loco keys");
    // The trainer numbers its envs 0, 1, 2 ... in rng: each gets its own seed.
    Env* e0 = (Env*)calloc(1, sizeof(Env)); Env* e1 = (Env*)calloc(1, sizeof(Env)); e0->rng = 0; e1->rng = 1;
    puf_init(e0, &kw); puf_init(e1, &kw);
    CHECK(e0->w->rng != e1->w->rng && e0->w->rng && e1->w->rng, "envs 0 and 1 get different nonzero seeds");
    puf_close(e0); puf_close(e1); free(e0); free(e1);
    dict_clear(&out); dict_clear(&kw);
    puf_close(env); free(env); free(obs); free(act);
}

// config/cretaceous_loco.ini through the trainer's own ini code (src/ini.h), loaded over default.ini and
// cretaceous.ini as loco/train.sh's overrides apply it: every [env] key is one the binding reads, and the
// env builds from that section (with the fixture in place of the licensed raptor).
static void test_config(void) {
    static const char* const KNOWN[] = {"dino", "clip",
#define LOCO_NAME(field, key, type) #key,
        LOCO_KEYS(LOCO_NAME)
#undef LOCO_NAME
    };
    Ini own = {0};
    puf_ini_load_file(&own, "config/cretaceous_loco.ini");
    Dict* e = puf_ini_section(&own, "env", 0);
    CHECK(e != NULL && e->size > 10, "config/cretaceous_loco.ini has an [env] section");
    for (int i = 0; e && i < e->size; i++) {
        bool known = false;
        for (size_t k = 0; k < sizeof KNOWN / sizeof KNOWN[0]; k++) known |= !strcmp(KNOWN[k], e->items[i].key);
        CHECK(known, "[env] key %s is one the loco binding reads", e->items[i].key);
    }
    Dict* p = puf_ini_section(&own, "policy", 0);
    CHECK(p && dict_find(p, "cretaceous_net") && dict_get(p, "cretaceous_net") == 0, "the config keeps the default encoder");
    puf_ini_free(&own);
    Ini ini = {0};
    // Exactly what the loco trainer loads (train.sh names its env cretaceous_loco):
    // default.ini, this file, then command-line overrides, which must find every key.
    char* argv[] = {("--env.dino=" FIXTURE), "--env.passive_tail=1", "--policy.hidden_size=256"};
    puf_ini_load_env(&ini, "cretaceous_loco", 3, argv);
    Dict* env_kw = puf_ini_section(&ini, "env", 0);
    Env* env = (Env*)calloc(1, sizeof(Env)); env->rng = 3;
    puf_init(env, env_kw);
    CHECK(env->num_agents == (int)dict_get(env_kw, "num_agents") && env->w->cfg.substeps == (int)dict_get(env_kw, "substeps")
        && fabsf(env->w->cfg.w_pose - (float)dict_get(env_kw, "reward_pose")) < 1e-6f && !strcmp(env->w->cfg.clip, dict_get_str(env_kw, "clip")),
        "the binding takes the config's values");
    CHECK(env->w->cfg.gains.passive && dict_get(puf_ini_section(&ini, "policy", 0), "hidden_size") == 256,
        "command-line overrides reach the binding and the policy");
    puf_close(env); free(env); puf_ini_free(&ini);
}

static void test_raptor(void) {
    FILE* f = fopen(RAPTOR, "rb");
    if (!f) { fprintf(T_ERR, "  (no %s: the licensed raptor is not installed, its rig is not checked)\n", RAPTOR); return; }
    fclose(f);
    LocoWorld* w = make_world(RAPTOR, 1, 1);
    CHECK(w != NULL, "the raptor's rig builds");
    if (!w) return;
    CHECK(!strcmp(w->rig.table, "raptor") && w->rig.nbody >= 18, "raptor table, %d bodies", w->rig.nbody);
    // The raptor's bind pose is in centimetres; the builder must bring it to
    // metres, or the body is a hundred times too big.
    CHECK(w->rig.height > 0.5f && w->rig.height < 3.0f && w->rig.total_mass > 20.0f && w->rig.total_mass < 1000.0f,
          "raptor in metres: %.2f m tall, %.1f kg", w->rig.height, w->rig.total_mass);
    free_world(w);
}

int main(void) {
    double t0 = t_now();
    test_rig();
    test_limits();
    test_stability();
    test_rsi_reward();
    test_determinism();
    test_push();
    test_tail_flex();
    test_policy();
    test_binding();
    test_config();
    test_raptor();
    return t_finish("test_loco", t0);
}
