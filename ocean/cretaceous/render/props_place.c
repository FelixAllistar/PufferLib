#include "props_place.h"
#include "flora.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PPI 3.14159265f

static inline uint32_t hashu(uint32_t h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h; }
typedef struct { uint32_t s; } PRng;
static inline float pr(PRng* r) { r->s = hashu(r->s + 0x9e3779b9u); return (r->s >> 8) * (1.0f / 16777216.0f); }
static inline float prr(PRng* r, float a, float b) { return a + (b - a) * pr(r); }
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }

// Tors: landmarks on easy ground. A smooth noise of TOR_CELL m cells; where it peaks (a few places a
// square kilometre) outcrops stand whatever the slope, crowded and up to TOR_BIG times the usual size,
// so the lowlands have rock piles to steer by, hide behind and pass between. 0 away from a tor, 1 at its heart.
#define TOR_CELL 150.0f
#define TOR_BIG 2.4f
#define TOR_SPACING 12.0f   // a tor's own lattice (a second pass over the outcrops, so the rest of the island's rocks stay where they were)
// Gorge walls: big rock faces along each wall's foot on their own lattice (a second pass over the cliffs,
// so the island's other cliffs stay where they were), facing the floor, so a gorge reads as rock walls
// rather than a smooth dirt slope.
#define SLAB_SPACING 8.0f
#define SLAB_R_LO 5.5f
#define SLAB_R_HI 9.0f
static float tor_cell(int ix, int iz) { return (hashu((uint32_t)ix * 73856093u ^ (uint32_t)iz * 19349663u ^ 0x70f5u) >> 8) * (1.0f / 16777216.0f); }
static float tor_at(float x, float z) {
    float fx = x / TOR_CELL, fz = z / TOR_CELL; int ix = (int)floorf(fx), iz = (int)floorf(fz); float tx = fx - ix, tz = fz - iz;
    tx = tx * tx * (3 - 2 * tx); tz = tz * tz * (3 - 2 * tz);
    float n = (tor_cell(ix, iz) * (1 - tx) + tor_cell(ix + 1, iz) * tx) * (1 - tz) + (tor_cell(ix, iz + 1) * (1 - tx) + tor_cell(ix + 1, iz + 1) * tx) * tz;
    return sstep(0.74f, 0.86f, n);
}
float prop_tor_at(float x, float z) { return tor_at(x, z); }

// ---------------------------------------------------------------- classes
const PropClass PROP_CLASS[PROP_KINDS] = {
    [PROP_BOULDER]   = {11.0f, 0.45f, 2.0f, 0.28f, 0.6f, 0.35f, false, 1.0f},
    [PROP_OUTCROP]   = {30.0f, 2.5f, 5.0f, 0.15f, 0.8f, 0.2f, false, 1.0f},
    [PROP_STONE]     = {2.6f, 0.10f, 0.45f, 0.3f, 0.8f, 0.6f, true, 1.0f},
    [PROP_LOG]       = {22.0f, 2.2f, 4.0f, 0.12f, 1.0f, 0.05f, false, 1.0f},
    [PROP_STUMP]     = {26.0f, 0.6f, 1.0f, 0.06f, 0.4f, 0.05f, false, 1.0f},
    [PROP_ROOTS]     = {34.0f, 1.4f, 2.4f, 0.18f, 0.7f, 0.1f, false, 1.0f},
    [PROP_DEBRIS]    = {5.0f, 0.25f, 0.6f, 0.04f, 1.0f, 0.05f, true, 1.0f},
    [PROP_DRIFTWOOD] = {16.0f, 1.2f, 2.4f, 0.15f, 1.0f, 0.05f, false, 1.0f},
    [PROP_COAST]     = {90.0f, 14.0f, 30.0f, 0.12f, 0.5f, 0.03f, false, 0.6f},
    [PROP_CLIFF]     = {30.0f, 6.0f, 16.0f, 0.22f, 0.45f, 0.15f, false, 0.7f},
};

// Asset name prefix -> classes it may be scattered as.
int prop_classes_for(const char* name, int out[3]) {
    int n = 0;
    if (!strncmp(name, "rock_moss_set_01", 16)) out[n++] = PROP_OUTCROP;
    else if (!strncmp(name, "rock_moss_set_02", 16) || strstr(name, "boulder")) out[n++] = PROP_BOULDER;
    else if (!strncmp(name, "rock_0", 6) || !strncmp(name, "stone_", 6) || strstr(name, "namaqualand_rocks")) out[n++] = PROP_STONE;
    else if (!strcmp(name, "dead_tree_trunk")) out[n++] = PROP_LOG;
    else if (!strcmp(name, "dead_tree_trunk_02")) { out[n++] = PROP_LOG; out[n++] = PROP_DRIFTWOOD; }
    else if (strstr(name, "stump")) out[n++] = PROP_STUMP;
    else if (strstr(name, "root_cluster")) out[n++] = PROP_ROOTS;
    else if (strstr(name, "bark_debris")) out[n++] = PROP_DEBRIS;
    else if (strstr(name, "coast_rocks") || strstr(name, "coastal_cliff") || strstr(name, "coast_land_rocks")) out[n++] = PROP_COAST;
    else if (strstr(name, "rock_face") || strstr(name, "namaqualand_cliff")) out[n++] = PROP_CLIFF;
    return n;
}

// ---------------------------------------------------------------- placement
static size_t cell_of(const TerraMap* m, float x, float z) {
    int gx = (int)clampf(x / m->cell + 0.5f, 0, m->n - 1), gz = (int)clampf(z / m->cell + 0.5f, 0, m->n - 1);
    return (size_t)gz * m->n + gx;
}
static float splat(const TerraMap* m, size_t i, int k) { return m->splat[i * TERRA_MAT_COUNT + k] / 255.0f; }
static float vegw(const TerraMap* m, size_t i, int k) { return m->veg[i * TERRA_VEG_COUNT + k] / 255.0f; }
static float canopy_at(const TerraMap* m, size_t i) {
    return clampf(vegw(m, i, TERRA_VEG_CONIFER) + vegw(m, i, TERRA_VEG_BROADLEAF) + 0.6f * vegw(m, i, TERRA_VEG_TREEFERN), 0, 1);
}
static bool near_sea(const TerraMap* m, float x, float z, float r) {
    for (int k = 0; k < 8; k++) {
        float a = k * PPI / 4;
        if (m->water_kind[cell_of(m, x + cosf(a) * r, z + sinf(a) * r)] == TERRA_WATER_SEA) return true;
    }
    return false;
}

static bool near_river(const TerraMap* m, float x, float z, float r) {
    for (int k = 0; k < 6; k++) {
        float a = k * PPI / 3; int wk = m->water_kind[cell_of(m, x + cosf(a) * r, z + sinf(a) * r)];
        if (wk == TERRA_WATER_RIVER) return true;
    }
    return m->water_kind[cell_of(m, x, z)] == TERRA_WATER_RIVER;
}

