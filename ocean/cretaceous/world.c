#include "world.h"
#include "render/veg_place.h"   // tree scatter for the trunk colliders (no GL)
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
    return kind == PROP_BOULDER || kind == PROP_OUTCROP || kind == PROP_STONE || kind == PROP_LOG || kind == PROP_STUMP || kind == PROP_ROOTS ||
           kind == PROP_DRIFTWOOD || kind == PROP_COAST || kind == PROP_CLIFF;
}
static bool concave(int kind) { return kind == PROP_ROOTS || kind == PROP_COAST || kind == PROP_CLIFF; }
static bool wooden(int kind) { return kind == PROP_LOG || kind == PROP_DRIFTWOOD; }

// A log as slabs along its longest horizontal axis: each slab hulls the
// vertices within its span (plus a small overlap), so a bent trunk is not
// one fat hull that rolls like a barrel.
static int log_parts(const float* pos, int nv, b3HullData** out) {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < nv; i++) for (int k = 0; k < 3; k++) { lo[k] = fminf(lo[k], pos[3 * i + k]); hi[k] = fmaxf(hi[k], pos[3 * i + k]); }
    int ax = hi[0] - lo[0] >= hi[2] - lo[2] ? 0 : 2; float len = hi[ax] - lo[ax], step = len / CRET_PROP_PARTS, pad = 0.04f * len;
    b3Vec3* pts = malloc(sizeof(b3Vec3) * nv); int n = 0;
    for (int p = 0; p < CRET_PROP_PARTS; p++) {
        float a = lo[ax] + p * step - pad, b = lo[ax] + (p + 1) * step + pad; int m = 0;
        for (int i = 0; i < nv; i++) if (pos[3 * i + ax] >= a && pos[3 * i + ax] <= b) pts[m++] = (b3Vec3){pos[3 * i], pos[3 * i + 1], pos[3 * i + 2]};
        if (m < 8) continue;
        b3HullData* h = b3CreateHull(pts, m, 32);
        if (h) out[n++] = h;
    }
    free(pts);
    return n;
}

static void load_prop_colliders(CretWorld* w, int verbose) {
    if (getenv("CRET_NO_PROP_COLLIDERS") || !prop_catalog_load(&w->prop_cat, PROP_DIR)) return;
    const PropCatalog* c = &w->prop_cat;
    for (int v = 0; v < c->nvar; v++) {
        if (c->kind[v] < 0) continue;
        bool any = false, cave = false, wood = false;
        for (int k = 0; k < PROP_KINDS; k++) for (int q = 0; q < c->kind_count[k]; q++)
            if (c->kind_list[k][q] == v && collides(k)) { any = true; cave |= concave(k); wood |= wooden(k); }
        if (!any) continue;
        char path[512]; snprintf(path, sizeof path, PROP_DIR "%s.prop", c->name[v]);
        float* pos = NULL; int32_t* idx = NULL; int ni = 0, nv = prop_read_lod(path, 1, &pos, &idx, &ni);
        if (nv < 4) { free(pos); free(idx); continue; }
        if (cave) {
            b3MeshDef md = {0}; md.vertices = (b3Vec3*)pos; md.indices = idx; md.vertexCount = nv; md.triangleCount = ni / 3;
            md.weldTolerance = 0.01f;
            w->prop_mesh[v] = b3CreateMesh(&md, NULL, 0);
        }
        if (wood) w->prop_nparts[v] = log_parts(pos, nv, w->prop_part[v]);
        w->prop_hull[v] = b3CreateHull((const b3Vec3*)pos, nv, 32);
        if (w->prop_nparts[v]) for (int p = 0; p < w->prop_nparts[v]; p++) w->prop_vol[v] += b3ComputeHullMass(w->prop_part[v][p], 1.0f).mass;
        else if (w->prop_hull[v]) w->prop_vol[v] = b3ComputeHullMass(w->prop_hull[v], 1.0f).mass;
        if (w->prop_mesh[v]) { w->prop_mesh_pos[v] = pos; w->prop_mesh_idx[v] = idx; } else { free(pos); free(idx); }
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
            if (!collides(k)) continue;
            int n = prop_scatter(c, &w->map, k, tx, tz, buf, 4096);
            for (int i = 0; i < n; i++) {
                const PropInstance* in = &buf[i]; int v = in->variant; float r = c->radius[v] * in->scale;
                bool dyn = prop_dynamic(k, r);
                if (k == PROP_STONE && !dyn) continue;   // pebbles: walked over
                if (concave(k) ? !w->prop_mesh[v] : !w->prop_hull[v]) continue;
                if (w->nprops == cap) { cap *= 2; w->props = realloc(w->props, sizeof(CretPropCollider) * cap); }
                w->props[w->nprops++] = (CretPropCollider){in->x, in->y, in->z, in->scale, {in->q[0], in->q[1], in->q[2], in->q[3]}, (uint16_t)v, (uint16_t)k, in->key, dyn};
                w->ndyn += dyn;
            }
        }
    }
    w->prop_start[T * T] = w->nprops;
    free(buf);
    if (verbose) fprintf(stderr, "cretaceous: %d prop colliders (%d movable, %d variants)\n", w->nprops, w->ndyn, c->nvar);
}

