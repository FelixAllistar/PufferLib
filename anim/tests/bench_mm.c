/*
 * Search benchmark. Usage: bench_mm DATABASE.mmdb [VERSION1.mmdb [NOFINGERS.mmdb]]
 *
 * Times loading every given database, compares the version 2 database's
 * poses with the version 1 file of the same build (FK over every frame), and
 * runs the search and controller timings on each.
 *
 * Times mm_search against mm_search_brute_force on queries built like the
 * controller builds them: the pose features of a random frame, with the
 * trajectory blocks taken from another random frame (a change of intent) and
 * a little noise. The best cost starts at infinity, which is the slowest case
 * (the controller starts from the cost of continuing). It also times full
 * controller updates on a meandering path, and the searches on the queries
 * those updates issue.
 */
#define _POSIX_C_SOURCE 199309L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../mm.h"

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static float frand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (float)((rng_state >> 11) * (1.0 / 9007199254740992.0));
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static float quat_angle_deg(mm_quat a, mm_quat b) {
    double ax = a.x, ay = a.y, az = a.z, aw = a.w, bx = -b.x, by = -b.y, bz = -b.z, bw = b.w;
    double x = aw * bx + ax * bw + ay * bz - az * by;
    double y = aw * by - ax * bz + ay * bw + az * bx;
    double z = aw * bz + ax * by - ay * bx + az * bw;
    double w = aw * bw - ax * bx - ay * by - az * bz;
    return (float)(2.0 * atan2(sqrt(x * x + y * y + z * z), fabs(w)) * 57.29577951308232);
}

static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

/* Loads path a few times and reports the fastest and mean load. */
static int timed_load(mm_db *db, const char *path) {
    double best = 1e9, total = 0.0;
    int reps = 5;
    for (int r = 0; r < reps; r++) {
        double t0 = now();
        int err = mm_db_load(db, path);
        double t = now() - t0;
        if (err) {
            printf("cannot load %s (%d)\n", path, err);
            return 0;
        }
        total += t;
        if (t < best) best = t;
        if (r + 1 < reps) mm_db_free(db);
    }
    printf("%s: version %d, file %.1f MB, in memory %.1f MB, %d rotation tracks; load %.0f ms (best of %d, mean "
           "%.0f ms, file in the page cache)\n",
           path, db->version, file_size(path) / 1e6, db->memory_bytes / 1e6, db->track_count, 1e3 * best, reps,
           1e3 * total / reps);
    return 1;
}

/* Max local rotation and FK joint position error of b against a over every frame. */
static void compare(const mm_db *a, const mm_db *b, float *rot_err, float *pos_err) {
    int J = a->joint_count;
    mm_quat *ra = malloc(sizeof(mm_quat) * (size_t)J), *rb = malloc(sizeof(mm_quat) * (size_t)J);
    mm_quat *wa = malloc(sizeof(mm_quat) * (size_t)J), *wb = malloc(sizeof(mm_quat) * (size_t)J);
    mm_vec3 *pa = malloc(sizeof(mm_vec3) * (size_t)J), *pb = malloc(sizeof(mm_vec3) * (size_t)J);
    *rot_err = 0.0f;
    *pos_err = 0.0f;
    for (int f = 0; f < a->frame_count; f++) {
        mm_vec3 ha, hb;
        mm_sample_pose(a, f, 0.0f, ra, &ha);
        mm_sample_pose(b, f, 0.0f, rb, &hb);
        mm_quat yaw = mm_quat_from_yaw(a->root_yaw[f]);
        mm_forward_kinematics(a, ra, ha, a->root_pos[f], yaw, pa, wa);
        mm_forward_kinematics(b, rb, hb, b->root_pos[f], yaw, pb, wb);
        for (int j = 0; j < J; j++) {
            float e = quat_angle_deg(ra[j], rb[j]);
            if (e > *rot_err) *rot_err = e;
            float dx = pa[j].x - pb[j].x, dy = pa[j].y - pb[j].y, dz = pa[j].z - pb[j].z;
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d > *pos_err) *pos_err = d;
        }
    }
    free(ra);
    free(rb);
    free(wa);
    free(wb);
    free(pa);
    free(pb);
}

