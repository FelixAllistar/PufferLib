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
    if (m->nshoulder && (kind == PROP_CLIFF || kind == PROP_OUTCROP || kind == PROP_COAST)) cnt += shoulder_scatter(p, m, kind, tx, tz, out + cnt, cap - cnt);
    return cnt;
}

// ---------------------------------------------------------------- rock shoulders
// The bake cut each shoulder's notch (terra.h TerraShoulder); its rocks are laid out here from the
// notch alone, so the renderer and the colliders (world.c) place the same ones. Rock faces (cliff
// class) stand side by side along the curved wall in one or two rows, overlapping by about a third,
// each turned to look out of the cut and leaning back as the wall does (wall_face). Each is sunk until
// its whole open edge (the shells are open at the back and round their sides) lies under the drawn
// ground, at the distance from the wall that leaves the most of its face in sight; a face that shows
// less than a third is left out. The apron at the wall's foot gets a few mossy outcrops (outcrop
// class) and low slabs (coast class, the small ones). Up to SH_PIECES of each class a shoulder.
#define SH_PIECES 48
static void sh_rot(const float q[4], const float* p, float sc, float o[3]) { float b[3] = {p[0] * sc, p[1] * sc, p[2] * sc}; quat_rot(q, b, o); }
// Origin height that puts every open-edge point (or, for a closed scan, the bottom rim) 0.1 m under the ground.
static float rim_seat(const PropCatalog* p, const TerraMap* m, int vi, float x, float z, float sc, const float q[4]) {
    if (!p->nrim[vi]) return seat_height(p, m, m->surface, vi, x, z, sc, q) - 0.1f;
    float y = 1e30f, o[3];
    for (int k = 0; k < p->nrim[vi]; k++) { sh_rot(q, p->rim[vi][k], sc, o); y = fminf(y, seat_ground(m, m->surface, x + o[0], z + o[2]) - o[1]); }
    return y - 0.1f;
}
// The same on every open vertex (prop_catalog_set_rim's edge): slower, for a final seat.
static float edge_seat(const PropCatalog* p, const TerraMap* m, int vi, float x, float z, float sc, const float q[4]) {
    if (!p->nedge[vi]) return rim_seat(p, m, vi, x, z, sc, q);
    float y = 1e30f, o[3];
    for (int k = 0; k < p->nedge[vi]; k++) { sh_rot(q, p->edge[vi][k], sc, o); y = fminf(y, seat_ground(m, m->surface, x + o[0], z + o[2]) - o[1]); }
    return y - 0.05f;
}
// Share of the front points standing 0.25 m or more clear of the ground.
static float front_shown(const PropCatalog* p, const TerraMap* m, int vi, float x, float y, float z, float sc, const float q[4]) {
    int shown = 0; float o[3];
    for (int k = 0; k < p->nfront[vi]; k++) { sh_rot(q, p->front[vi][k], sc, o); shown += y + o[1] > seat_ground(m, m->surface, x + o[0], z + o[2]) + 0.25f; }
    return p->nfront[vi] ? (float)shown / p->nfront[vi] : 0;
}
static void sh_quat(float yaw, float lean, float o[4]) { float qy[4], ql[4]; quat_axis(0, 1, 0, yaw, qy); quat_axis(1, 0, 0, lean, ql); quat_mul(qy, ql, o); }
// The wall where it rises from (x, z), looking back along -(nx, nz): its base height, how high it rises
// before the slope eases (the lip), and how far back it leans (radians from upright).
static void sh_wall_at(const TerraMap* m, float x, float z, float nx, float nz, float* base, float* height, float* lean) {
    float flo = seat_ground(m, m->surface, x + 3 * nx, z + 3 * nz), lip = flo;
    for (float b = 0; b < 30; b += 1) {
        float y0 = seat_ground(m, m->surface, x - b * nx, z - b * nz), y1 = seat_ground(m, m->surface, x - (b + 1) * nx, z - (b + 1) * nz);
        lip = y1; if (y1 - y0 < 0.9f && y0 > flo + 1) { lip = y0; break; }
    }
    float H = lip - flo, b25 = -1e9f, b75 = -1e9f;
    for (float b = -4; b < 30 && b75 < -1e8f; b += 0.5f) {
        float y = seat_ground(m, m->surface, x - b * nx, z - b * nz) - flo;
        if (b25 < -1e8f && y >= 0.25f * H) b25 = b;
        if (b25 > -1e8f && y >= 0.75f * H) b75 = b;
    }
    *base = flo; *height = H; *lean = b75 > b25 + 0.1f ? atan2f(b75 - b25, 0.5f * H) : 0.35f;
}
// One rock face standing at (x, z) on a wall H m high: every cliff scan at three sizes (up to a little
// under the lip, so the hill rolls over its top), each turned so its face lies along the wall (leaning
// back as the wall does, whatever angle the scan lay at: most lie at 45 degrees) with its open back
// into it, at 12 distances out from the wall. The seat that shows the most face wins (the slot's own
// pick of scan a little favoured, for variety); none if no seat shows 30%. Returns its width, or 0.
static float wall_face(const PropCatalog* p, const TerraMap* m, PRng* r, float x0, float z0, float nx, float nz, float flo, float H, float lean, float wmax, PropInstance* out) {
    int nk = p->kind_count[PROP_CLIFF], fav = (int)(pr(r) * nk) % nk;
    float yaw = atan2f(nx, nz) + prr(r, -0.2f, 0.2f), tilt = lean + prr(r, -0.05f, 0.1f), size = prr(r, 0.9f, 1.0f), moss = prr(r, 0.25f, 0.6f);
    int bv = -1; float best = 0, bx = 0, by = 0, bz = 0, bsc = 0, bq[4] = {0, 0, 0, 1};
    for (int k = 0; k < nk; k++) for (int f2 = 0; f2 < 3; f2++) {
        int vi = p->kind_list[PROP_CLIFF][k]; const float* lo = p->lo[vi]; const float* hi = p->hi[vi];
        float sc = H * size * (0.6f + 0.2f * f2) / fmaxf(hi[1] - lo[1], 0.1f);
        if ((hi[0] - lo[0]) * sc > wmax) continue;
        float q[4]; sh_quat(yaw, p->lean[vi] - tilt, q);
        float D = (hi[2] - lo[2]) * sc;
        for (int j = 0; j < 12; j++) {
            float d = (0.9f - 0.1f * j) * D - hi[2] * sc, x = x0 + nx * d, z = z0 + nz * d;
            float y = rim_seat(p, m, vi, x, z, sc, q);
            if (y + hi[1] * sc * cosf(p->lean[vi] - tilt) < flo + 0.5f * H) continue;   // too deep: it would not stand up the wall
            float shown = front_shown(p, m, vi, x, y, z, sc, q) + (k == fav ? 0.08f : 0);
            if (shown > best + 0.02f) { best = shown; bv = vi; bx = x; by = y; bz = z; bsc = sc; memcpy(bq, q, sizeof q); }
        }
    }
    if (bv < 0 || best < 0.3f) return 0;
    // The search seats on the sampled rim; the chosen one sits on the whole open edge, and must still show.
    by = fminf(by, edge_seat(p, m, bv, bx, bz, bsc, bq));
    if (front_shown(p, m, bv, bx, by, bz, bsc, bq) < 0.25f) return 0;
    *out = (PropInstance){bx, by, bz, bsc, {bq[0], bq[1], bq[2], bq[3]}, moss, 0, (uint16_t)bv, PROP_CLIFF, 0};
    return (p->hi[bv][0] - p->lo[bv][0]) * bsc;
}
static int shoulder_layout(const PropCatalog* p, const TerraMap* m, const TerraShoulder* s, int kind, PropInstance* out, int cap) {
    int nk = p->kind_count[kind], cnt = 0; if (!nk) return 0;
    PRng r = {hashu(s->seed ^ (uint32_t)(kind + 1) * 0x9e3779b9u)};
    float L = 6.0f + 0.3f * s->wall;   // the apron's length (terra.c sh_apron_len)
    if (kind == PROP_CLIFF) {
        // Two rows: along the wall's foot, and where the wall stands 7 m or more, a second staggered
        // half a face along, from where it has risen 40% of its height, so the rock climbs to the lip.
        for (int row = 0; row < 2; row++)
        for (float t = -1.0f + (0.15f + 0.1f * row) * pr(&r) + 0.08f * row; t < 1.0f && cnt < cap;) {
            float f[4]; terra_shoulder_foot(s, t, f);
            float e = 0.02f, g = (terra_shoulder_wall(s, t + e) - terra_shoulder_wall(s, t - e)) / (2 * e), arc = sqrtf(s->half * s->half + g * g);
            float flo, H, lean; sh_wall_at(m, f[0], f[1], f[2], f[3], &flo, &H, &lean);
            if (H < (row ? 7.0f : 1.6f)) { t += 2.0f / arc; continue; }
            float x = f[0], z = f[1];
            if (row) {   // up the wall to 40% of its height
                for (float b = 0; b < 20; b += 0.5f) if (seat_ground(m, m->surface, f[0] - b * f[2], f[1] - b * f[3]) >= flo + 0.4f * H) { x = f[0] - b * f[2]; z = f[1] - b * f[3]; break; }
                flo += 0.4f * H; H *= 0.6f;
            }
            // No wider than the curve's radius here: the bite's sides bend toward the viewer, and a wide
            // flat face would stand out in front of them.
            float g2 = (terra_shoulder_wall(s, t + e) - 2 * terra_shoulder_wall(s, t) + terra_shoulder_wall(s, t - e)) / (e * e * s->half * s->half);
            float rc = powf(1 + g * g / (s->half * s->half), 1.5f) / fmaxf(fabsf(g2), 1e-4f);
            float w = wall_face(p, m, &r, x, z, f[2], f[3], flo, H, lean, clampf(rc, 4, 30), &out[cnt]);
            if (w > 0) cnt++;
            t += fmaxf(0.62f * (w > 0 ? w : 4), 2.0f) / arc;
        }
    } else if (kind == PROP_OUTCROP || kind == PROP_COAST) {
        int want = kind == PROP_OUTCROP ? 2 + (int)(pr(&r) * 3) : (int)(pr(&r) * 2.5f);
        for (int k = 0; k < want * 3 && cnt < want && cnt < cap; k++) {
            int vi = p->kind_list[kind][(int)(pr(&r) * nk) % nk];
            if (kind == PROP_COAST && p->radius[vi] > 8) continue;   // the coast's low slabs only, not its long cliffs
            float t = prr(&r, -0.8f, 0.8f), f[4]; terra_shoulder_foot(s, t, f);
            float out_m = prr(&r, 1.5f, L), x = f[0] + f[2] * out_m, z = f[1] + f[3] * out_m;
            float target = kind == PROP_OUTCROP ? prr(&r, 1.0f, 2.4f) : prr(&r, 2.0f, 4.0f), sc = target / fmaxf(p->radius[vi], 0.01f);
            float q[4]; sh_quat(prr(&r, 0, 2 * PPI), prr(&r, -0.15f, 0.15f), q);
            float y = edge_seat(p, m, vi, x, z, sc, q) - 0.06f * p->height[vi] * sc;
            if (seat_ground(m, m->surface, x, z) - y > 0.6f * p->height[vi] * sc) continue;   // sunk too far: the apron is too steep here
            out[cnt++] = (PropInstance){x, y, z, sc, {q[0], q[1], q[2], q[3]}, prr(&r, 0.4f, 0.9f), 0, (uint16_t)vi, (uint16_t)kind, 0};
        }
    }
    return cnt;
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
        else n = shoulder_layout(p, m, s, kind, own, SH_PIECES);
        for (int i = 0; i < n && cnt < cap && local < 1024 - PROP_SHOULDER_CELL; i++) {
            if (buf[i].kind != kind || buf[i].x < x0 || buf[i].x >= x1 || buf[i].z < z0 || buf[i].z >= z1) continue;
            out[cnt] = buf[i]; out[cnt++].key = prop_key(m, kind, tx, tz, PROP_SHOULDER_CELL + local++);
        }
    }
    return cnt;
}