static float density(const TerraMap* m, int kind, float x, float z, float slope, float h) {
    size_t i = cell_of(m, x, z);
    float rock = splat(m, i, TERRA_MAT_ROCK), scree = splat(m, i, TERRA_MAT_SCREE), moss = splat(m, i, TERRA_MAT_MOSS);
    float sand = splat(m, i, TERRA_MAT_SAND), mud = splat(m, i, TERRA_MAT_MUD), can = canopy_at(m, i);
    switch (kind) {
    case PROP_BOULDER: return (0.09f + 0.6f * near_river(m, x, z, 7.0f) + 0.9f * scree + 0.5f * rock + 0.35f * moss + 0.08f * can) * (1 - sstep(1.1f, 1.5f, slope));
    case PROP_OUTCROP: return (0.9f * rock + 0.4f * scree + 0.2f * moss + 0.1f) * sstep(0.35f, 0.8f, slope) * (1 - sstep(1.6f, 2.2f, slope));
    case PROP_STONE: return (0.08f + 0.8f * near_river(m, x, z, 5.0f) + 1.1f * scree + 0.5f * rock + 0.4f * mud + 0.15f * sand) * (1 - sstep(1.2f, 1.6f, slope));
    // Dead wood gathers in old stands (render/flora.h flora_old, the same
    // patches that hold standing dead trees), about as much over the island.
    case PROP_LOG: return 0.55f * can * (0.55f + 1.4f * flora_old(m, x, z)) * (1 - sstep(0.35f, 0.6f, slope));
    case PROP_STUMP: return 0.35f * can * (0.55f + 1.4f * flora_old(m, x, z)) * (1 - sstep(0.5f, 0.8f, slope));
    case PROP_ROOTS: return 0.3f * can * (1 - sstep(0.5f, 0.9f, slope));
    case PROP_DEBRIS: return 0.5f * can * (1 - sstep(0.6f, 1.0f, slope));
    case PROP_DRIFTWOOD: return (h < 3.5f && sand > 0.3f && near_sea(m, x, z, 25.0f)) ? 0.5f : 0.0f;
    case PROP_CLIFF: return (1.1f * rock + 0.4f * scree + 0.35f * sstep(0.9f, 1.4f, slope)) * sstep(0.6f, 1.0f, slope) * (1 - sstep(2.2f, 3.0f, slope));
    case PROP_COAST: return (h > -1.0f && h < 6.0f && (rock + scree + sand) > 0.2f && near_sea(m, x, z, 30.0f)) ? 0.7f : 0.0f;
    }
    return 0;
}

static void quat_mul(const float a[4], const float b[4], float o[4]) {
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
static void quat_axis(float ax, float ay, float az, float ang, float o[4]) {
    float l = sqrtf(ax * ax + ay * ay + az * az); if (l < 1e-6f) { o[0] = o[1] = o[2] = 0; o[3] = 1; return; }
    float s = sinf(ang * 0.5f) / l; o[0] = ax * s; o[1] = ay * s; o[2] = az * s; o[3] = cosf(ang * 0.5f);
}

static void quat_rot(const float q[4], const float v[3], float o[3]) {   // q v q*
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty); o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz); o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

bool prop_dynamic(int kind, float r) {
    return kind == PROP_BOULDER || kind == PROP_LOG || kind == PROP_DRIFTWOOD || (kind == PROP_STONE && r >= PROP_STONE_DYN_R);
}

// Seating. Scanned props are open underneath; every point of the bottom
// rim (catalog base points) must sit below the ground, or the prop floats
// and the hole shows. The lowest origin height that buries the whole rim
// is the minimum over rim points of (ground under the point - its height
// above the origin). How far that lies below the ground at the centre is
// deep on knife edges, spurs and gully crossings, where the prop is refused.
// The ground is the drawn cubic surface when `coef` is given, else the
// bilinear one (the shape of the Box3D heightfield).
static float seat_ground(const TerraMap* m, const float* coef, float x, float z) {
    return coef ? terra_bspline_height(coef, m->n, m->cell, x, z) : terra_height(m, x, z);
}
static float seat_height(const PropCatalog* p, const TerraMap* m, const float* coef, int vi, float x, float z, float sc, const float q[4]) {
    float y = 1e30f;
    for (int k = 0; k < p->nbase[vi]; k++) {
        float b[3] = {p->base[vi][k][0] * sc, p->base[vi][k][1] * sc, p->base[vi][k][2] * sc}, r[3];
        quat_rot(q, b, r);
        y = fminf(y, seat_ground(m, coef, x + r[0], z + r[2]) - r[1]);
    }
    return y;
}
// Extra embedding below the seated height (fraction of the prop's height):
// static pieces settle in; dynamic props only by a small margin, so a woken
// body is not pushed up out of the ground.
static const float EMBED[PROP_KINDS] = {
    [PROP_BOULDER] = 0.03f, [PROP_OUTCROP] = 0.12f, [PROP_STONE] = 0.06f, [PROP_LOG] = 0.06f, [PROP_STUMP] = 0.04f,
    [PROP_ROOTS] = 0.10f, [PROP_DEBRIS] = 0.0f, [PROP_DRIFTWOOD] = 0.06f};
// Deepest the seat may lie below the centre ground, as a fraction of height.
static const float MAX_BURY[PROP_KINDS] = {
    [PROP_BOULDER] = 0.45f, [PROP_OUTCROP] = 0.5f, [PROP_STONE] = 0.5f, [PROP_LOG] = 0.6f, [PROP_STUMP] = 0.45f,
    [PROP_ROOTS] = 0.55f, [PROP_DEBRIS] = 1.0f, [PROP_DRIFTWOOD] = 0.6f};

uint32_t prop_key(const TerraMap* m, int kind, int tx, int tz, int cell) {
    int T = (int)ceilf(terra_size(m) / PROP_TILE);
    return ((uint32_t)(tz * T + tx) * PROP_KINDS + (uint32_t)kind) * 1024u + (uint32_t)cell;
}

