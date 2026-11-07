#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>

#define MAX_WORLDS 8
static CretWorld* g_worlds[MAX_WORLDS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void progress(const char* stage, float f, void* u) {
    if (*(int*)u) fprintf(stderr, "cretaceous: baking world: %-18s %3.0f%%\n", stage, f * 100);
}

static CretWorld* load_or_bake(uint32_t seed, int n, float cell, int verbose) {
    CretWorld* w = calloc(1, sizeof(*w));
    w->params = terra_default_params(seed); w->params.n = n; w->params.cell = cell;
    // Developer/mod override, e.g. CRET_TERRA="k=6e-5 diffusion=0.006".
    if (terra_params_apply(&w->params, getenv("CRET_TERRA")) < 0) fprintf(stderr, "cretaceous: bad CRET_TERRA spec ignored\n");
    w->params.n = n; w->params.cell = cell;
    char path[512];
    mkdir("build", 0755); mkdir("build/cretaceous", 0755); mkdir("build/cretaceous/worlds", 0755);
    snprintf(path, sizeof path, "build/cretaceous/worlds/s%u_n%d_%016llx.terra", seed, n,
             (unsigned long long)terra_params_hash(&w->params));
    if (terra_load(&w->map, &w->params, path) != 0) {
        if (verbose) fprintf(stderr, "cretaceous: no cached world at %s; baking (one time)\n", path);
        if (terra_generate(&w->params, &w->map, progress, &verbose) != 0) { free(w); return NULL; }
        if (terra_save(&w->map, &w->params, path) != 0) fprintf(stderr, "cretaceous: could not cache %s\n", path);
    }
    w->size = terra_size(&w->map);
    // Collision heightfield. Box3D grids run along x (columns) and z (rows).
    b3HeightFieldDef def = {0};
    def.heights = w->map.height; def.countX = n; def.countZ = n;
    def.scale = (b3Vec3){cell, 1.0f, cell};
    float lo = 1e9f, hi = -1e9f;
    for (size_t i = 0; i < (size_t)n * n; i++) { if (w->map.height[i] < lo) lo = w->map.height[i]; if (w->map.height[i] > hi) hi = w->map.height[i]; }
    def.globalMinimumHeight = lo - 1; def.globalMaximumHeight = hi + 1;
    uint8_t* mats = calloc((size_t)(n - 1) * (n - 1), 1); def.materialIndices = mats;
    w->field = b3CreateHeightField(&def);
    free(mats);
    return w;
}

CretWorld* cret_world_acquire(uint32_t seed, int n, float cell, int verbose) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_WORLDS; i++) {
        CretWorld* w = g_worlds[i];
        if (w && w->params.seed == seed && w->params.n == n && w->params.cell == cell) { w->refs++; pthread_mutex_unlock(&g_lock); return w; }
    }
    CretWorld* w = load_or_bake(seed, n, cell, verbose);
    if (w) {
        w->refs = 1;
        for (int i = 0; i < MAX_WORLDS; i++) if (!g_worlds[i]) { g_worlds[i] = w; break; }
    }
    pthread_mutex_unlock(&g_lock);
    return w;
}

void cret_world_release(CretWorld* w) {
    if (!w) return;
    pthread_mutex_lock(&g_lock);
    if (--w->refs == 0) {
        for (int i = 0; i < MAX_WORLDS; i++) if (g_worlds[i] == w) g_worlds[i] = NULL;
        b3DestroyHeightField(w->field); terra_free(&w->map); free(w);
    }
    pthread_mutex_unlock(&g_lock);
}

b3BodyId cret_world_add_ground(const CretWorld* w, b3WorldId world) {
    b3BodyDef bd = b3DefaultBodyDef();
    b3BodyId body = b3CreateBody(world, &bd);
    b3ShapeDef sd = b3DefaultShapeDef();
    b3CreateHeightFieldShape(body, &sd, w->field);
    return body;
}