PropShoulders* prop_shoulders_prepare(const PropCatalog* c, const TerraMap* m) {
    if (!m->nshoulder) return NULL;
    static const int KINDS[3] = {PROP_CLIFF, PROP_OUTCROP, PROP_COAST};
    PropShoulders* ps = malloc(sizeof *ps); int n = m->nshoulder;
    PropInstance* all = malloc(sizeof(PropInstance) * 3 * SH_PIECES * (size_t)n); int* cnt = calloc(n, sizeof(int));
    ps->start = malloc(sizeof(int) * (n + 1));
    #pragma omp parallel for schedule(dynamic, 1)
    for (int k = 0; k < n; k++)
        for (int q = 0; q < 3; q++) cnt[k] += shoulder_layout(c, m, &m->shoulder[k], KINDS[q], all + (size_t)k * 3 * SH_PIECES + cnt[k], SH_PIECES);
    int total = 0;
    for (int k = 0; k < n; k++) { ps->start[k] = total; memmove(all + total, all + (size_t)k * 3 * SH_PIECES, sizeof(PropInstance) * cnt[k]); total += cnt[k]; }
    ps->start[n] = total; ps->piece = realloc(all, sizeof(PropInstance) * (total ? total : 1));
    free(cnt);
    return ps;
}
void prop_shoulders_free(PropShoulders* ps) { if (ps) { free(ps->piece); free(ps->start); free(ps); } }

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