static int shoulder_scatter(const PropCatalog* p, const TerraMap* m, int kind, int tx, int tz, PropInstance* out, int cap);
// One candidate per lattice cell; deterministic per (tile, class).
int prop_scatter(const PropCatalog* p, const TerraMap* m, int kind, int tx, int tz, PropInstance* out, int cap) {
    const PropClass* C = &PROP_CLASS[kind];
    if (!p->kind_count[kind]) return 0;
    float sp = C->spacing; int steps = sp >= PROP_TILE ? 1 : (int)(PROP_TILE / sp);
    float cell = PROP_TILE / steps, chance = sp > PROP_TILE ? (PROP_TILE * PROP_TILE) / (sp * sp) : 1.0f;
    bool seated = kind != PROP_COAST && kind != PROP_CLIFF;   // the big set pieces keep the centre sink
    int cnt = 0;
    for (int pass = 0; pass < (kind == PROP_OUTCROP || kind == PROP_CLIFF ? 2 : 1); pass++) {   // pass 1: the tors, the gorge walls
    if (pass) { steps = (int)(PROP_TILE / (kind == PROP_CLIFF ? SLAB_SPACING : TOR_SPACING)); cell = PROP_TILE / steps; }
    for (int j = 0; j < steps; j++) for (int i = 0; i < steps; i++) {
        PRng r = {hashu((uint32_t)(tx * 4099 + i) * 2654435761u ^ (uint32_t)(tz * 3571 + j) * 40503u ^ (uint32_t)(kind + 31 + pass * 97) * 2246822519u)};
        float x = tx * PROP_TILE + (i + pr(&r)) * cell, z = tz * PROP_TILE + (j + pr(&r)) * cell;
        float n[3]; terra_normal(m, x, z, n);
        float slope = sqrtf(fmaxf(1 - n[1] * n[1], 0)) / fmaxf(n[1], 0.05f);
        float h = terra_height(m, x, z);
        // Clumping: a slow noise-free hash field makes boulder fields and gaps.
        float clump = 0.5f + 0.5f * sinf(x * 0.013f + 1.7f * sinf(z * 0.009f)) * sinf(z * 0.011f + 1.3f * sinf(x * 0.007f));
        float d = density(m, kind, x, z, slope, h) * chance * (kind == PROP_BOULDER || kind == PROP_STONE ? 0.4f + 1.2f * clump : 1.0f);
        float tor = 0, slab = 0, in[3] = {0, 0, 0};
        if (pass && kind == PROP_CLIFF) {
            slab = m->ravine == TERRA_RAVINE_STEPPED ? cret_gorge_wall_at(x, z, in) : 0;   // the wide ravines have no walls
            size_t si = cell_of(m, x, z);
            if (slab <= 0 || slope < 0.4f || splat(m, si, TERRA_MAT_ROCK) + splat(m, si, TERRA_MAT_SCREE) < 0.3f) continue;   // only where a wall was actually cut
            d = slab * 0.85f;
        } else if (pass) { tor = slope < 1.6f ? tor_at(x, z) : 0; if (tor <= 0) continue; d = tor * 0.6f; }
        if (pr(&r) >= d) continue;
        if (m->nshoulder && kind != PROP_DRIFTWOOD) {   // a rock shoulder's wall, its top and (for all but loose rock) its apron are its own
            float e = terra_shoulder_near(m, x, z, C->r_hi, NULL);
            bool loose = kind == PROP_BOULDER || kind == PROP_STONE || kind == PROP_DEBRIS;
            if (e < 10 + C->r_hi && e > (loose ? -2.5f : -14.0f) - C->r_hi) continue;
        }
        size_t ci = cell_of(m, x, z);
        float w = terra_water(m, x, z);
        bool wet = m->water_kind[ci] != TERRA_WATER_NONE && w > h + 0.05f;
        if (kind == PROP_COAST) { if (h < 0.2f) continue; }
        else if (h < 0.3f || m->water_kind[ci] == TERRA_WATER_SEA) continue;
        else if (wet && w - h > ((kind == PROP_BOULDER || kind == PROP_STONE) ? 0.7f : 0.25f)) continue;   // rocks may sit in streams
        if (cnt == cap) return cnt;
        int vi = p->kind_list[kind][(int)(pr(&r) * p->kind_count[kind]) % p->kind_count[kind]];
        float target = prr(&r, C->r_lo, C->r_hi); target = C->r_lo + (target - C->r_lo) * pr(&r);   // bias small
        if (slab > 0) target = prr(&r, SLAB_R_LO, SLAB_R_HI);
        target *= 1 + (TOR_BIG - 1) * tor * pr(&r);   // a tor's rocks: up to TOR_BIG times as big
        float sc = target / fmaxf(p->radius[vi], 0.01f);
        // Orientation: yaw, a random tilt, then align up toward the terrain
        // normal. Small rocks lie with the slope; big boulders stand nearer
        // upright (and are refused where the slope would bury them).
        float align = slab > 0 ? 0.15f : C->align;   // a wall's rock faces stand nearly upright
        if (kind == PROP_BOULDER || kind == PROP_STONE) align = 0.95f - 0.45f * sstep(0.5f, 1.6f, target);
        float qy[4], qt[4], qa[4], t1[4], q[4];
        float yaw = prr(&r, 0, 2 * PPI);
        // A wall's rock face: the cliff scans are open shells whose face looks along local +z; it turns to the
        // gorge's floor (from behind, the open back drew as black shards).
        if (slab > 0) yaw = atan2f(in[0], in[1]) + 0.3f * (yaw / PPI - 1);
        // Every other cliff face looks downhill for the same reason: turned at random, those on a gorge's rim
        // showed their open backs to the floor as black shards hanging in the sky.
        else if (kind == PROP_CLIFF && n[0] * n[0] + n[2] * n[2] > 0.01f) yaw = atan2f(n[0], n[2]) + 0.5f * (yaw / PPI - 1);
        quat_axis(0, 1, 0, yaw, qy);
        float ta = prr(&r, 0, 2 * PPI); quat_axis(cosf(ta), 0, sinf(ta), prr(&r, -C->tilt, C->tilt), qt);
        float ux = n[0] * align, uy = 1 - align + n[1] * align, uz = n[2] * align, ul = sqrtf(ux * ux + uy * uy + uz * uz);
        ux /= ul; uy /= ul; uz /= ul;
        quat_axis(uz, 0, -ux, acosf(clampf(uy, -1, 1)), qa);   // (0,1,0) x u = (uz, 0, -ux)
        quat_mul(qt, qy, t1); quat_mul(qa, t1, q);
        float hh = p->height[vi] * sc, y;
        if (seated && p->nbase[vi]) {
            // The rim goes under the drawn surface: seated on the bilinear one, 14% of rocks showed
            // their open bottoms where the cubic dips between samples (gorge floors, bank feet). A
            // dynamic body also rests on the heightfield when it wakes, so it takes the lower of the two seats;
            // each seat is held to the burial limit on its own surface (judged on the lower one, a tenth of
            // the movable rocks, those in the gorges, were refused).
            float ys = seat_height(p, m, m->surface, vi, x, z, sc, q);
            if (seat_ground(m, m->surface, x, z) - ys > MAX_BURY[kind] * hh + 0.05f) continue;   // a ridge, spur or gully it cannot sit on
            if (m->surface && prop_dynamic(kind, p->radius[vi] * sc)) {
                float yb = seat_height(p, m, NULL, vi, x, z, sc, q);
                if (h - yb > MAX_BURY[kind] * hh + 0.05f) continue;
                ys = fminf(ys, yb);
            }
            y = ys - (0.03f + 0.02f * hh) - EMBED[kind] * hh;
        } else y = h - (C->sink * hh + fminf(slope, 1.0f) * p->radius[vi] * sc * 0.2f * (1 - C->align * 0.7f));
        // A wall's rock face stands at the wall's foot, its centre half its radius back in the wall and its
        // front on the floor, so the face rises up the wall a metre or so out from it. (A scanned rock is
        // about as deep as it is wide: placed up a wall this steep, the wall ran straight through it.)
        if (slab > 0) { float rr = p->radius[vi] * sc, f = in[2] - 0.5f * rr; x += in[0] * f; z += in[1] * f; y = terra_height(m, x + in[0] * 0.65f * rr, z + in[1] * 0.65f * rr) - 0.06f * hh; }
        float moss = clampf(1.6f * splat(m, ci, TERRA_MAT_MOSS) + 0.5f * canopy_at(m, ci) + 0.3f * splat(m, ci, TERRA_MAT_FERN) - 0.6f * splat(m, ci, TERRA_MAT_SAND), 0, 1);
        if (kind == PROP_DRIFTWOOD || kind == PROP_COAST) moss = 0;
        if (kind == PROP_STONE || kind == PROP_DEBRIS) moss *= 0.5f;
        float tint = kind == PROP_DRIFTWOOD ? 1.0f : 0.0f;
        out[cnt++] = (PropInstance){x, y, z, sc, {q[0], q[1], q[2], q[3]}, moss * prr(&r, 0.6f, 1.0f), tint, (uint16_t)vi, (uint16_t)kind,
                                    prop_key(m, kind, tx, tz, pass * 512 + j * steps + i)};
    }
    }
    if (m->nshoulder && kind == PROP_OUTCROP) cnt += shoulder_scatter(p, m, kind, tx, tz, out + cnt, cap - cnt);
    return cnt;
}

