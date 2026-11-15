/*
 * Runtime tests. Usage: test_mm BUILD_DIR
 *
 * Needs BUILD_DIR/test.mmdb (version 2), test_v1.mmdb (the same build as
 * version 1), test_nofingers.mmdb (version 2, fingers dropped) and
 * fk_reference.txt from tests/make_fixtures.py.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mm.h"

static int failures = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            failures++;                                     \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

/* A small deterministic generator for test inputs only. */
static uint64_t rng_state = 88172645463325252ull;
static float frand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (float)((rng_state >> 11) * (1.0 / 9007199254740992.0));
}
static float nrand(void) {
    float u = frand() + 1e-9f, v = frand();
    return sqrtf(-2.0f * logf(u)) * cosf(6.2831853f * v);
}

static float dist3(mm_vec3 a, mm_vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static int walk_frame;
static float walk_speed;

/* Angle between two rotations in degrees, in double precision: 2 atan2 of the
 * vector and scalar parts of a b^-1 (acos of a float dot product is too coarse
 * near zero). */
static float quat_angle_deg(mm_quat a, mm_quat b) {
    double ax = a.x, ay = a.y, az = a.z, aw = a.w, bx = -b.x, by = -b.y, bz = -b.z, bw = b.w;
    double x = aw * bx + ax * bw + ay * bz - az * by;
    double y = aw * by - ax * bz + ay * bw + az * bx;
    double z = aw * bz + ax * by - ay * bx + az * bw;
    double w = aw * bw - ax * bx - ay * by - az * bz;
    return (float)(2.0 * atan2(sqrt(x * x + y * y + z * z), fabs(w)) * 57.29577951308232);
}

static void test_fk(const mm_db *db, const char *dir, float tolerance) {
    char path[512];
    snprintf(path, sizeof(path), "%s/fk_reference.txt", dir);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL, "cannot open %s", path);
    if (!f) return;
    int J, N, F;
    if (fscanf(f, "%d %d %d %d %f", &J, &N, &F, &walk_frame, &walk_speed) != 5) {
        CHECK(0, "bad reference header");
        fclose(f);
        return;
    }
    CHECK(J == db->joint_count && N == db->frame_count && F == db->feature_count, "database sizes differ from the builder");
    mm_quat *rot = malloc(sizeof(mm_quat) * (size_t)J);
    mm_vec3 *wp = malloc(sizeof(mm_vec3) * (size_t)J);
    mm_quat *wr = malloc(sizeof(mm_quat) * (size_t)J);
    float worst = 0.0f;
    int frames = 0, frame;
    while (fscanf(f, "%d", &frame) == 1) {
        mm_vec3 hips;
        mm_sample_pose(db, frame, 0.0f, rot, &hips);
        mm_forward_kinematics(db, rot, hips, db->root_pos[frame], mm_quat_from_yaw(db->root_yaw[frame]), wp, wr);
        for (int j = 0; j < J; j++) {
            mm_vec3 ref;
            if (fscanf(f, "%f %f %f", &ref.x, &ref.y, &ref.z) != 3) break;
            float d = dist3(ref, wp[j]);
            if (d > worst) worst = d;
        }
        frames++;
    }
    fclose(f);
    printf("fk (version %d): %d frames, max joint error vs builder %.2e m\n", db->version, frames, worst);
    CHECK(frames > 100, "too few reference frames");
    CHECK(worst < tolerance, "forward kinematics differs from the builder by %g", worst);
    free(rot);
    free(wp);
    free(wr);
}

