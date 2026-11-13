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
    static const int TREES[] = {VEG_CONIFER, VEG_BROADLEAF, VEG_CYCAD, VEG_TREEFERN, VEG_PALM, VEG_SNAG};
    int T = (int)ceilf(w->size / VEG_TILE);
    if (w->no_trunks || !w->map.veg || tx < 0 || tz < 0 || tx >= T || tz >= T) return 0;
    VegInstance buf[512]; int n = 0;
    for (int k = 0; k < (int)(sizeof TREES / sizeof *TREES); k++) {
        int m = scatter(&w->map, TREES[k], tx, tz, buf, 512);
        for (int i = 0; i < m && n < cap; i++) {
            float r, h; if (!veg_trunk(&buf[i], &r, &h)) continue;
            float* o = out + 5 * n++; o[0] = buf[i].x; o[1] = buf[i].y; o[2] = buf[i].z; o[3] = r; o[4] = h;
        }
    }
    return n;
}

// ---------------------------------------------------------------- food
// Expected edible matter (kg/m^2) per kind at one point: for every bake
// layer the scatter's chance of a plant per lattice site (veg_place.h:
// layer, habitat multiplier, dice), over the lattice cell, times its
// species' shares (flora_shares) and their food; and water lilies by
// their placement rule (render/veg.c place_lilies).
static void food_at(const TerraMap* m, float x, float z, float out[CRET_FOOD_KINDS]) {
    FloraSite site = flora_site(m, x, z);
    float h = terra_height(m, x, z), wl = terra_water(m, x, z), depth = wl - h;
    int gx = (int)fminf(fmaxf(x / m->cell + 0.5f, 0), m->n - 1), gz = (int)fminf(fmaxf(z / m->cell + 0.5f, 0), m->n - 1);
    int wk = m->water_kind[(size_t)gz * m->n + gx]; bool wet = wk != TERRA_WATER_NONE && depth > 0.05f;
    for (int f = 0; f < CRET_FOOD_KINDS; f++) out[f] = 0;
    if (h < 0.4f || wk == TERRA_WATER_SEA) return;
    for (int k = 0; k < TERRA_VEG_COUNT; k++) {
        if (site.slope > (is_tree(k) ? 0.95f : 1.1f) || (wet && (k != VEG_HORSETAIL || depth > 0.5f))) continue;
        float d = veg_layer_density(m, k, x, z) * DENSITY[k] * flora_density_mul(k, &site), p = fminf(d * d * 1.6f, 1.0f);
        if (p <= 0) continue;
        float sc = 0.5f * (SCALE_LO[k] + SCALE_HI[k]), per_m2 = p / (SPACING[k] * SPACING[k]) * sc * sc;
        int sp[8]; float w[8]; int n = flora_shares(m, k, x, z, &site, sp, w);
        for (int i = 0; i < n; i++) for (int f = 0; f < CRET_FOOD_KINDS; f++) out[f] += per_m2 * w[i] * FLORA[sp[i]].food[f];
    }
    if ((wk == TERRA_WATER_RIVER || wk == TERRA_WATER_LAKE) && depth >= 0.25f && depth <= 1.6f && fl_noise(m->seed * 0x9e3779b1u + 0x11u, x, z, 20.0f) >= 0.56f) {
        float gw = fmaxf(fabsf(terra_water(m, x + 3, z) - terra_water(m, x - 3, z)), fabsf(terra_water(m, x, z + 3) - terra_water(m, x, z - 3))) / 6.0f;
        if (gw <= 0.012f) for (int f = 0; f < CRET_FOOD_KINDS; f++) out[f] += 0.75f / (SPACING[VEG_LILY] * SPACING[VEG_LILY]) * FLORA[FL_LILY].food[f];
    }
}
// The grid: each 16 m cell averages four points, as relative abundance.
static void build_food(CretWorld* w) {
    const TerraMap* m = &w->map; int n = (int)ceilf(w->size / CRET_FOOD_CELL);
    uint8_t* g = malloc((size_t)n * n * CRET_FOOD_KINDS);
    #pragma omp parallel for schedule(dynamic, 4)
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
        float acc[CRET_FOOD_KINDS] = {0}, one[CRET_FOOD_KINDS];
        for (int q = 0; q < 4; q++) {
            food_at(m, (i + 0.25f + 0.5f * (q & 1)) * CRET_FOOD_CELL, (j + 0.25f + 0.5f * (q >> 1)) * CRET_FOOD_CELL, one);
            for (int f = 0; f < CRET_FOOD_KINDS; f++) acc[f] += 0.25f * one[f];
        }
        for (int f = 0; f < CRET_FOOD_KINDS; f++) g[((size_t)j * n + i) * CRET_FOOD_KINDS + f] = (uint8_t)(255.0f * (1.0f - expf(-acc[f] / CRET_FOOD_SCALE[f])) + 0.5f);
    }
    w->food_n = n;
    __atomic_store_n(&w->food, g, __ATOMIC_RELEASE);
}
static pthread_mutex_t g_food_lock = PTHREAD_MUTEX_INITIALIZER;
CretFood cret_food_kinds_at(const CretWorld* w, float x, float z) {
    const uint8_t* g = __atomic_load_n(&w->food, __ATOMIC_ACQUIRE);
    if (!g) {
        pthread_mutex_lock(&g_food_lock);
        if (!w->food) build_food((CretWorld*)w);   // a cache: the world's data does not change
        pthread_mutex_unlock(&g_food_lock);
        g = w->food;
    }
    int n = w->food_n; float gx = fminf(fmaxf(x / CRET_FOOD_CELL - 0.5f, 0), n - 1.001f), gz = fminf(fmaxf(z / CRET_FOOD_CELL - 0.5f, 0), n - 1.001f);
    int ix = (int)gx, iz = (int)gz; float fx = gx - ix, fz = gz - iz; float o[CRET_FOOD_KINDS];
    const uint8_t* a = g + ((size_t)iz * n + ix) * CRET_FOOD_KINDS, * c = a + (size_t)n * CRET_FOOD_KINDS;
    for (int f = 0; f < CRET_FOOD_KINDS; f++) {
        float top = a[f] + (a[f + CRET_FOOD_KINDS] - a[f]) * fx, bot = c[f] + (c[f + CRET_FOOD_KINDS] - c[f]) * fx;
        o[f] = (top + (bot - top) * fz) * (1.0f / 255.0f);
    }
    return (CretFood){o[0], o[1], o[2], o[3]};
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
        long age = (long)tick - s->last;   // wide: a client's tick comes from the network (net fuzzing reached an overflow)
        if (age <= max_age && age >= 0) { i++; continue; }
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

// Open country. The baked island is closed forest almost everywhere (over nine tenths of the land under
// a full canopy). Regions of gentle ground, picked by a smooth noise of GRASS_CELL m cells, become dry
// grassland: nearly all their trees and most of their ferns go (map.veg, once, when the world loads),
// the scrub stays. About a quarter of the land, in a few large pieces with soft edges.
#define GRASS_CELL 700.0f
static float grass_hash(int ix, int iz, uint32_t salt) { uint32_t h = (uint32_t)ix * 73856093u ^ (uint32_t)iz * 19349663u ^ salt; h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return (h >> 8) * (1.0f / 16777216.0f); }
static float grass_noise(float x, float z, float cell, uint32_t salt) {
    float fx = x / cell, fz = z / cell; int ix = (int)floorf(fx), iz = (int)floorf(fz); float tx = fx - ix, tz = fz - iz;
    tx = tx * tx * (3 - 2 * tx); tz = tz * tz * (3 - 2 * tz);
    return (grass_hash(ix, iz, salt) * (1 - tx) + grass_hash(ix + 1, iz, salt) * tx) * (1 - tz) + (grass_hash(ix, iz + 1, salt) * (1 - tx) + grass_hash(ix + 1, iz + 1, salt) * tx) * tz;
}
// Gorges. The land is cut where a smooth noise of GORGE_CELL m cells crosses its middle value, which
// draws long winding lines; within GORGE_HALF m of a line the ground drops GORGE_DEPTH m to a flat
// floor, by walls in three ledges (stepped slabs, not a smooth V). Only in some regions (a second,
// larger noise), on land well above the sea and clear of rivers and lakes, and the cut fades out at
// a region's edge, which makes the ramps in and out. Walls are bare rock and scree, the floor dry
// soil; no trees stand in a gorge. Done on map.height once at load, before the surface, the
// collision field, the plants and the textures are built from it, so all of them agree.
#define GORGE_CELL 520.0f
#define GORGE_HALF 15.0f
#define GORGE_DEPTH 16.0f
static void gorges_build(CretWorld* w) {
    TerraMap* m = &w->map; int n = m->n, h = n / 2; float c = m->cell; size_t N = (size_t)n * n;
    w->gorge = calloc((size_t)h * h, 1); uint8_t* wet = calloc(N, 1); float* cut = calloc(N, sizeof(float)); uint8_t* wall = calloc(N, 1);
    if (!w->gorge || !wet || !cut || !wall) { free(wet); free(cut); free(wall); return; }
    // Keep 40 m from any water.
    int R = (int)(40.0f / c) + 1;
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        if (m->water_kind[(size_t)z * n + x] == TERRA_WATER_NONE) continue;
        for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
            int qx = x + dx, qz = z + dz; if (qx >= 0 && qz >= 0 && qx < n && qz < n) wet[(size_t)qz * n + qx] = 1;
        }
    }
    #pragma omp parallel for schedule(static)
    for (int z = 2; z < n - 2; z++) for (int x = 2; x < n - 2; x++) {
        size_t i = (size_t)z * n + x; float px = x * c, pz = z * c, y = m->height[i];
        if (wet[i] || y < 10.0f) continue;
        float region = grass_noise(px, pz, 1500.0f, 0x90a6eu); region = (region - 0.5f) / 0.1f; region = region < 0 ? 0 : region > 1 ? 1 : region;
        if (region <= 0) continue;
        const float e = 6.0f;
        float n0 = grass_noise(px, pz, GORGE_CELL, 0xc0ffeu), nx = grass_noise(px + e, pz, GORGE_CELL, 0xc0ffeu), nz = grass_noise(px, pz + e, GORGE_CELL, 0xc0ffeu);
        float g = sqrtf((nx - n0) * (nx - n0) + (nz - n0) * (nz - n0)) / e; if (g < 1e-5f) continue;
        float dist = fabsf(n0 - 0.5f) / g, t = dist / GORGE_HALF; if (t >= 1) continue;
        float p, steps = 0;
        if (t < 0.3f) p = 1;
        else { float s = 1 - (t - 0.3f) / 0.7f; steps = floorf(s * 3) / 3; p = 0.82f * steps + 0.18f * s; }
        float depth = GORGE_DEPTH * (0.8f + 0.4f * grass_noise(px, pz, 260.0f, 0x7711u)) * p * region;
        depth = fminf(depth, y - 6.0f); if (depth < 0.3f) continue;
        cut[i] = depth; wall[i] = t >= 0.3f;
    }
    for (size_t i = 0; i < N; i++) {
        if (cut[i] <= 0) continue;
        m->height[i] -= cut[i];
        uint8_t* sp = m->splat + i * TERRA_MAT_COUNT; uint8_t* v = m->veg + i * TERRA_VEG_COUNT;
        float k = cut[i] / (GORGE_DEPTH * 0.5f); k = k > 1 ? 1 : k;   // how much of a gorge this is (the ramps keep their own ground)
        int rock = wall[i] ? 190 : 20, scree = wall[i] ? 65 : 95, dirt = wall[i] ? 0 : 140;
        int tot = 0;
        for (int q = 0; q < TERRA_MAT_COUNT; q++) { int old = sp[q], nw = q == TERRA_MAT_ROCK ? rock : q == TERRA_MAT_SCREE ? scree : q == TERRA_MAT_DIRT ? dirt : 0; int val = (int)(old + (nw - old) * k + 0.5f); sp[q] = (uint8_t)val; tot += val; }
        sp[wall[i] ? TERRA_MAT_ROCK : TERRA_MAT_DIRT] = (uint8_t)(sp[wall[i] ? TERRA_MAT_ROCK : TERRA_MAT_DIRT] + 255 - tot);   // sums to 255
        for (int q = 0; q < TERRA_VEG_COUNT; q++) v[q] = (uint8_t)(v[q] * (1 - k) * (q == TERRA_VEG_FERN && !wall[i] ? 1.0f : 1.0f));
        if (!wall[i]) v[TERRA_VEG_FERN] = (uint8_t)(40 * k);   // a few ferns on the floor
    }
    for (int z = 0; z < h; z++) for (int x = 0; x < h; x++) {
        float d = cut[(size_t)(2 * z) * n + 2 * x] / GORGE_DEPTH; w->gorge[(size_t)z * h + x] = (uint8_t)((d > 1 ? 1 : d) * 255 + 0.5f);
    }
    if (getenv("CRET_GORGE_DBG")) {   // how much was cut, and a few floors to go and look at
        long cells = 0; int shown = 0;
        for (size_t i = 0; i < N; i++) cells += cut[i] > 1;
        fprintf(stderr, "gorges: %.1f km of floor-width cut (%ld cells)\n", cells * c * c / (2 * GORGE_HALF) / 1000.0, cells);
        for (int z = 40; z < n - 40 && shown < 8; z += 37) for (int x = 40; x < n - 40 && shown < 8; x += 41)
            if (cut[(size_t)z * n + x] > 0.9f * GORGE_DEPTH && !wall[(size_t)z * n + x]) { fprintf(stderr, "GORGE floor at %.0f %.0f depth %.1f\n", x * c, z * c, cut[(size_t)z * n + x]); shown++; }
    }
    free(wet); free(cut); free(wall);
}
float cret_world_gorge(const CretWorld* w, float x, float z) {
    if (!w->gorge) return 0;
    int h = w->map.n / 2, gx = (int)(x / (2 * w->map.cell)), gz = (int)(z / (2 * w->map.cell));
    if (gx < 0 || gz < 0 || gx >= h || gz >= h) return 0;
    return w->gorge[(size_t)gz * h + gx] / 255.0f;
}
static void grassland_build(CretWorld* w) {
    TerraMap* m = &w->map; int n = m->n, h = n / 2; float c = m->cell;
    w->grassland = calloc((size_t)h * h, 1); if (!w->grassland) return;
    for (int z = 0; z < h; z++) for (int x = 0; x < h; x++) {
        int sx = 2 * x, sz = 2 * z; float px = sx * c, pz = sz * c, y = m->height[(size_t)sz * n + sx];
        if (y < 1.0f) continue;
        int xl = sx >= 2 ? sx - 2 : 0, xr = sx + 2 < n ? sx + 2 : n - 1, zl = sz >= 2 ? sz - 2 : 0, zr = sz + 2 < n ? sz + 2 : n - 1;
        float gx = (m->height[(size_t)sz * n + xr] - m->height[(size_t)sz * n + xl]) / ((xr - xl) * c), gz = (m->height[(size_t)zr * n + sx] - m->height[(size_t)zl * n + sx]) / ((zr - zl) * c);
        float slope = sqrtf(gx * gx + gz * gz);
        float nz = 0.7f * grass_noise(px, pz, GRASS_CELL, 0x6ea55u) + 0.3f * grass_noise(px, pz, GRASS_CELL * 0.35f, 0x1234u);
        float g = (nz - 0.5f) / 0.08f; g = g < 0 ? 0 : g > 1 ? 1 : g; g = g * g * (3 - 2 * g);
        float flat = 1 - (slope - 0.3f) / 0.3f; flat = flat < 0 ? 0 : flat > 1 ? 1 : flat;
        w->grassland[(size_t)z * h + x] = (uint8_t)(g * flat * 255 + 0.5f);
    }
    for (int sz = 0; sz < n; sz++) for (int sx = 0; sx < n; sx++) {
        float g = w->grassland[(size_t)(sz / 2 < h ? sz / 2 : h - 1) * h + (sx / 2 < h ? sx / 2 : h - 1)] / 255.0f; if (g <= 0) continue;
        uint8_t* v = m->veg + ((size_t)sz * n + sx) * TERRA_VEG_COUNT;
        float trees = 1 - 0.94f * g, ferns = 1 - 0.75f * g;
        v[TERRA_VEG_CONIFER] = (uint8_t)(v[TERRA_VEG_CONIFER] * trees); v[TERRA_VEG_BROADLEAF] = (uint8_t)(v[TERRA_VEG_BROADLEAF] * trees);
        v[TERRA_VEG_TREEFERN] = (uint8_t)(v[TERRA_VEG_TREEFERN] * trees); v[TERRA_VEG_PALM] = (uint8_t)(v[TERRA_VEG_PALM] * trees);
        v[TERRA_VEG_FERN] = (uint8_t)(v[TERRA_VEG_FERN] * ferns); v[TERRA_VEG_HORSETAIL] = (uint8_t)(v[TERRA_VEG_HORSETAIL] * ferns);
    }
}
float cret_world_grassland(const CretWorld* w, float x, float z) {
    if (!w->grassland) return 0;
    int h = w->map.n / 2, gx = (int)(x / (2 * w->map.cell)), gz = (int)(z / (2 * w->map.cell));
    if (gx < 0 || gz < 0 || gx >= h || gz >= h) return 0;
    return w->grassland[(size_t)gz * h + gx] / 255.0f;
}

