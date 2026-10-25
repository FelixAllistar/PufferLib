#pragma once
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char key[128], distribution[64];
    float min, max;
} PufSweepHistoryParam;

typedef void (*PufSweepHistoryReplay)(void*, const float*, float, float, int);

static void puf_sweep_history_error(const char* path, const char* reason) {
    fprintf(stderr, "Invalid sweep history %s: %s\n", path, reason);
    exit(1);
}

// A locked, append-only record of completed trials. Parameters are stored in
// their actual units so history remains readable independently of Protein.
// A history written for different search parameters is moved aside (path.old-<time>) and a fresh one
// started, so changing the search never blocks a sweep. Missing parent directories are created.
static void puf_sweep_history_move_aside(FILE* fp, const char* path, const char* reason) {
    char aside[1100];
    snprintf(aside, sizeof(aside), "%s.old-%ld", path, (long)time(NULL));
    fclose(fp);
    if (rename(path, aside)) { perror(aside); exit(1); }
    fprintf(stderr, "Sweep history %s: %s; moved it to %s and starting a fresh history\n", path, reason, aside);
}

static FILE* puf_sweep_history_open(const char* path, PufSweepHistoryParam* defs,
        int dim, PufSweepHistoryReplay replay, void* ctx, int* completed, int* next_run) {
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s", path);
    for (char* c = parent + 1; *c; c++) {
        if (*c != '/') continue;
        *c = 0;
        if (mkdir(parent, 0777) && errno != EEXIST) { perror(parent); exit(1); }
        *c = '/';
    }
    FILE* fp = fopen(path, "a+");
    if (!fp) { perror(path); exit(1); }
    if (flock(fileno(fp), LOCK_EX | LOCK_NB))
        puf_sweep_history_error(path, "another sweep owns this history");
    if (fcntl(fileno(fp), F_SETFD, FD_CLOEXEC)) { perror("fcntl"); exit(1); }
    rewind(fp);
    int c = fgetc(fp);
    rewind(fp);
    if (c == EOF) {
        fprintf(fp, "PUFFER_SWEEP_HISTORY_V1 %d\n", dim);
        for (int i = 0; i < dim; i++)
            fprintf(fp, "%s %.9g %.9g %s\n", defs[i].key, defs[i].min,
                defs[i].max, defs[i].distribution);
        if (fflush(fp) || fsync(fileno(fp))) { perror(path); exit(1); }
        return fp;
    }
    char magic[64]; int count;
    if (fscanf(fp, "%63s %d", magic, &count) != 2 ||
            strcmp(magic, "PUFFER_SWEEP_HISTORY_V1") || count != dim) {
        puf_sweep_history_move_aside(fp, path, "different format or parameter count");
        return puf_sweep_history_open(path, defs, dim, replay, ctx, completed, next_run);
    }
    for (int i = 0; i < dim; i++) {
        char key[128], distribution[64]; float low, high;
        if (fscanf(fp, "%127s %f %f %63s", key, &low, &high, distribution) != 4 ||
                strcmp(key, defs[i].key) || strcmp(distribution, defs[i].distribution) ||
                low != defs[i].min || high != defs[i].max) {
            puf_sweep_history_move_aside(fp, path, "search parameter names/ranges changed");
            return puf_sweep_history_open(path, defs, dim, replay, ctx, completed, next_run);
        }
    }
    float* values = (float*)calloc(dim, sizeof(float));
    int previous = -1;
    while (1) {
        int run, failed; float score, cost;
        int got = fscanf(fp, "%d", &run);
        if (got == EOF) break;
        if (got != 1 || run <= previous || run < 0 ||
                fscanf(fp, "%f %f %d", &score, &cost, &failed) != 3 ||
                (failed != 0 && failed != 1) || (!failed && !isfinite(score)) ||
                !isfinite(cost) || cost <= 0)
            puf_sweep_history_error(path, "invalid or truncated trial record");
        for (int i = 0; i < dim; i++) {
            float tolerance = 8 * FLT_EPSILON * fmaxf(fabsf(defs[i].min), fabsf(defs[i].max));
            if (fscanf(fp, "%f", &values[i]) != 1 || !isfinite(values[i]) ||
                    values[i] < defs[i].min - tolerance || values[i] > defs[i].max + tolerance)
                puf_sweep_history_error(path, "invalid or truncated parameter vector");
            values[i] = fminf(defs[i].max, fmaxf(defs[i].min, values[i]));
        }
        replay(ctx, values, score, cost, failed);
        *completed += !failed;
        *next_run = run + 1;
        previous = run;
    }
    if (ferror(fp)) { perror(path); exit(1); }
    free(values);
    if (fseek(fp, 0, SEEK_END)) { perror(path); exit(1); }
    return fp;
}

static void puf_sweep_history_append(FILE* fp, int run, const float* values,
        int dim, float score, float cost, int failed) {
    // Leading newline also separates a complete last record lacking a newline.
    fprintf(fp, "\n%d %.9g %.9g %d", run, score, cost, failed);
    for (int i = 0; i < dim; i++) fprintf(fp, " %.9g", values[i]);
    fputc('\n', fp);
    if (fflush(fp) || fsync(fileno(fp))) { perror("sweep history"); exit(1); }
}