static void test_search(const mm_db *db) {
    int F = db->feature_count;
    float *q = malloc(sizeof(float) * (size_t)F);
    uint32_t crouch = mm_db_tag_bit(db, "crouch"), stand = mm_db_tag_bit(db, "stand");
    uint32_t mirrored = mm_db_tag_bit(db, "mirrored"), civilian = mm_db_tag_bit(db, "civilian");
    uint32_t suspect = mm_db_tag_bit(db, "suspect");
    CHECK(crouch && stand && mirrored && civilian && suspect, "missing tags");
    int mismatches = 0, found = 0;
    for (int t = 0; t < 2000; t++) {
        int base = (int)(frand() * (float)db->frame_count);
        float noise = t % 3 == 0 ? 0.05f : (t % 3 == 1 ? 0.5f : 2.0f);
        mm_db_frame_features(db, base, q);
        for (int d = 0; d < F; d++) q[d] += noise * nrand();
        mm_filter filter = {0, 0};
        /* With version 2 stand and crouch are frame tags, the others clip tags;
         * suspect is the clip that crouches halfway. */
        switch (t % 8) {
            case 1: filter.required = stand; break;
            case 2: filter.required = crouch; filter.forbidden = mirrored; break;
            case 3: filter.forbidden = mirrored | civilian; break;
            case 4: filter.required = crouch | civilian; break; /* No clip has both. */
            case 5: filter.required = suspect | crouch; break;
            case 6: filter.required = suspect; filter.forbidden = crouch | mirrored; break;
            case 7: filter.forbidden = stand; break;
            default: break;
        }
        float limit = t % 7 == 0 ? 5.0f : INFINITY;
        float c_fast = 0.0f, c_ref = 0.0f;
        int a = mm_search(db, q, filter, limit, &c_fast);
        int b = mm_search_brute_force(db, q, filter, limit, &c_ref);
        if (a != b || c_fast != c_ref) mismatches++;
        if (a >= 0) {
            found++;
            CHECK(mm_frame_allowed(db, a, filter), "search returned a filtered frame");
        }
    }
    printf("search (version %d): 2000 random queries, %d with results, %d mismatches against brute force\n",
           db->version, found, mismatches);
    CHECK(mismatches == 0, "accelerated search differs from brute force");
    CHECK(found > 1000, "too few queries found a result");
    free(q);
}

static void test_trajectory_features(const mm_db *db) {
    /* A capsule walking straight at the clip's speed predicts the same
     * trajectory the builder extracted from the straight walk. */
    mm_spring_state s;
    memset(&s, 0, sizeof(s));
    s.velocity = mm_vec3_make(0.0f, 0.0f, walk_speed);
    mm_vec3 pos[MM_MAX_TRAJECTORY];
    float yaw[MM_MAX_TRAJECTORY];
    mm_predict_trajectory(db, &s, s.velocity, 0.0f, 0.27f, 0.27f, pos, yaw);
    float *q = calloc((size_t)db->feature_count, sizeof(float));
    float *ref = calloc((size_t)db->feature_count, sizeof(float));
    mm_trajectory_features(db, s.position, s.yaw, pos, yaw, q);
    mm_db_frame_features(db, walk_frame, ref);
    float worst = 0.0f;
    for (int b = 0; b < db->block_count; b++) {
        const mm_block *blk = &db->blocks[b];
        if (blk->type < MM_BLOCK_TRAJECTORY_POSITION) continue;
        for (int i = blk->offset; i < blk->offset + blk->size; i++) {
            /* Compare in feature units (meters, unit vectors). */
            float d = fabsf(q[i] - ref[i]) * db->feature_scale[i];
            if (d > worst) worst = d;
        }
    }
    printf("trajectory: straight walk at %.3f m/s, max feature error vs builder %.4f\n", walk_speed, worst);
    CHECK(worst < 0.02f, "runtime trajectory features differ from the builder by %g", worst);
    free(q);
    free(ref);
}