// ---------------------------------------------------------------- rock shoulders
// The bake cut each shoulder's notch (terra.h TerraShoulder) only as high as its rock reaches; the rock
// is laid out here from the notch alone, so the renderer and the colliders (world.c) place the same
// pieces. The wall is built from closed rocks (the mossy rock scans, which have no open edge, so any side
// may face out and nothing open can show from behind or above), scaled up, leaning back into the hill
// and stood on whichever axis gives the shape a slot wants. Three passes: along the wall's foot, one
// piece after another overlapping by half, each as tall as the wall where it stands and standing a metre
// proud of the foot with its back sunk into the cut, so the rock is the wall; at each joint a rock lodged
// high in the seam and often a lower one in front, so the face reads as one irregular outcrop; then
// rocks lodged on whatever wall is still bare from the review views (sh_fill). All pieces are outcrop
// class (static convex colliders, never pushed about). Up to SH_PIECES a shoulder.
#define SH_PIECES 64
#define SH_PIECE_MAX_H 10.0f   // the tallest piece (m); the bake plans no wall over terra.c SH_ROCK_MAX
#define SH_SCALE_MAX 6.0f     // how far a scan is scaled up (its texture stretches with it)
static void sh_rot(const float q[4], const float* p, float sc, float o[3]) { float b[3] = {p[0] * sc, p[1] * sc, p[2] * sc}; quat_rot(q, b, o); }
// The wall where it rises from (x, z), looking back along -(nx, nz), within `reach` m of the foot: its
// floor height (3 m out from the foot) and how high it rises: to the top of the last half metre that
// climbs 0.45 m or more (slopes over 42 degrees; the lip rounds over above it). Where nothing does (the
// ends of a notch), the rise over the first metre in.
static void sh_wall_at(const TerraMap* m, float x, float z, float nx, float nz, float reach, float* base, float* height) {
    float flo = seat_ground(m, m->surface, x + 3 * nx, z + 3 * nz), lip = seat_ground(m, m->surface, x - nx, z - nz);
    for (float b = -3; b < reach; b += 0.5f) {
        float y0 = seat_ground(m, m->surface, x - b * nx, z - b * nz), y1 = seat_ground(m, m->surface, x - (b + 0.5f) * nx, z - (b + 0.5f) * nz);
        if (y1 - y0 >= 0.45f) lip = fmaxf(lip, y1);
    }
    *base = flo; *height = fmaxf(lip - flo, 0);
}
// Orientation o (0-5) of a closed rock in the wall's frame (+y up, +z out of the hill, x along it):
// which of its axes stands up (y, z or x) and a quarter turn about it, then a lean back into the hill.
static void sh_orient(int o, float spin, float lean, float q[4]) {
    float qa[4], qs[4], ql[4], t[4];
    if (o / 2 == 0) quat_axis(1, 0, 0, 0, qa); else if (o / 2 == 1) quat_axis(1, 0, 0, -PPI / 2, qa); else quat_axis(0, 0, 1, PPI / 2, qa);
    quat_axis(0, 1, 0, spin + (o & 1) * PPI / 2, qs); quat_axis(1, 0, 0, -lean, ql);
    quat_mul(qs, qa, t); quat_mul(ql, t, q);
}
// Bounds (unit scale) of variant v turned by q: lo[3], hi[3] from its support points.
static void sh_bounds(const PropCatalog* p, int v, const float q[4], float lo[3], float hi[3]) {
    for (int a = 0; a < 3; a++) { lo[a] = 1e30f; hi[a] = -1e30f; }
    for (int k = 0; k < PROP_EXT_PTS; k++) { float o[3]; quat_rot(q, p->ext[v][k], o); for (int a = 0; a < 3; a++) { lo[a] = fminf(lo[a], o[a]); hi[a] = fmaxf(hi[a], o[a]); } }
}
// The scans a wall is built from: one of the two mossy rock sets (grey rock_moss_set_02, olive
// rock_moss_set_01) per shoulder, so each wall reads as one bed of rock; the brown and orange boulders
// stay loose on the ground.
static const char* const SH_FAMILY[2] = {"rock_moss_set_02", "rock_moss_set_01"};
// One rock standing at (x0, z0) on the wall's foot, facing out along (nx, nz): `want` m tall from 0.3 m
// under the lowest ground beneath it (when seated) to `top`, ideally `wide` m wide and at most `wmax`,
// standing `proud` m out of the foot. Every closed scan in every orientation is scored on how near its shape comes to
// wide x want, its depth (deep enough to reach back into the cut) and how far it is scaled; a little
// randomness keeps neighbours different. Returns its width along the wall, or 0.
static float sh_rock(const PropCatalog* p, const TerraMap* m, PRng* r, const char* family, float x0, float z0, float nx, float nz, float top, float want, float wide, float wmax, float proud, bool seated, float moss, PropInstance* out) {
    int bv = -1, bo = 0; float best = 1e30f, spin = prr(r, -0.25f, 0.25f) + (pr(r) < 0.5f ? PPI : 0), lean = prr(r, 0.12f, 0.3f);
    static const int KINDS[2] = {PROP_BOULDER, PROP_OUTCROP};
    for (int c = 0; c < 2; c++) for (int k = 0; k < p->kind_count[KINDS[c]]; k++) {
        int v = p->kind_list[KINDS[c]][k]; if (!p->closed[v] || strncmp(p->name[v], family, strlen(family))) continue;
        for (int o = 0; o < 6; o++) {
            float q[4], lo[3], hi[3]; sh_orient(o, spin, lean, q); sh_bounds(p, v, q, lo, hi);
            float h = hi[1] - lo[1], sc = want / fmaxf(h, 0.05f), w = (hi[0] - lo[0]) * sc, d = (hi[2] - lo[2]) * sc;
            if (sc > SH_SCALE_MAX || w > wmax || h * sc > 1.3f * w) continue;   // no slender pieces stood on end: a wall, not standing stones
            float score = fabsf(logf(w / wide)) + 0.6f * fmaxf(0.4f * want - d, 0) / want + 0.05f * sc + 0.35f * pr(r);
            if (score < best) { best = score; bv = v; bo = o; }
        }
    }
    if (bv < 0) return 0;
    float q[4], lo[3], hi[3], yaw = atan2f(nx, nz), qy[4], qw[4]; sh_orient(bo, spin, lean, q); sh_bounds(p, bv, q, lo, hi);
    quat_axis(0, 1, 0, yaw, qy); quat_mul(qy, q, qw);
    float sc = want / (hi[1] - lo[1]), ax = nz, az = -nx;   // (ax, az): along the wall, right of the outward normal
    // The frame origin that puts the rock's middle on the slot and its front `proud` m out of the foot.
    float off = proud - hi[2] * sc, mid = -0.5f * (lo[0] + hi[0]) * sc;
    float x = x0 + nx * off + ax * mid, z = z0 + nz * off + az * mid;
    // Seated 0.3 m under the lowest ground beneath its support points, and no lower than its top allows;
    // a rock lodged up the wall (not seated) just has its top at `top`.
    float g = 1e30f, o[3];
    for (int k = 0; k < PROP_EXT_PTS && seated; k++) { sh_rot(qw, p->ext[bv][k], sc, o); if (o[1] < lo[1] * sc + 0.5f * (hi[1] - lo[1]) * sc) g = fminf(g, seat_ground(m, m->surface, x + o[0], z + o[2])); }
    float y = fminf(g - 0.3f, top - want) - lo[1] * sc;
    *out = (PropInstance){x, y, z, sc, {qw[0], qw[1], qw[2], qw[3]}, moss, 0, (uint16_t)bv, PROP_OUTCROP, 0};
    return (hi[0] - lo[0]) * sc;
}
// The review views (prop_shoulder_view); side -1 is the hillside on the other side of the notch.
static void sh_view(const TerraMap* m, const TerraShoulder* s, int side, float eye[3], float at[3]) {
    float f[4], flo, H; terra_shoulder_foot(s, 0, f); sh_wall_at(m, f[0], f[1], f[2], f[3], s->wall / 3 + 4, &flo, &H);
    at[0] = f[0]; at[2] = f[1]; at[1] = flo + 0.5f * H;
    float back = 1.5f * s->half + 12;
    // Out in front: 5 m over the ground, and no lower than 3 m over the notch's floor, so the fall of the
    // slope below the mouth does not hide it.
    if (!side) { eye[0] = s->x - s->ux * back; eye[2] = s->z - s->uz * back; eye[1] = fmaxf(seat_ground(m, m->surface, eye[0], eye[2]) + 5.0f, s->floor + 3.0f); }
    else {
        float sx = s->x + side * s->uz * 1.4f * s->half - s->ux * 0.6f * s->half, sz = s->z - side * s->ux * 1.4f * s->half - s->uz * 0.6f * s->half;
        eye[0] = sx; eye[2] = sz; eye[1] = fmaxf(seat_ground(m, m->surface, sx, sz) + 6.0f, s->floor + s->wall + 3.0f);
    }
}