float cret_prop_mass(const CretWorld* w, int id) {
    const CretPropCollider* pc = &w->props[id];
    return w->prop_vol[pc->variant] * pc->scale * pc->scale * pc->scale * (wooden(pc->kind) ? CRET_WOOD_DENSITY : CRET_ROCK_DENSITY);
}

b3BodyId cret_prop_body(const CretWorld* w, b3WorldId phys, int id, const float p[3], const float q[4], bool awake) {
    const CretPropCollider* pc = &w->props[id];
    b3BodyDef bd = b3DefaultBodyDef(); bd.type = pc->dyn ? b3_dynamicBody : b3_staticBody;
    bd.position = (b3Pos){p[0], p[1], p[2]};
    bd.rotation = (b3Quat){{q[0], q[1], q[2]}, q[3]};
    b3ShapeDef sd = b3DefaultShapeDef(); sd.filter.categoryBits = CRET_CAT_PROP; sd.filter.maskBits = CRET_CAT_AGENT | CRET_CAT_DYN;
    if (pc->dyn) {
        // Rock grips and rolls; wood slides a little more and rolls along its length.
        bd.isAwake = awake; bd.userData = (void*)(intptr_t)(id + 1);
        bd.linearDamping = 0.05f; bd.angularDamping = wooden(pc->kind) ? 0.4f : 0.25f;
        sd.filter.categoryBits = CRET_CAT_PROP | CRET_CAT_DYN;
        sd.filter.maskBits = CRET_CAT_AGENT | CRET_CAT_GROUND | CRET_CAT_PROP | CRET_CAT_DYN;
        sd.density = wooden(pc->kind) ? CRET_WOOD_DENSITY : CRET_ROCK_DENSITY;
        sd.baseMaterial.friction = wooden(pc->kind) ? 0.55f : 0.7f;
        sd.baseMaterial.rollingResistance = 0.04f;
    }
    b3BodyId body = b3CreateBody(phys, &bd);
    b3Vec3 sc = {pc->scale, pc->scale, pc->scale}; int v = pc->variant;
    if (concave(pc->kind) && w->prop_mesh[v]) b3CreateMeshShape(body, &sd, w->prop_mesh[v], sc);
    else if (w->prop_nparts[v]) for (int k = 0; k < w->prop_nparts[v]; k++) b3CreateTransformedHullShape(body, &sd, w->prop_part[v][k], b3Transform_identity, sc);
    else b3CreateTransformedHullShape(body, &sd, w->prop_hull[v], b3Transform_identity, sc);
    return body;
}

int cret_tree_trunks(const CretWorld* w, int tx, int tz, float* out, int cap) {
    static const int TREES[] = {VEG_CONIFER, VEG_BROADLEAF, VEG_CYCAD, VEG_TREEFERN, VEG_PALM};
    int T = (int)ceilf(w->size / VEG_TILE);
    if (w->no_trunks || !w->map.veg || tx < 0 || tz < 0 || tx >= T || tz >= T) return 0;
    VegInstance buf[512]; int n = 0;
    for (int k = 0; k < 5; k++) {
        int m = scatter(&w->map, TREES[k], tx, tz, buf, 512);
        for (int i = 0; i < m && n < cap; i++) {
            float r, h; if (!veg_trunk(&buf[i], &r, &h)) continue;
            float* o = out + 5 * n++; o[0] = buf[i].x; o[1] = buf[i].y; o[2] = buf[i].z; o[3] = r; o[4] = h;
        }
    }
    return n;
}

void cret_prop_stream_init(CretPropStream* ps, const CretWorld* w) {
    memset(ps, 0, sizeof *ps);
    ps->tiles = (int)ceilf(w->size / PROP_TILE);
    ps->slot = calloc((size_t)ps->tiles * ps->tiles, sizeof(int32_t));
}

bool cret_prop_stream_loaded(const CretPropStream* ps, float x, float z) {
    int tx = (int)floorf(x / PROP_TILE), tz = (int)floorf(z / PROP_TILE);
    return ps->slot && tx >= 0 && tz >= 0 && tx < ps->tiles && tz < ps->tiles && ps->slot[tz * ps->tiles + tx];
}