static void test_ik(void) {
    float worst = 0.0f;
    for (int t = 0; t < 1000; t++) {
        mm_vec3 a = {nrand(), nrand(), nrand()};
        mm_quat ar = {nrand(), nrand(), nrand(), nrand()};
        mm_quat br = {nrand(), nrand(), nrand(), nrand()};
        float la = sqrtf(ar.x * ar.x + ar.y * ar.y + ar.z * ar.z + ar.w * ar.w);
        float lb = sqrtf(br.x * br.x + br.y * br.y + br.z * br.z + br.w * br.w);
        ar.x /= la; ar.y /= la; ar.z /= la; ar.w /= la;
        br.x /= lb; br.y /= lb; br.z /= lb; br.w /= lb;
        mm_quat parent = mm_quat_from_yaw(nrand());
        mm_quat a_world = mm_quat_mul(parent, ar);
        mm_quat b_world = mm_quat_mul(a_world, br);
        mm_vec3 off_b = {0.0f, -0.42f, 0.0f}, off_c = {0.0f, -0.40f, 0.0f};
        mm_vec3 bp = mm_quat_rotate(a_world, off_b);
        bp = mm_vec3_make(bp.x + a.x, bp.y + a.y, bp.z + a.z);
        mm_vec3 cp = mm_quat_rotate(b_world, off_c);
        cp = mm_vec3_make(cp.x + bp.x, cp.y + bp.y, cp.z + bp.z);
        /* A reachable target. */
        mm_vec3 dir = {nrand(), nrand(), nrand()};
        float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        float r = 0.1f + 0.7f * frand();
        mm_vec3 target = {a.x + dir.x / dl * r, a.y + dir.y / dl * r, a.z + dir.z / dl * r};
        mm_quat na, nb;
        mm_two_bone_ik(a, bp, cp, target, mm_vec3_make(0.0f, 0.0f, 1.0f), a_world, b_world, ar, 0.999f, &na, &nb);
        mm_quat wa = mm_quat_mul(parent, na), wb = mm_quat_mul(wa, nb);
        mm_vec3 b2 = mm_quat_rotate(wa, off_b);
        b2 = mm_vec3_make(b2.x + a.x, b2.y + a.y, b2.z + a.z);
        mm_vec3 c2 = mm_quat_rotate(wb, off_c);
        c2 = mm_vec3_make(c2.x + b2.x, c2.y + b2.y, c2.z + b2.z);
        float d = dist3(c2, target);
        if (d > worst) worst = d;
    }
    printf("ik: 1000 random reachable targets, max end error %.2e m\n", worst);
    CHECK(worst < 1e-4f, "two-bone IK misses its target by %g", worst);
}

/* A scripted game capsule: walk, stop, a 90 degree turn, strafe, stop. */
typedef struct {
    mm_vec3 pos, vel;
    float yaw;
} capsule;

static void script(float t, mm_vec3 *desired_vel, float *desired_yaw) {
    if (t < 3.0f) {
        *desired_vel = mm_vec3_make(0.0f, 0.0f, 1.4f);
        *desired_yaw = 0.0f;
    } else if (t < 5.0f) {
        *desired_vel = mm_vec3_make(0.0f, 0.0f, 0.0f);
        *desired_yaw = 0.0f;
    } else if (t < 8.0f) {
        *desired_vel = mm_vec3_make(1.4f, 0.0f, 0.0f);
        *desired_yaw = 1.5707963f;
    } else if (t < 11.0f) {
        /* Facing +X and moving +Z is a strafe to the character's left. */
        *desired_vel = mm_vec3_make(0.0f, 0.0f, 1.1f);
        *desired_yaw = 1.5707963f;
    } else {
        *desired_vel = mm_vec3_make(0.0f, 0.0f, 0.0f);
        *desired_yaw = 1.5707963f;
    }
}

static void capsule_step(capsule *c, mm_vec3 dv, float dyaw, float dt) {
    /* The game moves its capsule with a simple exponential approach. */
    float k = 1.0f - expf(-dt * 6.0f);
    c->vel.x += (dv.x - c->vel.x) * k;
    c->vel.z += (dv.z - c->vel.z) * k;
    c->pos.x += c->vel.x * dt;
    c->pos.z += c->vel.z * dt;
    float d = dyaw - c->yaw;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    c->yaw += d * (1.0f - expf(-dt * 8.0f));
}