// The fill: the wall's bare patches as seen from out in front of the mouth and from the hillsides on
// both sides (sh_view), each rock taken as the ellipsoid inside its bounds (cheap, a little smaller than
// the rock). Wall points are those prop_shoulder_coverage measures, every 0.05 of the mouth across and
// 0.25 m deep. While some stay bare, a rock is lodged on the barest one (the one with the most bare
// points within 1.5 m), sized to the patch, its back set into the wall, standing a metre proud of it.
typedef struct { float p[3], n[2]; uint8_t vis; } ShPt;
static bool sh_hits(const PropCatalog* c, const PropInstance* in, const float o[3], const float d[3]) {
    const float* lo = c->lo[in->variant]; const float* hi = c->hi[in->variant];
    float iq[4] = {-in->q[0], -in->q[1], -in->q[2], in->q[3]}, a[3] = {o[0] - in->x, o[1] - in->y, o[2] - in->z}, ro[3], rd[3];
    quat_rot(iq, a, ro); quat_rot(iq, d, rd);
    float A = 0, B = 0, C = -1;
    for (int k = 0; k < 3; k++) {
        float mid = 0.5f * (lo[k] + hi[k]) * in->scale, ax = fmaxf(0.4f * (hi[k] - lo[k]) * in->scale, 0.05f);
        float u = (ro[k] - mid) / ax, v = rd[k] / ax; A += v * v; B += 2 * u * v; C += u * u;
    }
    float disc = B * B - 4 * A * C; if (disc < 0) return false;
    float t0 = (-B - sqrtf(disc)) / (2 * A), t1 = (-B + sqrtf(disc)) / (2 * A);
    return t1 > 0 && t0 < 1;   // the segment o .. o + d passes through it
}
static int sh_fill(const PropCatalog* p, const TerraMap* m, const TerraShoulder* s, PRng* r, const char* fam, PropInstance* out, int cnt, int cap) {
    enum { NP = 41 * 40 }; ShPt* pt = malloc(sizeof(ShPt) * NP); int np = 0; float eye[3][3], at[3], reach = s->wall / 3 + 4;
    for (int v = 0; v < 3; v++) sh_view(m, s, v == 0 ? 0 : v == 1 ? 1 : -1, eye[v], at);
    for (int i = 0; i <= 40; i++) {
        float t = -1 + 0.05f * i, f[4]; terra_shoulder_foot(s, t, f); bool wall = false;
        for (float b = -0.5f; b <= reach && np < NP; b += 0.25f) {
            float x = f[0] - b * f[2], z = f[1] - b * f[3];
            if (seat_ground(m, m->surface, x - 0.4f * f[2], z - 0.4f * f[3]) - seat_ground(m, m->surface, x + 0.4f * f[2], z + 0.4f * f[3]) <= 0.8f) { if (wall && b > 1) break; continue; }
            wall = true;
            ShPt* q = &pt[np]; q->p[0] = x; q->p[1] = seat_ground(m, m->surface, x, z); q->p[2] = z; q->n[0] = f[2]; q->n[1] = f[3]; q->vis = 0;
            for (int v = 0; v < 3; v++) {   // not hidden by the ground in front
                float d[3] = {x - eye[v][0], q->p[1] - eye[v][1], z - eye[v][2]}, L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); bool hid = false;
                for (float u = 0.5f; u < L - 0.6f && !hid; u += 0.5f) { float f2 = u / L; hid = eye[v][1] + d[1] * f2 < seat_ground(m, m->surface, eye[v][0] + d[0] * f2, eye[v][2] + d[2] * f2) - 0.02f; }
                q->vis |= (uint8_t)(!hid << v);
            }
            np += q->vis != 0;
        }
    }
    uint8_t* bare = malloc(np + 1); uint8_t* tried = calloc(np + 1, 1);
    for (int it = 0; it < 24 && cnt < cap; it++) {
        int nb = 0;
        for (int k = 0; k < np; k++) {
            bare[k] = 0;
            for (int v = 0; v < 3 && !bare[k]; v++) {
                if (!(pt[k].vis >> v & 1)) continue;
                float d[3] = {pt[k].p[0] - eye[v][0], pt[k].p[1] - eye[v][1], pt[k].p[2] - eye[v][2]}; bool hit = false;
                for (int i = 0; i < cnt && !hit; i++) hit = sh_hits(p, &out[i], eye[v], d);
                bare[k] = !hit;
            }
            nb += bare[k];
        }
        if (nb < np / 80 + 2) break;   // what is left is a scatter of single points, not a patch
        int best = -1, bn = 0;
        for (int k = 0; k < np; k++) {
            if (!bare[k] || tried[k]) continue;
            int n = 0;
            for (int j = 0; j < np; j++) n += bare[j] && fabsf(pt[j].p[0] - pt[k].p[0]) + fabsf(pt[j].p[1] - pt[k].p[1]) + fabsf(pt[j].p[2] - pt[k].p[2]) < 1.5f;
            if (n > bn) { bn = n; best = k; }
        }
        if (best < 0) break;
        tried[best] = 1;   // a rock put here that leaves it bare does not get another go
        // The patch's height: the bare points close across it.
        float y0 = 1e30f, y1 = -1e30f;
        for (int j = 0; j < np; j++)
            if (bare[j] && hypotf(pt[j].p[0] - pt[best].p[0], pt[j].p[2] - pt[best].p[2]) < 2.5f) { y0 = fminf(y0, pt[j].p[1]); y1 = fmaxf(y1, pt[j].p[1]); }
        float want = clampf(1.4f * (y1 - y0) + 1.0f, 1.5f, 5.0f), top = fmaxf(y1, pt[best].p[1]) + 0.4f * want;
        if (sh_rock(p, m, r, fam, pt[best].p[0], pt[best].p[2], pt[best].n[0], pt[best].n[1], top, want, want * prr(r, 1.0f, 1.4f), 6, prr(r, 0.8f, 1.3f), false, prr(r, 0.5f, 0.9f), &out[cnt]) > 0) cnt++;
    }
    free(bare); free(tried); free(pt);
    return cnt;
}
static int shoulder_layout(const PropCatalog* p, const TerraMap* m, const TerraShoulder* s, PropInstance* out, int cap) {
    int cnt = 0; PRng r = {hashu(s->seed ^ 0x5401u)}; const char* fam = SH_FAMILY[(s->seed >> 7) & 1];
    // The wall: along its foot from one end to the other, wherever it stands over a metre, a rock as tall
    // as the wall there (its top just over the lip, where the hill meets it), no wider than the wall's
    // curve allows (the notch's sides bend toward the viewer, and a wide flat piece would stand out in
    // front of them). `c` is where the rock so
    // far stops covering the wall: each piece is centred half its wanted width past it, and the next one
    // starts 55% of the way back into this one (the scans are rounded, so their ends cover little).
    float e = 0.02f, joints[SH_PIECES]; int nj = 0;
    for (float c = -1.0f + 0.05f * pr(&r); c < 1.0f && cnt < cap;) {
        float arc0 = sqrtf(s->half * s->half + 4 * s->depth * s->depth * c * c), guess = clampf(0.9f * s->wall, 2.0f, 8.0f), t = c + 0.5f * guess / arc0;
        float g = (terra_shoulder_wall(s, t + e) - terra_shoulder_wall(s, t - e)) / (2 * e), arc = sqrtf(s->half * s->half + g * g);
        float f[4], flo, H; terra_shoulder_foot(s, t, f); sh_wall_at(m, f[0], f[1], f[2], f[3], s->wall / 3 + 4, &flo, &H);
        if (H < 1.0f) { c += 1.0f / arc; continue; }
        float want = fminf(H + 0.45f, SH_PIECE_MAX_H), wide = clampf(want * prr(&r, 0.8f, 1.2f), 2.0f, 9.0f);
        t = c + 0.5f * wide / arc; terra_shoulder_foot(s, t, f); sh_wall_at(m, f[0], f[1], f[2], f[3], s->wall / 3 + 4, &flo, &H);
        want = fminf(fmaxf(H, 1.0f) + 0.8f, SH_PIECE_MAX_H);
        float g2 = (terra_shoulder_wall(s, t + e) - 2 * terra_shoulder_wall(s, t) + terra_shoulder_wall(s, t - e)) / (e * e * s->half * s->half);
        float rc = powf(1 + g * g / (s->half * s->half), 1.5f) / fmaxf(fabsf(g2), 1e-4f);
        float w = sh_rock(p, m, &r, fam, f[0], f[1], f[2], f[3], flo + H + 0.5f, want, wide, clampf(rc, 3, 12), prr(&r, 0.7f, 1.2f), true, prr(&r, 0.35f, 0.7f), &out[cnt]);
        if (w <= 0) { c += 1.0f / arc; continue; }
        cnt++; if (nj < SH_PIECES) joints[nj++] = t + 0.5f * w / arc;
        c = t - 0.05f * w / arc;
    }
    // At each joint between two pieces, where their rounded ends leave a seam: a rock lodged high in it,
    // its back set into the wall and its top at the lip, and most often a lower one standing in front, so
    // the face reads as one irregular outcrop rather than a row and no seam shows the cut behind.
    for (int k = 0; k + 1 < nj && cnt + 2 <= cap; k++) {
        if (fabsf(joints[k]) > 0.95f) continue;
        float f[4], flo, H; terra_shoulder_foot(s, joints[k], f); sh_wall_at(m, f[0], f[1], f[2], f[3], s->wall / 3 + 4, &flo, &H);
        if (H < 2.0f) continue;
        float want = H * prr(&r, 0.45f, 0.6f), top = flo + H + 0.5f, x = f[0], z = f[1];
        for (float b = 0; b < s->wall / 3 + 4; b += 0.25f) {   // up the wall to where its middle will be
            x = f[0] - b * f[2]; z = f[1] - b * f[3];
            if (seat_ground(m, m->surface, x, z) >= top - 0.6f * want) break;
        }
        if (sh_rock(p, m, &r, fam, x, z, f[2], f[3], top, want, want * prr(&r, 1.0f, 1.5f), 6, prr(&r, 0.3f, 0.6f), false, prr(&r, 0.5f, 0.9f), &out[cnt]) > 0) cnt++;
        if (pr(&r) < 0.35f) continue;
        want = H * prr(&r, 0.5f, 0.8f);
        if (sh_rock(p, m, &r, fam, f[0], f[1], f[2], f[3], flo + want, want, want * prr(&r, 0.8f, 1.3f), 6, prr(&r, 1.2f, 2.0f), true, prr(&r, 0.5f, 0.9f), &out[cnt]) > 0) cnt++;
    }
    return sh_fill(p, m, s, &r, fam, out, cnt, cap);
}
// The shoulder pieces of one class that stand in tile (tx, tz) (each piece belongs to the tile under it,
// so the colliders stream in with the ground they stand on), keyed from PROP_SHOULDER_CELL up.
static int shoulder_scatter(const PropCatalog* p, const TerraMap* m, int kind, int tx, int tz, PropInstance* out, int cap) {
    float x0 = tx * PROP_TILE, z0 = tz * PROP_TILE, x1 = x0 + PROP_TILE, z1 = z0 + PROP_TILE; int cnt = 0, local = 0;
    for (int k = 0; k < m->nshoulder && cnt < cap; k++) {
        const TerraShoulder* s = &m->shoulder[k]; float R = terra_shoulder_reach(s);
        if (s->x + R < x0 || s->x - R > x1 || s->z + R < z0 || s->z - R > z1) continue;
        PropInstance own[SH_PIECES]; const PropInstance* buf = own; int n;
        if (m->shoulder_props) { buf = m->shoulder_props->piece + m->shoulder_props->start[k]; n = m->shoulder_props->start[k + 1] - m->shoulder_props->start[k]; }
        else n = shoulder_layout(p, m, s, own, SH_PIECES);
        for (int i = 0; i < n && cnt < cap && local < 1024 - PROP_SHOULDER_CELL; i++) {
            if (buf[i].kind != kind || buf[i].x < x0 || buf[i].x >= x1 || buf[i].z < z0 || buf[i].z >= z1) continue;
            out[cnt] = buf[i]; out[cnt++].key = prop_key(m, kind, tx, tz, PROP_SHOULDER_CELL + local++);
        }
    }
    return cnt;
}

