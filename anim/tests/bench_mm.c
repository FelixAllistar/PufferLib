/*
 * Search benchmark. Usage: bench_mm DATABASE.mmdb
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

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: bench_mm DATABASE.mmdb\n");
        return 1;
    }
    mm_db db;
    int err = mm_db_load(&db, argv[1]);
    if (err) {
        printf("cannot load %s (%d)\n", argv[1], err);
        return 1;
    }
    int F = db.feature_count, Q = 500;
    printf("database: %d frames x %d features, %d clips\n", db.frame_count, F, db.clip_count);
    float *queries = malloc(sizeof(float) * (size_t)Q * F);
    for (int q = 0; q < Q; q++) {
        int pose = (int)(frand() * (float)db.frame_count);
        int traj = (int)(frand() * (float)db.frame_count);
        for (int b = 0; b < db.block_count; b++) {
            const mm_block *blk = &db.blocks[b];
            int src = blk->type >= MM_BLOCK_TRAJECTORY_POSITION ? traj : pose;
            for (int i = blk->offset; i < blk->offset + blk->size; i++)
                queries[(size_t)q * F + i] = db.features[(size_t)src * F + i] + 0.1f * (frand() - 0.5f);
        }
    }
    uint32_t stand = mm_db_tag_bit(&db, "stand"), mirrored = mm_db_tag_bit(&db, "mirrored");
    mm_filter filters[2] = {{0, 0}, {stand, mirrored}};
    const char *filter_names[2] = {"no filter", "stand, not mirrored"};
    double *times = malloc(sizeof(double) * (size_t)Q);
    for (int fi = 0; fi < 2; fi++) {
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
    mm_db_free(&db);
    return 0;
}