typedef struct {
    float max_step;           /* Largest root-space joint step between updates. */
    float max_jump;           /* Largest change of that step between updates. */
    float max_excess;         /* Largest step beyond the joint's natural bound. */
    int excess_joint;
    float max_lock_drift;     /* Largest move of a locked foot. */
    int lock_frames;
    int transitions;
    uint64_t hash;
} run_result;

/* The largest root-space step of every joint between consecutive database
 * frames, halved for 60 Hz updates: how far a joint moves on its own. */
static void natural_steps(const mm_db *db, float *bound) {
    int J = db->joint_count;
    mm_quat *rot = malloc(sizeof(mm_quat) * (size_t)J), *wr = malloc(sizeof(mm_quat) * (size_t)J);
    mm_vec3 *wp = malloc(sizeof(mm_vec3) * (size_t)J), *prev = malloc(sizeof(mm_vec3) * (size_t)J);
    mm_quat id = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int j = 0; j < J; j++) bound[j] = 0.0f;
    for (int f = 0; f < db->frame_count; f++) {
        mm_vec3 hips;
        mm_sample_pose(db, f, 0.0f, rot, &hips);
        mm_forward_kinematics(db, rot, hips, mm_vec3_make(0.0f, 0.0f, 0.0f), id, wp, wr);
        int first = db->clips[db->frame_clip[f]].start == f;
        for (int j = 0; j < J; j++) {
            if (!first) {
                float d = 0.5f * dist3(wp[j], prev[j]);
                if (d > bound[j]) bound[j] = d;
            }
            prev[j] = wp[j];
        }
    }
    free(rot);
    free(wr);
    free(wp);
    free(prev);
}

static float natural_bound[256];
/* When set, run_script also drives a second controller on the same database
 * with other inputs between the updates of the first. */
static int shared_neighbour = 0;