// Distance to the nearest water on a coarse grid: a two-pass chamfer
// transform from the cells that hold water deep enough to drink.
static void water_far_build(CretWorld* w) {
    int n = (int)(w->size / CRET_WATER_CELL); if (n < 2) return;
    float* d = malloc(sizeof(float) * (size_t)n * n); if (!d) return;
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        float px = (x + 0.5f) * CRET_WATER_CELL, pz = (z + 0.5f) * CRET_WATER_CELL;
        d[z * n + x] = cret_water(w, px, pz) > cret_height(w, px, pz) + 0.3f ? 0 : 1e9f;
    }
    // Over ground that can be walked only (a cell whose middle is too steep is a wall), so the way
    // down the grid is a way an animal can take; four sweeps carry it round obstacles.
    uint8_t* wall = malloc((size_t)n * n); if (!wall) { free(d); return; }
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        int steep = 0;   // of nine points in the cell, by the slope the bots walk (bots.c walkable: normal y > 0.8)
        for (int k = 0; k < 9 && d[z * n + x] > 0; k++) {
            float nn[3]; terra_normal(&w->map, (x + 0.25f + 0.25f * (k % 3)) * CRET_WATER_CELL, (z + 0.25f + 0.25f * (k / 3)) * CRET_WATER_CELL, nn);
            steep += nn[1] <= 0.8f;
        }
        wall[z * n + x] = steep >= 6;
    }
    float c = CRET_WATER_CELL, dg = c * 1.4142f;
    for (int pass = 0; pass < 4; pass++) {
        for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
            float* v = &d[z * n + x]; if (wall[z * n + x]) continue;
            if (x > 0) *v = fminf(*v, v[-1] + c);
            if (z > 0) { *v = fminf(*v, v[-n] + c); if (x > 0) *v = fminf(*v, v[-n - 1] + dg); if (x < n - 1) *v = fminf(*v, v[-n + 1] + dg); }
        }
        for (int z = n - 1; z >= 0; z--) for (int x = n - 1; x >= 0; x--) {
            float* v = &d[z * n + x]; if (wall[z * n + x]) continue;
            if (x < n - 1) *v = fminf(*v, v[1] + c);
            if (z < n - 1) { *v = fminf(*v, v[n] + c); if (x < n - 1) *v = fminf(*v, v[n + 1] + dg); if (x > 0) *v = fminf(*v, v[n - 1] + dg); }
        }
    }
    free(wall);
    w->water_far = d; w->water_n = n;
}
bool cret_world_toward_water(const CretWorld* w, float x, float z, float reach, float* gx, float* gz) {
    int n = w->water_n; if (!w->water_far) return false;
    int cx = (int)(x / CRET_WATER_CELL), cz = (int)(z / CRET_WATER_CELL);
    cx = cx < 0 ? 0 : cx >= n ? n - 1 : cx; cz = cz < 0 ? 0 : cz >= n ? n - 1 : cz;
    float here = w->water_far[cz * n + cx]; if (here <= 0 || here > 1e8f) return false;
    for (int step = 0; step < (int)(reach / CRET_WATER_CELL) && w->water_far[cz * n + cx] > 0; step++) {
        int bx = cx, bz = cz; float bd = w->water_far[cz * n + cx];
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            int qx = cx + dx, qz = cz + dz; if (qx < 0 || qz < 0 || qx >= n || qz >= n) continue;
            if (w->water_far[qz * n + qx] < bd) { bd = w->water_far[qz * n + qx]; bx = qx; bz = qz; }
        }
        if (bx == cx && bz == cz) break;
        cx = bx; cz = bz;
    }
    *gx = (cx + 0.5f) * CRET_WATER_CELL; *gz = (cz + 0.5f) * CRET_WATER_CELL;
    return true;
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
#ifdef __EMSCRIPTEN__
    // The page wrote the island into the in-memory file system; once loaded
    // that copy (185 MB at 2048^2) is only held twice. It goes, and the world
    // stays loaded for the whole visit (cret_world_release).
    else remove(path);