static void load_tile(CretPropStream* ps, const CretWorld* w, b3WorldId phys, int t, int tick) {
    if (ps->ns == ps->cap) { ps->cap = ps->cap ? ps->cap * 2 : 64; ps->s = realloc(ps->s, sizeof(CretPropSlot) * ps->cap); }
    CretPropSlot* s = &ps->s[ps->ns]; int a = w->prop_start ? w->prop_start[t] : 0, b = w->prop_start ? w->prop_start[t + 1] : 0;
    s->tile = t; s->last = tick; s->n = 0;
    s->bodies = malloc(sizeof(b3BodyId) * (b - a + 1)); s->ids = malloc(sizeof(int32_t) * (b - a + 1));
    for (int i = a; i < b; i++) {
        const CretPropCollider* pc = &w->props[i];
        if (pc->dyn && ps->owned && ps->owned(ps->owned_ctx, i)) continue;   // moved: the sim keeps it
        float p[3] = {pc->x, pc->y, pc->z};
        s->bodies[s->n] = cret_prop_body(w, phys, i, p, pc->q, false); s->ids[s->n++] = pc->dyn ? i : -1;
    }
    // Tree trunks: one static body at the tile corner, a capsule per trunk.
    float tr[5 * 256]; int nt = cret_tree_trunks(w, t % ps->tiles, t / ps->tiles, tr, 256);
    if (nt) {
        float ox = (t % ps->tiles) * PROP_TILE, oz = (t / ps->tiles) * PROP_TILE;
        b3BodyDef bd = b3DefaultBodyDef(); bd.type = b3_staticBody; bd.position = (b3Pos){ox, 0, oz};
        b3BodyId body = b3CreateBody(phys, &bd);
        b3ShapeDef sd = b3DefaultShapeDef(); sd.filter.categoryBits = CRET_CAT_PROP; sd.filter.maskBits = CRET_CAT_AGENT | CRET_CAT_DYN;
        for (int k = 0; k < nt; k++) {
            const float* q = tr + 5 * k; float r = q[3];
            b3Capsule cap = {{q[0] - ox, q[1] + r, q[2] - oz}, {q[0] - ox, q[1] + fmaxf(q[4] - r, r + 0.01f), q[2] - oz}, r};
            b3CreateCapsuleShape(body, &sd, &cap);
        }
        s->bodies[s->n] = body; s->ids[s->n++] = -1;
    }
    ps->slot[t] = ++ps->ns;
}

void cret_prop_stream_touch(CretPropStream* ps, const CretWorld* w, b3WorldId phys, float x, float z, float reach, int tick) {
    if (!ps->slot) return;
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
        for (int k = 0; k < s->n; k++) {
            if (s->ids[k] >= 0 && ps->owned && ps->owned(ps->owned_ctx, s->ids[k])) continue;   // moved since: the sim's now
            if (b3Body_IsValid(s->bodies[k])) b3DestroyBody(s->bodies[k]);
        }
        free(s->bodies); free(s->ids); ps->slot[s->tile] = 0;
        if (i != ps->ns - 1) { ps->s[i] = ps->s[ps->ns - 1]; ps->slot[ps->s[i].tile] = i + 1; }
        ps->ns--;
    }
}

void cret_prop_stream_free(CretPropStream* ps) {
    for (int i = 0; i < ps->ns; i++) { free(ps->s[i].bodies); free(ps->s[i].ids); }
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
    w->hcoef = terra_bspline_coefficients(w->map.height, n);
    if (!w->hcoef) { terra_free(&w->map); free(w); return NULL; }
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
    w->no_trunks = getenv("CRET_NO_PROP_COLLIDERS") || getenv("CRET_NO_TREE_COLLIDERS");
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
        b3DestroyHeightField(w->field); terra_free(&w->map); free(w->hcoef);
        for (int v = 0; v < PROP_MAX_VARIANTS; v++) {
            if (w->prop_hull[v]) b3DestroyHull(w->prop_hull[v]);
            for (int k = 0; k < w->prop_nparts[v]; k++) b3DestroyHull(w->prop_part[v][k]);
            if (w->prop_mesh[v]) b3DestroyMesh(w->prop_mesh[v]);
            free(w->prop_mesh_pos[v]); free(w->prop_mesh_idx[v]);
        }
        free(w->props); free(w->prop_start); free(w);
    }
    pthread_mutex_unlock(&g_lock);
}

b3BodyId cret_world_add_ground(const CretWorld* w, b3WorldId world) {
    b3BodyDef bd = b3DefaultBodyDef();
    b3BodyId body = b3CreateBody(world, &bd);
    b3ShapeDef sd = b3DefaultShapeDef(); sd.filter.categoryBits = CRET_CAT_GROUND; sd.filter.maskBits = CRET_CAT_DYN;
    sd.baseMaterial.friction = 0.7f; sd.baseMaterial.rollingResistance = 0.04f;
    b3CreateHeightFieldShape(body, &sd, w->field);
    return body;
}