static run_result run_script(const mm_db *db, const mm_config *cfg, int steps, int keep_positions, float *positions) {
    run_result res;
    memset(&res, 0, sizeof(res));
    int J = db->joint_count;
    float dt = 1.0f / 60.0f;
    capsule cap = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0f};
    mm_input in;
    memset(&in, 0, sizeof(in));
    in.filter.required = mm_db_tag_bit(db, "stand");
    mm_controller c;
    int start = 0;
    for (int k = 0; k < db->clip_count; k++)
        if (strcmp(db->clips[k].name, "idle") == 0) start = db->clips[k].start + 20;
    mm_controller_init(&c, db, cfg, start, &in);
    mm_controller other;
    mm_input other_in = in;
    other_in.filter.required = mm_db_tag_bit(db, "crouch");
    if (shared_neighbour) mm_controller_init(&other, db, cfg, db->frame_count / 2, &other_in);
    mm_vec3 *prev = malloc(sizeof(mm_vec3) * (size_t)J);
    mm_vec3 *prev_step = malloc(sizeof(mm_vec3) * (size_t)J);
    mm_vec3 lock_pos[2];
    int locked_prev[2] = {0, 0};
    res.hash = 1469598103934665603ull;
    for (int s = 0; s < steps; s++) {
        float t = (float)s * dt;
        script(t, &in.desired_velocity, &in.desired_yaw);
        capsule_step(&cap, in.desired_velocity, in.desired_yaw, dt);
        in.position = cap.pos;
        in.velocity = cap.vel;
        in.yaw = cap.yaw;
        mm_controller_update(&c, &in, dt);
        if (shared_neighbour) {
            other_in.desired_velocity = mm_vec3_make(0.8f * sinf(t), 0.0f, 0.8f * cosf(0.7f * t));
            other_in.desired_yaw = 2.0f * sinf(0.3f * t);
            other_in.velocity = other_in.desired_velocity;
            other_in.position.x += other_in.velocity.x * dt;
            other_in.position.z += other_in.velocity.z * dt;
            other_in.yaw = other_in.desired_yaw;
            mm_controller_update(&other, &other_in, 0.7f * dt);
        }
        mm_quat inv = mm_quat_from_yaw(-c.root_yaw);
        for (int j = 0; j < J; j++) {
            mm_vec3 d = {c.world_pos[j].x - c.root_pos.x, c.world_pos[j].y - c.root_pos.y,
                         c.world_pos[j].z - c.root_pos.z};
            mm_vec3 local = mm_quat_rotate(inv, d);
            if (s > 0) {
                mm_vec3 step = {local.x - prev[j].x, local.y - prev[j].y, local.z - prev[j].z};
                float len = dist3(step, mm_vec3_make(0.0f, 0.0f, 0.0f));
                if (len > res.max_step) res.max_step = len;
                float excess = len - (1.5f * natural_bound[j] + 0.01f);
                if (excess > res.max_excess) {
                    res.max_excess = excess;
                    res.excess_joint = j;
                }
                if (s > 1) {
                    float jump = dist3(step, prev_step[j]);
                    if (jump > res.max_jump) res.max_jump = jump;
                }
                prev_step[j] = step;
            }
            prev[j] = local;
            if (keep_positions) {
                positions[((size_t)s * J + j) * 3 + 0] = c.world_pos[j].x;
                positions[((size_t)s * J + j) * 3 + 1] = c.world_pos[j].y;
                positions[((size_t)s * J + j) * 3 + 2] = c.world_pos[j].z;
            }
        }
        for (int side = 0; side < 2; side++) {
            int foot = db->roles[side ? MM_ROLE_RIGHT_FOOT : MM_ROLE_LEFT_FOOT];
            if (c.feet[side].locked) {
                if (!locked_prev[side]) lock_pos[side] = c.world_pos[foot];
                float d = dist3(c.world_pos[foot], lock_pos[side]);
                if (d > res.max_lock_drift) res.max_lock_drift = d;
                res.lock_frames++;
            }
            locked_prev[side] = c.feet[side].locked;
        }
        const unsigned char *bytes = (const unsigned char *)c.local_rot;
        for (size_t i = 0; i < sizeof(mm_quat) * (size_t)J; i++) res.hash = (res.hash ^ bytes[i]) * 1099511628211ull;
        bytes = (const unsigned char *)c.world_pos;
        for (size_t i = 0; i < sizeof(mm_vec3) * (size_t)J; i++) res.hash = (res.hash ^ bytes[i]) * 1099511628211ull;
    }
    res.transitions = c.transitions;
    if (shared_neighbour) mm_controller_free(&other);
    free(prev);
    free(prev_step);
    mm_controller_free(&c);
    return res;
}

static void test_playback(const mm_db *db) {
    mm_config cfg;
    mm_config_default(&cfg);
    int steps = 60 * 13;
    natural_steps(db, natural_bound);
    run_result a = run_script(db, &cfg, steps, 0, NULL);
    mm_config raw = cfg;
    raw.inertialization_halflife = 0.0f;
    raw.foot_lock = 0;
    run_result b = run_script(db, &raw, steps, 0, NULL);
    printf("playback: %d updates at 60 Hz, %d transitions\n", steps, a.transitions);
    printf("  max root-space joint step %.3f m per update (%.3f m without inertialization and foot lock)\n",
           a.max_step, b.max_step);
    printf("  max change of that step %.4f m (%.4f m without inertialization and foot lock)\n", a.max_jump,
           b.max_jump);
    printf("  worst step beyond 1.5 x the joint's natural step + 1 cm: %.4f m at %s (%.4f m without)\n",
           a.max_excess, db->joint_names[a.excess_joint], b.max_excess);
    printf("foot lock: %d locked foot-frames, max locked foot drift %.4f m\n", a.lock_frames, a.max_lock_drift);
    CHECK(a.transitions > 3, "expected several transitions, got %d", a.transitions);
    CHECK(a.max_excess <= 0.0f, "pose discontinuity: %s stepped %g m beyond its natural bound",
          db->joint_names[a.excess_joint], a.max_excess);
    CHECK(b.max_excess > 0.1f, "the uninertialized run should show the discontinuities the test is meant to catch");
    CHECK(a.lock_frames > 200, "feet were rarely locked");
    CHECK(a.max_lock_drift < 0.01f, "locked foot moved %g m", a.max_lock_drift);
    run_result c = run_script(db, &cfg, steps, 0, NULL);
    printf("determinism (version %d): hash %016llx vs %016llx\n", db->version, (unsigned long long)a.hash,
           (unsigned long long)c.hash);
    CHECK(a.hash == c.hash, "two identical runs differ");
    shared_neighbour = 1;
    run_result d = run_script(db, &cfg, steps, 0, NULL);
    shared_neighbour = 0;
    printf("sharing: hash %016llx with a second controller playing the same database in between\n",
           (unsigned long long)d.hash);
    CHECK(a.hash == d.hash, "a second controller on the same database changed the first one's output");
}