PropShoulders* prop_shoulders_prepare(const PropCatalog* c, const TerraMap* m) {
    if (!m->nshoulder) return NULL;
    PropShoulders* ps = malloc(sizeof *ps); int n = m->nshoulder;
    PropInstance* all = malloc(sizeof(PropInstance) * SH_PIECES * (size_t)n); int* cnt = calloc(n, sizeof(int));
    ps->start = malloc(sizeof(int) * (n + 1));
    #pragma omp parallel for schedule(dynamic, 1)
    for (int k = 0; k < n; k++) cnt[k] = shoulder_layout(c, m, &m->shoulder[k], all + (size_t)k * SH_PIECES, SH_PIECES);
    int total = 0;
    for (int k = 0; k < n; k++) { ps->start[k] = total; memmove(all + total, all + (size_t)k * SH_PIECES, sizeof(PropInstance) * cnt[k]); total += cnt[k]; }
    ps->start[n] = total; ps->piece = realloc(all, sizeof(PropInstance) * (total ? total : 1));
    free(cnt);
    return ps;
}
void prop_shoulders_free(PropShoulders* ps) { if (ps) { free(ps->piece); free(ps->start); free(ps); } }

void prop_shoulder_view(const TerraMap* m, const TerraShoulder* s, int side, float eye[3], float at[3]) { sh_view(m, s, side, eye, at); }

