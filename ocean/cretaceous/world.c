#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <math.h>

#define MAX_WORLDS 8
static CretWorld* g_worlds[MAX_WORLDS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void progress(const char* stage, float f, void* u) {
    if (*(int*)u) fprintf(stderr, "cretaceous: baking world: %-18s %3.0f%%\n", stage, f * 100);
}

// ---------------------------------------------------------------- prop colliders
static bool collides(int kind) {
    return kind == PROP_BOULDER || kind == PROP_OUTCROP || kind == PROP_LOG || kind == PROP_STUMP || kind == PROP_ROOTS ||
           kind == PROP_DRIFTWOOD || kind == PROP_COAST || kind == PROP_CLIFF;
}

// Coarsest LOD of a .prop file: positions (Y up, base at 0) and indices.
static int read_lod2(const char* path, float** pos, int32_t** idx, int* ni) {
    FILE* f = fopen(path, "rb"); if (!f) return 0;
    uint32_t hdr[2]; char mat[32]; float rh[2]; uint32_t nlod; int nv = 0;
    bool ok = fread(hdr, 4, 2, f) == 2 && fread(mat, 32, 1, f) == 1 && fread(rh, 4, 2, f) == 2 && fread(&nlod, 4, 1, f) == 1 && nlod == 3;
    for (uint32_t l = 0; ok && l < nlod; l++) {
        uint32_t v, n; ok = fread(&v, 4, 1, f) && fread(&n, 4, 1, f) && v > 0 && v < (1u << 22) && n < (1u << 24) && n % 3 == 0;
        if (!ok) break;
        if (l < 2) { ok = fseek(f, (long)v * 28 + (long)n * 4, SEEK_CUR) == 0; continue; }
        uint8_t* vb = malloc((size_t)v * 28); *pos = malloc(sizeof(float) * 3 * v); *idx = malloc(4 * (size_t)n);
        ok = fread(vb, 28, v, f) == v && fread(*idx, 4, n, f) == n;
        for (uint32_t i = 0; ok && i < v; i++) memcpy(*pos + 3 * i, vb + 28 * i, 12);
        for (uint32_t i = 0; ok && i < n; i++) if ((uint32_t)(*idx)[i] >= v) ok = false;
        free(vb); nv = (int)v; *ni = (int)n;
        if (!ok) { free(*pos); free(*idx); *pos = NULL; *idx = NULL; }
    }
    fclose(f);
    return ok ? nv : 0;
}

static void load_prop_colliders(CretWorld* w, int verbose) {
    if (getenv("CRET_NO_PROP_COLLIDERS") || !prop_catalog_load(&w->prop_cat, PROP_DIR)) return;
    const PropCatalog* c = &w->prop_cat;
    for (int v = 0; v < c->nvar; v++) {
        if (c->kind[v] < 0) continue;
        bool any = false; for (int k = 0; k < PROP_KINDS; k++) for (int q = 0; q < c->kind_count[k]; q++) if (c->kind_list[k][q] == v && collides(k)) any = true;
        if (!any) continue;
        char path[512]; snprintf(path, sizeof path, PROP_DIR "%s.prop", c->name[v]);
        float* pos = NULL; int32_t* idx = NULL; int ni = 0, nv = read_lod2(path, &pos, &idx, &ni);
        if (nv < 4) continue;
        if (c->kind[v] == PROP_COAST || c->kind[v] == PROP_CLIFF) {
            b3MeshDef md = {0}; md.vertices = (b3Vec3*)pos; md.indices = idx; md.vertexCount = nv; md.triangleCount = ni / 3;
            md.weldTolerance = 0.01f;
            w->prop_mesh[v] = b3CreateMesh(&md, NULL, 0);
        } else {
            w->prop_hull[v] = b3CreateHull((const b3Vec3*)pos, nv, 32);
        }
        free(pos); free(idx);
    }
    // Scatter every tile once (same calls as the renderer) and keep the blockers.
    int T = w->prop_tiles = (int)ceilf(w->size / PROP_TILE);
    w->prop_start = calloc((size_t)T * T + 1, sizeof(int));
    int cap = 4096; w->props = malloc(sizeof(CretPropCollider) * cap);
    PropInstance* buf = malloc(sizeof(PropInstance) * 4096);
    for (int t = 0; t < T * T; t++) {
        w->prop_start[t] = w->nprops;
        int tx = t % T, tz = t / T;
        for (int k = 0; k < PROP_KINDS; k++) {
            if (!collides(k) || PROP_CLASS[k].near) continue;
            int n = prop_scatter(c, &w->map, k, tx, tz, buf, 4096);
            for (int i = 0; i < n; i++) {
                const PropInstance* in = &buf[i]; int v = in->variant;
                if (!w->prop_hull[v] && !w->prop_mesh[v]) continue;
                if (k == PROP_BOULDER && c->radius[v] * in->scale < 0.55f) continue;   // small boulders: step around visually only
                if (w->nprops == cap) { cap *= 2; w->props = realloc(w->props, sizeof(CretPropCollider) * cap); }
                w->props[w->nprops++] = (CretPropCollider){in->x, in->y, in->z, in->scale, {in->q[0], in->q[1], in->q[2], in->q[3]}, (uint16_t)v, (uint16_t)k};
            }
        }
    }
    w->prop_start[T * T] = w->nprops;
    free(buf);
    if (verbose) fprintf(stderr, "cretaceous: %d prop colliders (%d variants)\n", w->nprops, c->nvar);
}

void cret_prop_stream_init(CretPropStream* ps, const CretWorld* w) {
    memset(ps, 0, sizeof *ps);
    ps->tiles = w->prop_tiles;
    if (ps->tiles) ps->slot = calloc((size_t)ps->tiles * ps->tiles, sizeof(int32_t));
}

static void load_tile(CretPropStream* ps, const CretWorld* w, b3WorldId phys, int t, int tick) {
    if (ps->ns == ps->cap) { ps->cap = ps->cap ? ps->cap * 2 : 64; ps->s = realloc(ps->s, sizeof(CretPropSlot) * ps->cap); }
    CretPropSlot* s = &ps->s[ps->ns]; int a = w->prop_start[t], b = w->prop_start[t + 1];
    s->tile = t; s->last = tick; s->n = 0; s->bodies = b > a ? malloc(sizeof(b3BodyId) * (b - a)) : NULL;
    for (int i = a; i < b; i++) {
        const CretPropCollider* pc = &w->props[i];
        b3BodyDef bd = b3DefaultBodyDef(); bd.type = b3_staticBody;
        bd.position = (b3Pos){pc->x, pc->y, pc->z};
        bd.rotation = (b3Quat){{pc->q[0], pc->q[1], pc->q[2]}, pc->q[3]};
        b3BodyId body = b3CreateBody(phys, &bd);
        b3ShapeDef sd = b3DefaultShapeDef(); sd.filter.categoryBits = CRET_CAT_PROP; sd.filter.maskBits = CRET_CAT_AGENT;
        b3Vec3 sc = {pc->scale, pc->scale, pc->scale};
        if (w->prop_hull[pc->variant]) b3CreateTransformedHullShape(body, &sd, w->prop_hull[pc->variant], b3Transform_identity, sc);
        else b3CreateMeshShape(body, &sd, w->prop_mesh[pc->variant], sc);
        s->bodies[s->n++] = body;
    }
    ps->slot[t] = ++ps->ns;
}

void cret_prop_stream_touch(CretPropStream* ps, const CretWorld* w, b3WorldId phys, float x, float z, float reach, int tick) {
    if (!ps->slot || !w->nprops) return;
    int T = ps->tiles;
    int x0 = (int)floorf((x - reach) / PROP_TILE), x1 = (int)floorf((x + reach) / PROP_TILE);
    int z0 = (int)floorf((z - reach) / PROP_TILE), z1 = (int)floorf((z + reach) / PROP_TILE);
    for (int tz = z0 < 0 ? 0 : z0; tz <= z1 && tz < T; tz++) for (int tx = x0 < 0 ? 0 : x0; tx <= x1 && tx < T; tx++) {
        int t = tz * T + tx;
        if (ps->slot[t]) ps->s[ps->slot[t] - 1].last = tick;
        else load_tile(ps, w, phys, t, tick);
    }
}

void cret_prop_stream_trim(CretPropStream* ps, int tick, int max_age) {
    for (int i = 0; i < ps->ns;) {
        CretPropSlot* s = &ps->s[i];
        if (tick - s->last <= max_age && tick >= s->last) { i++; continue; }
        for (int k = 0; k < s->n; k++) b3DestroyBody(s->bodies[k]);
        free(s->bodies); ps->slot[s->tile] = 0;
        if (i != ps->ns - 1) { ps->s[i] = ps->s[ps->ns - 1]; ps->slot[ps->s[i].tile] = i + 1; }
        ps->ns--;
    }
}

void cret_prop_stream_free(CretPropStream* ps) {
    for (int i = 0; i < ps->ns; i++) free(ps->s[i].bodies);
    free(ps->s); free(ps->slot); memset(ps, 0, sizeof *ps);
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
    load_prop_colliders(w, verbose);
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
        b3DestroyHeightField(w->field); terra_free(&w->map);
        for (int v = 0; v < PROP_MAX_VARIANTS; v++) { if (w->prop_hull[v]) b3DestroyHull(w->prop_hull[v]); if (w->prop_mesh[v]) b3DestroyMesh(w->prop_mesh[v]); }
        free(w->props); free(w->prop_start); free(w);
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