/* Version 2 against version 1 of the same build, and the fingers-dropped file. */
static void test_versions(const mm_db *v2, const mm_db *v1, const mm_db *nof) {
    CHECK(v2->version == 2 && v1->version == 1 && nof->version == 2, "unexpected versions");
    CHECK(v2->frame_count == v1->frame_count && v2->joint_count == v1->joint_count &&
              nof->frame_count == v1->frame_count, "versions differ in size");
    if (v2->frame_count != v1->frame_count || nof->frame_count != v1->frame_count) return;
    CHECK(v1->rotations && v1->features && !v1->frame_tags && !v2->rotations && !v2->features && v2->frame_tags,
          "version pointers");
    int J = v1->joint_count, F = v1->feature_count;
    mm_quat *r1 = malloc(sizeof(mm_quat) * (size_t)J), *r2 = malloc(sizeof(mm_quat) * (size_t)J);
    mm_quat *r3 = malloc(sizeof(mm_quat) * (size_t)J), *wr1 = malloc(sizeof(mm_quat) * (size_t)J);
    mm_quat *wr2 = malloc(sizeof(mm_quat) * (size_t)J);
    mm_vec3 *wp1 = malloc(sizeof(mm_vec3) * (size_t)J), *wp2 = malloc(sizeof(mm_vec3) * (size_t)J);
    float *f1 = malloc(sizeof(float) * (size_t)F), *f2 = malloc(sizeof(float) * (size_t)F);
    float rot_err = 0.0f, pos_err = 0.0f, feat_err = 0.0f;
    int dropped = 0, dropped_tracks = 0, kept_differ = 0, dropped_not_rest = 0;
    for (int j = 0; j < J; j++) {
        dropped += nof->joint_dropped[j];
        if (nof->joint_dropped[j] && v2->joint_track[j] >= 0) dropped_tracks++;
    }
    for (int f = 0; f < v1->frame_count; f++) {
        mm_vec3 h1, h2, h3;
        float alpha = (f % 3) * 0.37f;
        mm_sample_pose(v1, f, alpha, r1, &h1);
        mm_sample_pose(v2, f, alpha, r2, &h2);
        mm_sample_pose(nof, f, alpha, r3, &h3);
        for (int j = 0; j < J; j++) {
            float e = quat_angle_deg(r1[j], r2[j]);
            if (e > rot_err) rot_err = e;
            if (nof->joint_dropped[j]) {
                if (memcmp(&r3[j], &v1->rest_rot[j], sizeof(mm_quat)) != 0) dropped_not_rest++;
            } else if (memcmp(&r3[j], &r2[j], sizeof(mm_quat)) != 0) {
                kept_differ++;
            }
        }
        mm_quat yaw = mm_quat_from_yaw(v1->root_yaw[f]);
        mm_forward_kinematics(v1, r1, h1, v1->root_pos[f], yaw, wp1, wr1);
        mm_forward_kinematics(v2, r2, h2, v2->root_pos[f], yaw, wp2, wr2);
        for (int j = 0; j < J; j++) {
            float d = dist3(wp1[j], wp2[j]);
            if (d > pos_err) pos_err = d;
        }
        mm_db_frame_features(v1, f, f1);
        mm_db_frame_features(v2, f, f2);
        for (int d = 0; d < F; d++) {
            float e = fabsf(f1[d] - f2[d]);
            if (e > feat_err) feat_err = e;
        }
    }
    printf("version 2 vs 1: max local rotation error %.4f deg, max FK joint error %.2e m, max feature error %.2e "
           "(normalized)\n",
           rot_err, pos_err, feat_err);
    printf("  memory: version 1 %.2f MB, version 2 %.2f MB (%d rotation tracks), fingers dropped %.2f MB (%d "
           "tracks, %d joints dropped); per controller %zu + %zu bytes\n",
           v1->memory_bytes / 1e6, v2->memory_bytes / 1e6, v2->track_count, nof->memory_bytes / 1e6,
           nof->track_count, dropped, mm_controller_memory(v2), sizeof(mm_controller));
    CHECK(rot_err < 0.05f, "quantized rotations off by %g degrees", rot_err);
    CHECK(pos_err < 5e-4f, "quantized FK off by %g m", pos_err);
    CHECK(feat_err < 1e-3f, "quantized features off by %g", feat_err);
    /* 40 finger joints match; the end joints and Thumb1 do not move in the
     * synthetic clips, so 28 of them were tracks. */
    CHECK(dropped == 40, "expected the 40 finger joints dropped, got %d", dropped);
    CHECK(dropped_tracks == 28 && nof->track_count + 28 == v2->track_count, "dropping fingers should remove 28 tracks");
    CHECK(dropped_not_rest == 0, "%d dropped joint samples are not the rest rotation", dropped_not_rest);
    CHECK(kept_differ == 0, "%d kept joint samples differ from the full version 2 file", kept_differ);
    CHECK(v2->memory_bytes * 3 < v1->memory_bytes, "version 2 is not 3x smaller");
    CHECK(mm_frame_tags(v1, 0) == v1->clips[0].tags, "version 1 frame tags are the clip tags");
    free(r1);
    free(r2);
    free(r3);
    free(wr1);
    free(wr2);
    free(wp1);
    free(wp2);
    free(f1);
    free(f2);
}