static int bench(const mm_db *db_ptr) {
    const mm_db db = *db_ptr;
    int F = db.feature_count, Q = 500, total_mismatches = 0;
    printf("database version %d: %d frames x %d features, %d clips\n", db.version, db.frame_count, F, db.clip_count);
    float *queries = malloc(sizeof(float) * (size_t)Q * F);
    float *row = malloc(sizeof(float) * (size_t)F);
    for (int q = 0; q < Q; q++) {
        int pose = (int)(frand() * (float)db.frame_count);
        int traj = (int)(frand() * (float)db.frame_count);
        for (int b = 0; b < db.block_count; b++) {
            const mm_block *blk = &db.blocks[b];
            mm_db_frame_features(&db, blk->type >= MM_BLOCK_TRAJECTORY_POSITION ? traj : pose, row);
            for (int i = blk->offset; i < blk->offset + blk->size; i++)
                queries[(size_t)q * F + i] = row[i] + 0.1f * (frand() - 0.5f);
        }
    }
    uint32_t stand = mm_db_tag_bit(&db, "stand"), mirrored = mm_db_tag_bit(&db, "mirrored");
    uint32_t crouch = mm_db_tag_bit(&db, "crouch");
    mm_filter filters[3] = {{0, 0}, {stand, mirrored}, {crouch, 0}};
    const char *filter_names[3] = {"no filter", "stand, not mirrored", "crouch"};
    double *times = malloc(sizeof(double) * (size_t)Q);
    for (int fi = 0; fi < 3; fi++) {
        int mismatches = 0;
        double total = 0.0, brute_total = 0.0;
        for (int q = 0; q < Q; q++) {
            const float *query = queries + (size_t)q * F;
            float c0, c1;
            double t0 = now();
            int a = mm_search(&db, query, filters[fi], INFINITY, &c0);
            double t1 = now();
            int b = mm_search_brute_force(&db, query, filters[fi], INFINITY, &c1);
            double t2 = now();
            if (a != b || c0 != c1) mismatches++;
            times[q] = t1 - t0;
            total += t1 - t0;
            brute_total += t2 - t1;
        }
        qsort(times, (size_t)Q, sizeof(double), cmp_double);
        printf("search (%s): mean %.3f ms, median %.3f ms, p99 %.3f ms, max %.3f ms; brute force mean %.3f ms; "
               "%d mismatches\n",
               filter_names[fi], 1e3 * total / Q, 1e3 * times[Q / 2], 1e3 * times[Q * 99 / 100], 1e3 * times[Q - 1],
               1e3 * brute_total / Q, mismatches);
        total_mismatches += mismatches;
    }

    /* Controller updates at 60 Hz on a meandering path. */
    mm_config cfg;
    mm_config_default(&cfg);
    mm_controller c;
    mm_input in;
    memset(&in, 0, sizeof(in));
    in.filter.required = stand;
    mm_controller_init(&c, &db, &cfg, 0, &in);
    int steps = 3600;
    double t0 = now(), worst = 0.0;
    double search_inf = 0.0, search_bound = 0.0;
    int searched = 0, prev_searches = 0;
    for (int s = 0; s < steps; s++) {
        float t = (float)s / 60.0f;
        float heading = 1.5f * sinf(0.21f * t) + 0.8f * sinf(0.53f * t);
        float speed = 1.4f + 1.2f * sinf(0.17f * t);
        in.desired_velocity = mm_vec3_make(speed * sinf(heading), 0.0f, speed * cosf(heading));
        in.desired_yaw = heading;
        in.velocity = in.desired_velocity;
        in.position = mm_vec3_make(in.position.x + in.velocity.x / 60.0f, 0.0f, in.position.z + in.velocity.z / 60.0f);
        in.yaw = heading;
        double a = now();
        mm_controller_update(&c, &in, 1.0f / 60.0f);
        double b = now() - a;
        if (b > worst) worst = b;
        if (c.searches != prev_searches) {
            /* Time the query this update searched with, without and with the
             * controller's initial bound (the cost of continuing). */
            prev_searches = c.searches;
            mm_controller_query(&c, &in);
            float cost;
            double s0 = now();
            mm_search(&db, c.query, in.filter, INFINITY, &cost);
            double s1 = now();
            float limit = mm_frame_cost(&db, c.query, c.frame) - cfg.transition_margin;
            mm_search(&db, c.query, in.filter, limit, &cost);
            double s2 = now();
            search_inf += s1 - s0;
            search_bound += s2 - s1;
            searched++;
        }
    }
    double total = now() - t0 - search_inf - search_bound;
    printf("controller: %d updates, mean %.3f ms, max %.3f ms, %d searches, %d transitions\n", steps,
           1e3 * total / steps, 1e3 * worst, c.searches, c.transitions);
    printf("controller queries (stand filter): mean search %.3f ms from scratch, %.3f ms with the continuing-cost "
           "bound\n",
           1e3 * search_inf / searched, 1e3 * search_bound / searched);
    mm_controller_free(&c);
    free(times);
    free(queries);
    free(row);
    return total_mismatches;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: bench_mm DATABASE.mmdb [VERSION1.mmdb [NOFINGERS.mmdb]]\n");
        return 1;
    }
    mm_db db[3];
    int n = argc - 1 < 3 ? argc - 1 : 3;
    for (int i = 0; i < n; i++)
        if (!timed_load(&db[i], argv[i + 1])) return 1;
    int bad = 0;
    if (n >= 2) {
        float rot_err, pos_err;
        compare(&db[1], &db[0], &rot_err, &pos_err);
        printf("%s vs %s: max local rotation error %.4f deg, max FK joint error %.3f mm over all %d frames\n",
               argv[1], argv[2], rot_err, 1e3 * pos_err, db[0].frame_count);
        if (rot_err >= 0.05f) bad++;
        printf("size: version 2 is %.2fx smaller than version 1 on disk", (double)file_size(argv[2]) / file_size(argv[1]));
        if (n >= 3) printf(", %.2fx with the fingers dropped", (double)file_size(argv[2]) / file_size(argv[3]));
        printf("\n");
    }
    printf("per controller: %zu bytes allocated + sizeof(mm_controller) %zu bytes\n", mm_controller_memory(&db[0]),
           sizeof(mm_controller));
    for (int i = 0; i < n && i < 2; i++) bad += bench(&db[i]);
    for (int i = 0; i < n; i++) mm_db_free(&db[i]);
    if (bad) printf("FAILED: %d mismatches or errors\n", bad);
    return bad ? 1 : 0;
}