// The open edge sampled as the farthest open vertex from the middle in each of 32 sectors of the
// front view (x, y) and 16 of the top view (x, z), so a shell's side and top edges and a rock's
// bottom rim are all held; the foremost vertex of each of 6 x 4 cells of the front view; the bounds.
void prop_catalog_set_rim(PropCatalog* c, int v, const float* pos, int nv, const int32_t* idx, int ni) {
    if (v < 0 || v >= c->nvar || nv <= 0) return;
    float* lo = c->lo[v]; float* hi = c->hi[v];
    for (int a = 0; a < 3; a++) { lo[a] = 1e30f; hi[a] = -1e30f; }
    for (int i = 0; i < nv; i++) for (int a = 0; a < 3; a++) { lo[a] = fminf(lo[a], pos[3 * i + a]); hi[a] = fmaxf(hi[a], pos[3 * i + a]); }
    float mid[3] = {0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2])};
    float* e = malloc(sizeof(float) * 3 * nv); int ne = e ? prop_open_edge(pos, nv, idx, ni, e, nv) : 0;
    int step = (ne + PROP_EDGE_PTS - 1) / PROP_EDGE_PTS; c->nedge[v] = 0;
    for (int i = 0; i < ne; i += step > 0 ? step : 1) memcpy(c->edge[v][c->nedge[v]++], e + 3 * i, 12);
    enum { SF = 32, ST = PROP_RIM_PTS - SF };
    int pick[PROP_RIM_PTS]; float best[PROP_RIM_PTS];
    for (int s = 0; s < PROP_RIM_PTS; s++) { pick[s] = -1; best[s] = -1; }
    for (int i = 0; i < ne; i++) {
        const float* q = e + 3 * i; float dx = q[0] - mid[0], dy = q[1] - mid[1], dz = q[2] - mid[2];
        int s = (int)((atan2f(dy, dx) + PPI) / (2 * PPI) * SF); s = s < 0 ? 0 : s >= SF ? SF - 1 : s;
        if (dx * dx + dy * dy > best[s]) { best[s] = dx * dx + dy * dy; pick[s] = i; }
        s = (int)((atan2f(dz, dx) + PPI) / (2 * PPI) * ST); s = SF + (s < 0 ? 0 : s >= ST ? ST - 1 : s);
        if (dx * dx + dz * dz > best[s]) { best[s] = dx * dx + dz * dz; pick[s] = i; }
    }
    int n = 0;
    for (int s = 0; s < PROP_RIM_PTS; s++) {
        bool dup = pick[s] < 0;
        for (int t = 0; t < s && !dup; t++) dup = pick[t] == pick[s];
        if (!dup) { memcpy(c->rim[v][n], e + 3 * pick[s], 12); n++; }
    }
    c->nrim[v] = n; free(e);
    int fp[PROP_FRONT_PTS]; float fz[PROP_FRONT_PTS];
    for (int s = 0; s < PROP_FRONT_PTS; s++) { fp[s] = -1; fz[s] = -1e30f; }
    for (int i = 0; i < nv; i++) {
        const float* q = pos + 3 * i;
        int bx = (int)((q[0] - lo[0]) / fmaxf(hi[0] - lo[0], 1e-6f) * 6), by = (int)((q[1] - lo[1]) / fmaxf(hi[1] - lo[1], 1e-6f) * 4);
        int s = (by < 0 ? 0 : by > 3 ? 3 : by) * 6 + (bx < 0 ? 0 : bx > 5 ? 5 : bx);
        if (q[2] > fz[s]) { fz[s] = q[2]; fp[s] = i; }
    }
    n = 0; float zb = 0, yb = 0, zt = 0, yt = 0; int nb = 0, nt = 0;
    for (int s = 0; s < PROP_FRONT_PTS; s++) if (fp[s] >= 0) {
        const float* q = pos + 3 * fp[s]; memcpy(c->front[v][n], q, 12); n++;
        if (s < 6) { zb += q[2]; yb += q[1]; nb++; } else if (s >= 18) { zt += q[2]; yt += q[1]; nt++; }
    }
    c->nfront[v] = n;
    c->lean[v] = nb && nt ? atan2f(zb / nb - zt / nt, fmaxf(yt / nt - yb / nb, 1e-3f)) : 0;
}

bool prop_catalog_base_file(PropCatalog* c, int v, const char* path) {
    float* pos; int32_t* idx; int ni, nv = prop_read_lod(path, 0, &pos, &idx, &ni);
    if (nv) { prop_catalog_set_base(c, v, pos, nv); prop_catalog_set_rim(c, v, pos, nv, idx, ni); }
    free(pos); free(idx);
    return nv > 0;
}