/* The clip that walks standing and then crouches is found under crouch only
 * in its crouched part, and under stand only before it. */
static void test_frame_tags(const mm_db *db) {
    uint32_t crouch = mm_db_tag_bit(db, "crouch"), stand = mm_db_tag_bit(db, "stand");
    int clip = -1;
    for (int c = 0; c < db->clip_count; c++)
        if (strcmp(db->clips[c].name, "walk_to_crouch") == 0) clip = c;
    CHECK(clip >= 0, "no walk_to_crouch clip");
    if (clip < 0) return;
    const mm_clip *c = &db->clips[clip];
    CHECK((c->tags & (crouch | stand)) == 0, "stance should be a frame tag, not a clip tag");
    int first_crouch = -1, bad = 0;
    for (int i = 0; i < c->length; i++) {
        uint32_t t = mm_frame_tags(db, c->start + i);
        int is_crouch = (t & crouch) != 0;
        CHECK(is_crouch != ((t & stand) != 0), "frame %d must be exactly one of stand and crouch", i);
        if (is_crouch && first_crouch < 0) first_crouch = i;
        if (first_crouch >= 0 && !is_crouch) bad++;
    }
    printf("frame tags: walk_to_crouch (%d frames) is stand for frames 0-%d and crouch from %d\n", c->length,
           first_crouch - 1, first_crouch);
    /* The crouch starts at 2.5 s and is complete at 3.5 s (frames 75 to 105). */
    CHECK(first_crouch >= 75 && first_crouch <= 105, "crouch starts at frame %d", first_crouch);
    CHECK(bad == 0, "%d stand frames after the crouch began", bad);
    /* Searches for this clip's own poses under each stance filter. */
    int F = db->feature_count, wrong = 0, inside = 0;
    float *q = malloc(sizeof(float) * (size_t)F);
    for (int i = 0; i < c->length; i += 2) {
        mm_db_frame_features(db, c->start + i, q);
        for (int k = 0; k < 2; k++) {
            mm_filter filter = {k ? crouch : stand, 0};
            float cost;
            int best = mm_search(db, q, filter, INFINITY, &cost);
            float cost_ref;
            int ref = mm_search_brute_force(db, q, filter, INFINITY, &cost_ref);
            if (best != ref || cost != cost_ref) wrong++;
            if (best >= c->start && best < c->start + c->length) {
                inside++;
                int off = best - c->start;
                if (k ? off < first_crouch : off >= first_crouch) wrong++;
            }
        }
    }
    printf("  %d searches returned a frame of this clip, %d on the wrong side of the stance change\n", inside,
           wrong);
    CHECK(inside > 50, "the clip's own frames should often be the best match");
    CHECK(wrong == 0, "frame-tag search returned a frame of the wrong stance");
    free(q);
}