#endif
    gorges_build(w);
    grassland_build(w);
    terra_habitat(&w->map);   // again: the habitat grid reads the plant layers the grassland just thinned
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
    water_far_build(w);
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
#ifdef __EMSCRIPTEN__
    // Web: the island's file is gone after the first load (load_or_bake), so
    // the world stays for the next sim (a scenario after the title) instead
    // of being baked again.
    w->refs--;
#else
    if (--w->refs == 0) {
        for (int i = 0; i < MAX_WORLDS; i++) if (g_worlds[i] == w) g_worlds[i] = NULL;
        b3DestroyHeightField(w->field); terra_free(&w->map); free(w->hcoef);
        for (int v = 0; v < PROP_MAX_VARIANTS; v++) {
            if (w->prop_hull[v]) b3DestroyHull(w->prop_hull[v]);
            for (int k = 0; k < w->prop_nparts[v]; k++) b3DestroyHull(w->prop_part[v][k]);
            if (w->prop_mesh[v]) b3DestroyMesh(w->prop_mesh[v]);
            free(w->prop_mesh_pos[v]); free(w->prop_mesh_idx[v]);
        }
        free(w->props); free(w->prop_start); free(w->food); free(w->water_far); free(w->grassland); free(w->gorge); free(w);
    }
#endif
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