// Möller-Trumbore: the ray o + t d hits triangle (a, b, c) at t in (0, tmax).
static bool ray_tri(const float o[3], const float d[3], const float* a, const float* b, const float* c, float tmax) {
    float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    float pv[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    float det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2]; if (fabsf(det) < 1e-12f) return false;
    float inv = 1 / det, tv[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    float u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv; if (u < 0 || u > 1) return false;
    float qv[3] = {tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0]};
    float v = (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]) * inv; if (v < 0 || u + v > 1) return false;
    float t = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
    return t > 1e-4f && t < tmax;
}
float prop_shoulder_coverage(const PropCatalog* c, const TerraMap* m, int k, const float eye[3], float* bare_max) {
    const TerraShoulder* s = &m->shoulder[k];
    PropInstance own[SH_PIECES]; const PropInstance* pc = own; int np;
    if (m->shoulder_props) { pc = m->shoulder_props->piece + m->shoulder_props->start[k]; np = m->shoulder_props->start[k + 1] - m->shoulder_props->start[k]; }
    else np = shoulder_layout(c, m, s, own, SH_PIECES);
    // The pieces' LOD1 triangles in world space, and a bounding sphere each.
    float* tri[SH_PIECES] = {0}; int ntri[SH_PIECES] = {0}; float sph[SH_PIECES][4];
    for (int i = 0; i < np; i++) {
        char path[512]; snprintf(path, sizeof path, PROP_DIR "%s.prop", c->name[pc[i].variant]);
        float* pos; int32_t* idx; int ni, nv = prop_read_lod(path, 1, &pos, &idx, &ni);
        tri[i] = malloc(sizeof(float) * 9 * (size_t)(ni / 3 + 1)); ntri[i] = ni / 3;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (int j = 0; j < ni; j++) {
            float o[3]; sh_rot(pc[i].q, pos + 3 * idx[j], pc[i].scale, o); o[0] += pc[i].x; o[1] += pc[i].y; o[2] += pc[i].z;
            memcpy(tri[i] + 3 * j, o, 12); for (int a = 0; a < 3; a++) { lo[a] = fminf(lo[a], o[a]); hi[a] = fmaxf(hi[a], o[a]); }
        }
        for (int a = 0; a < 3; a++) sph[i][a] = 0.5f * (lo[a] + hi[a]);
        sph[i][3] = 0.5f * sqrtf((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
        free(pos); free(idx); (void)nv;
    }
    // The wall: in columns across the notch, from its foot back to where the cut ends (the planned wall
    // rising at 3:1, plus the rounding of the lip), the ground that rises into the hill steeper than 45 degrees.
    int shown = 0, covered = 0; float worst = 0, reach = s->wall / 3 + 4;
    for (float t = -1.0f; t <= 1.0f; t += 0.04f) {
        float f[4]; terra_shoulder_foot(s, t, f); float run_lo = 1e30f, run_hi = -1e30f; bool wall_seen = false;
        for (float b = -0.5f; b <= reach; b += 0.2f) {
            float x = f[0] - b * f[2], z = f[1] - b * f[3], y = seat_ground(m, m->surface, x, z);
            bool steep = seat_ground(m, m->surface, x - 0.4f * f[2], z - 0.4f * f[3]) - seat_ground(m, m->surface, x + 0.4f * f[2], z + 0.4f * f[3]) > 0.8f;
            if (!steep) { if (wall_seen && b > 1) break; continue; }
            wall_seen = true;
            float d[3] = {x - eye[0], y - eye[1], z - eye[2]}, L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            // Hidden by the ground in front: marched in 0.5 m steps, stopping short of the point.
            bool hidden = false;
            for (float u = 0.5f; u < L - 0.6f && !hidden; u += 0.5f) { float f2 = u / L; hidden = eye[1] + d[1] * f2 < seat_ground(m, m->surface, eye[0] + d[0] * f2, eye[2] + d[2] * f2) - 0.02f; }
            if (hidden) continue;
            bool rock = false;
            for (int i = 0; i < np && !rock; i++) {
                // The sphere first: the closest approach of the ray to its centre, within the segment.
                float w[3] = {sph[i][0] - eye[0], sph[i][1] - eye[1], sph[i][2] - eye[2]}, tt = (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / (L * L);
                tt = clampf(tt, 0, 1); float cx = eye[0] + d[0] * tt - sph[i][0], cy = eye[1] + d[1] * tt - sph[i][1], cz = eye[2] + d[2] * tt - sph[i][2];
                if (cx * cx + cy * cy + cz * cz > sph[i][3] * sph[i][3]) continue;
                for (int j = 0; j < ntri[i] && !rock; j++) rock = ray_tri(eye, d, tri[i] + 9 * j, tri[i] + 9 * j + 3, tri[i] + 9 * j + 6, 1 - 0.05f / L);
            }
            shown++; covered += rock;
            if (!rock) { run_lo = fminf(run_lo, y); run_hi = fmaxf(run_hi, y); }
            else { if (run_hi > run_lo) worst = fmaxf(worst, run_hi - run_lo); run_lo = 1e30f; run_hi = -1e30f; }
        }
        if (run_hi > run_lo) worst = fmaxf(worst, run_hi - run_lo);
    }
    for (int i = 0; i < np; i++) free(tri[i]);
    if (bare_max) *bare_max = worst;
    return shown ? (float)covered / shown : 1;
}

float prop_canopy_at(const TerraMap* m, float x, float z) { return canopy_at(m, cell_of(m, x, z)); }

int prop_catalog_add(PropCatalog* c, const char* name, float radius, float height) {
    if (c->nvar >= PROP_MAX_VARIANTS) return -1;
    int v = c->nvar++;
    snprintf(c->name[v], sizeof c->name[v], "%s", name);
    c->radius[v] = radius; c->height[v] = height;
    int cl[3], nc = prop_classes_for(c->name[v], cl); c->kind[v] = nc ? cl[0] : -1;
    for (int k = 0; k < nc; k++) c->kind_list[cl[k]][c->kind_count[cl[k]]++] = v;
    return v;
}

static int cmp_str(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }

bool prop_catalog_load(PropCatalog* c, const char* dir) {
    memset(c, 0, sizeof *c);
    DIR* d = opendir(dir); if (!d) return false;
    char* names[256]; int nn = 0; struct dirent* e;
    while ((e = readdir(d)) && nn < 256) { size_t L = strlen(e->d_name); if (L > 5 && !strcmp(e->d_name + L - 5, ".prop")) names[nn++] = strdup(e->d_name); }
    closedir(d);
    qsort(names, nn, sizeof(char*), cmp_str);
    for (int k = 0; k < nn; k++) {
        char path[512]; snprintf(path, sizeof path, "%s%s", dir, names[k]);
        FILE* f = fopen(path, "rb"); uint32_t magic = 0, ver = 0, nlod = 0; char mat[32]; float radius = 0, height = -1;
        bool ok = f && fread(&magic, 4, 1, f) && fread(&ver, 4, 1, f) && fread(mat, 32, 1, f) && fread(&radius, 4, 1, f) && fread(&height, 4, 1, f) && fread(&nlod, 4, 1, f);
        if (f) fclose(f);
        if (ok && magic == 0x50525043u && ver == 1 && nlod == 3 && radius > 0 && height >= 0) {
            names[k][strlen(names[k]) - 5] = 0; int v = prop_catalog_add(c, names[k], radius, height);
            if (v >= 0) prop_catalog_base_file(c, v, path);
        }
        free(names[k]);
    }
    return c->nvar > 0;
}

int prop_read_lod(const char* path, int lod, float** pos, int32_t** idx, int* ni) {
    *pos = NULL; *idx = NULL; *ni = 0;
    FILE* f = fopen(path, "rb"); if (!f) return 0;
    uint32_t hdr[2]; char mat[32]; float rh[2]; uint32_t nlod; int nv = 0;
    bool ok = fread(hdr, 4, 2, f) == 2 && fread(mat, 32, 1, f) == 1 && fread(rh, 4, 2, f) == 2 && fread(&nlod, 4, 1, f) == 1 && nlod == 3 && lod >= 0 && lod < 3;
    for (int l = 0; ok && l <= lod; l++) {
        uint32_t v, n; ok = fread(&v, 4, 1, f) && fread(&n, 4, 1, f) && v > 0 && v < (1u << 22) && n < (1u << 24) && n % 3 == 0;
        if (!ok) break;
        if (l < lod) { ok = fseek(f, (long)v * 28 + (long)n * 4, SEEK_CUR) == 0; continue; }
        uint8_t* vb = malloc((size_t)v * 28); *pos = malloc(sizeof(float) * 3 * v); *idx = malloc(4 * (size_t)n + 4);
        ok = vb && *pos && *idx && fread(vb, 28, v, f) == v && fread(*idx, 4, n, f) == n;
        for (uint32_t i = 0; ok && i < v; i++) memcpy(*pos + 3 * i, vb + 28 * i, 12);
        for (uint32_t i = 0; ok && i < n; i++) if ((uint32_t)(*idx)[i] >= v) ok = false;
        free(vb); nv = (int)v; *ni = (int)n;
    }
    fclose(f);
    if (!ok) { free(*pos); free(*idx); *pos = NULL; *idx = NULL; *ni = 0; return 0; }
    return nv;
}

// The rim: vertices in the bottom band (within 8% of the height above the
// lowest one), the outermost per 22.5-degree sector around the centre.
void prop_catalog_set_base(PropCatalog* c, int v, const float* pos, int nv) {
    if (v < 0 || v >= c->nvar || nv <= 0) return;
    float ylo = 1e30f, yhi = -1e30f;
    for (int i = 0; i < nv; i++) { ylo = fminf(ylo, pos[3 * i + 1]); yhi = fmaxf(yhi, pos[3 * i + 1]); }
    float band = ylo + fmaxf(0.08f * (yhi - ylo), 0.01f), best[PROP_BASE_PTS]; int pick[PROP_BASE_PTS];
    for (int k = 0; k < PROP_BASE_PTS; k++) { best[k] = -1; pick[k] = -1; }
    for (int i = 0; i < nv; i++) {
        const float* p = pos + 3 * i; if (p[1] > band) continue;
        float a = atan2f(p[2], p[0]); int k = (int)((a + PPI) / (2 * PPI) * PROP_BASE_PTS); k = k < 0 ? 0 : k >= PROP_BASE_PTS ? PROP_BASE_PTS - 1 : k;
        float r2 = p[0] * p[0] + p[2] * p[2]; if (r2 > best[k]) { best[k] = r2; pick[k] = i; }
    }
    int n = 0;
    for (int k = 0; k < PROP_BASE_PTS; k++) if (pick[k] >= 0) { memcpy(c->base[v][n], pos + 3 * pick[k], 12); n++; }
    c->nbase[v] = n;
}

// ---------------------------------------------------------------- open edges
typedef struct { int32_t q[3]; int i; } WeldKey;
static int weld_cmp(const void* pa, const void* pb) {
    const WeldKey* a = (const WeldKey*)pa; const WeldKey* b = (const WeldKey*)pb;
    for (int k = 0; k < 3; k++) if (a->q[k] != b->q[k]) return a->q[k] < b->q[k] ? -1 : 1;
    return a->i - b->i;
}
static int u64_cmp(const void* pa, const void* pb) { uint64_t a = *(const uint64_t*)pa, b = *(const uint64_t*)pb; return a < b ? -1 : a > b; }
int prop_open_edge(const float* pos, int nv, const int32_t* idx, int ni, float* out, int cap) {
    if (nv <= 0 || ni < 3) return 0;
    // Weld: the converter splits vertices along UV seams, which would read as open edges.
    WeldKey* k = malloc(sizeof(WeldKey) * nv); int* id = malloc(sizeof(int) * nv); int* rep = malloc(sizeof(int) * nv);
    uint64_t* e = malloc(sizeof(uint64_t) * ni); uint8_t* open = calloc(nv, 1);
    if (!k || !id || !rep || !e || !open) { free(k); free(id); free(rep); free(e); free(open); return 0; }
    for (int i = 0; i < nv; i++) { for (int a = 0; a < 3; a++) k[i].q[a] = (int32_t)lroundf(pos[3 * i + a] * 500.0f); k[i].i = i; }
    qsort(k, nv, sizeof(WeldKey), weld_cmp);
    int nw = 0;
    for (int j = 0; j < nv; j++) {
        if (j == 0 || memcmp(k[j].q, k[j - 1].q, sizeof k[j].q)) rep[nw++] = k[j].i;
        id[k[j].i] = nw - 1;
    }
    int ne = 0;
    for (int t = 0; t + 2 < ni; t += 3) for (int s = 0; s < 3; s++) {
        uint32_t a = (uint32_t)id[idx[t + s]], b = (uint32_t)id[idx[t + (s + 1) % 3]];
        if (a == b) continue;
        e[ne++] = a < b ? (uint64_t)a << 32 | b : (uint64_t)b << 32 | a;
    }
    qsort(e, ne, sizeof(uint64_t), u64_cmp);
    for (int j = 0; j < ne;) {   // an edge used by one triangle only is open
        int r = j; while (r < ne && e[r] == e[j]) r++;
        if (r - j == 1) { open[e[j] >> 32] = 1; open[e[j] & 0xffffffffu] = 1; }
        j = r;
    }
    int n = 0;
    for (int w = 0; w < nw && n < cap; w++) if (open[w]) { memcpy(out + 3 * n, pos + 3 * rep[w], 12); n++; }
    free(k); free(id); free(rep); free(e); free(open);
    return n;
}

// Bounds, the support points in 26 directions (the cube's faces, edges and corners: the farthest vertex
// each way, which is what turning and seating a closed rock needs) and whether the scan is closed (no open
// edge at all once its seams are welded; prop_open_edge).
void prop_catalog_set_shape(PropCatalog* c, int v, const float* pos, int nv, const int32_t* idx, int ni) {
    if (v < 0 || v >= c->nvar || nv <= 0) return;
    float* lo = c->lo[v]; float* hi = c->hi[v];
    for (int a = 0; a < 3; a++) { lo[a] = 1e30f; hi[a] = -1e30f; }
    for (int i = 0; i < nv; i++) for (int a = 0; a < 3; a++) { lo[a] = fminf(lo[a], pos[3 * i + a]); hi[a] = fmaxf(hi[a], pos[3 * i + a]); }
    int k = 0;
    for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        if (!dx && !dy && !dz) continue;
        int bi = 0; float bd = -1e30f;
        for (int i = 0; i < nv; i++) { float d = dx * pos[3 * i] + dy * pos[3 * i + 1] + dz * pos[3 * i + 2]; if (d > bd) { bd = d; bi = i; } }
        memcpy(c->ext[v][k++], pos + 3 * bi, 12);
    }
    float* e = malloc(sizeof(float) * 3 * nv);
    c->closed[v] = e && prop_open_edge(pos, nv, idx, ni, e, nv) == 0;
    free(e);
}

bool prop_catalog_base_file(PropCatalog* c, int v, const char* path) {
    float* pos; int32_t* idx; int ni, nv = prop_read_lod(path, 0, &pos, &idx, &ni);
    if (nv) { prop_catalog_set_base(c, v, pos, nv); prop_catalog_set_shape(c, v, pos, nv, idx, ni); }
    free(pos); free(idx);
    return nv > 0;
}