static void test_load_memory(const char *path, const mm_db *db) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "cannot open %s", path);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)n);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    CHECK(got == (size_t)n, "short read");
    mm_db m;
    int err = mm_db_load_memory(&m, buf, (size_t)n);
    CHECK(err == 0, "mm_db_load_memory failed (%d)", err);
    buf[n / 2] ^= 0xff; /* The database is a copy. */
    if (!err) {
        CHECK(m.memory_bytes == db->memory_bytes && m.frame_count == db->frame_count, "memory load differs");
        mm_quat a = mm_db_rotation(&m, m.frame_count / 3, m.hips), b = mm_db_rotation(db, m.frame_count / 3, m.hips);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0, "memory load decodes differently");
        mm_db_free(&m);
    }
    /* Truncated and corrupted files are rejected. */
    buf[n / 2] ^= 0xff;
    CHECK(mm_db_load_memory(&m, buf, (size_t)n - 4) != 0, "truncated file accepted");
    memcpy(buf + 16, "\xff\xff\xff\x7f", 4);
    CHECK(mm_db_load_memory(&m, buf, (size_t)n) != 0, "absurd frame count accepted");
    printf("load from memory: ok\n");
    free(buf);
}

static int load(mm_db *db, const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int err = mm_db_load(db, path);
    if (err) {
        printf("cannot load %s (%d)\n", path, err);
        return 0;
    }
    printf("loaded %s: version %d, %d joints, %d frames, %d clips, %d features, %d tags, %.2f MB\n", path,
           db->version, db->joint_count, db->frame_count, db->clip_count, db->feature_count, db->tag_count,
           db->memory_bytes / 1e6);
    return 1;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "build";
    mm_db db, v1, nof;
    if (!load(&db, dir, "test.mmdb") || !load(&v1, dir, "test_v1.mmdb") || !load(&nof, dir, "test_nofingers.mmdb"))
        return 1;
    test_fk(&db, dir, 5e-4f);
    test_fk(&v1, dir, 1e-4f);
    test_versions(&db, &v1, &nof);
    test_search(&db);
    test_search(&v1);
    test_search(&nof);
    test_frame_tags(&db);
    test_trajectory_features(&db);
    test_ik();
    test_playback(&db);
    test_playback(&v1);
    char path[512];
    snprintf(path, sizeof(path), "%s/test.mmdb", dir);
    test_load_memory(path, &db);
    mm_db_free(&db);
    mm_db_free(&v1);
    mm_db_free(&nof);
    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("all runtime tests passed\n");
    return 0;
}
