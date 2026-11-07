#include "props_place.h"
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
    case PROP_LOG: return 0.55f * can * (1 - sstep(0.35f, 0.6f, slope));
    case PROP_STUMP: return 0.35f * can * (1 - sstep(0.5f, 0.8f, slope));
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

// One candidate per lattice cell; deterministic per (tile, class).
int prop_scatter(const PropCatalog* p, const TerraMap* m, int kind, int tx, int tz, PropInstance* out, int cap) {
    const PropClass* C = &PROP_CLASS[kind];
    if (!p->kind_count[kind]) return 0;
    float sp = C->spacing; int steps = sp >= PROP_TILE ? 1 : (int)(PROP_TILE / sp);
    float cell = PROP_TILE / steps, chance = sp > PROP_TILE ? (PROP_TILE * PROP_TILE) / (sp * sp) : 1.0f;
    int cnt = 0;
    for (int j = 0; j < steps; j++) for (int i = 0; i < steps; i++) {
        PRng r = {hashu((uint32_t)(tx * 4099 + i) * 2654435761u ^ (uint32_t)(tz * 3571 + j) * 40503u ^ (uint32_t)(kind + 31) * 2246822519u)};
        float x = tx * PROP_TILE + (i + pr(&r)) * cell, z = tz * PROP_TILE + (j + pr(&r)) * cell;
        float n[3]; terra_normal(m, x, z, n);
        float slope = sqrtf(fmaxf(1 - n[1] * n[1], 0)) / fmaxf(n[1], 0.05f);
        float h = terra_height(m, x, z);
        // Clumping: a slow noise-free hash field makes boulder fields and gaps.
        float clump = 0.5f + 0.5f * sinf(x * 0.013f + 1.7f * sinf(z * 0.009f)) * sinf(z * 0.011f + 1.3f * sinf(x * 0.007f));
        float d = density(m, kind, x, z, slope, h) * chance * (kind == PROP_BOULDER || kind == PROP_STONE ? 0.4f + 1.2f * clump : 1.0f);
        if (pr(&r) >= d) continue;
        size_t ci = cell_of(m, x, z);
        float w = terra_water(m, x, z);
        bool wet = m->water_kind[ci] != TERRA_WATER_NONE && w > h + 0.05f;
        if (kind == PROP_COAST) { if (h < 0.2f) continue; }
        else if (h < 0.3f || m->water_kind[ci] == TERRA_WATER_SEA) continue;
        else if (wet && w - h > ((kind == PROP_BOULDER || kind == PROP_STONE) ? 0.7f : 0.25f)) continue;   // rocks may sit in streams
        if (cnt == cap) return cnt;
        int vi = p->kind_list[kind][(int)(pr(&r) * p->kind_count[kind]) % p->kind_count[kind]];
        float target = prr(&r, C->r_lo, C->r_hi); target = C->r_lo + (target - C->r_lo) * pr(&r);   // bias small
        float sc = target / fmaxf(p->radius[vi], 0.01f);
        // Orientation: yaw, a random tilt, then align up toward the terrain normal.
        float qy[4], qt[4], qa[4], t1[4], q[4];
        quat_axis(0, 1, 0, prr(&r, 0, 2 * PPI), qy);
        float ta = prr(&r, 0, 2 * PPI); quat_axis(cosf(ta), 0, sinf(ta), prr(&r, -C->tilt, C->tilt), qt);
        float ux = n[0] * C->align, uy = 1 - C->align + n[1] * C->align, uz = n[2] * C->align, ul = sqrtf(ux * ux + uy * uy + uz * uz);
        ux /= ul; uy /= ul; uz /= ul;
        quat_axis(uz, 0, -ux, acosf(clampf(uy, -1, 1)), qa);   // (0,1,0) x u = (uz, 0, -ux)
        quat_mul(qt, qy, t1); quat_mul(qa, t1, q);
        float sink = C->sink * p->height[vi] * sc + fminf(slope, 1.0f) * p->radius[vi] * sc * 0.2f * (1 - C->align * 0.7f);
        float moss = clampf(1.6f * splat(m, ci, TERRA_MAT_MOSS) + 0.5f * canopy_at(m, ci) + 0.3f * splat(m, ci, TERRA_MAT_FERN) - 0.6f * splat(m, ci, TERRA_MAT_SAND), 0, 1);
        if (kind == PROP_DRIFTWOOD || kind == PROP_COAST) moss = 0;
        if (kind == PROP_STONE || kind == PROP_DEBRIS) moss *= 0.5f;
        float tint = kind == PROP_DRIFTWOOD ? 1.0f : 0.0f;
        out[cnt++] = (PropInstance){x, h - sink, z, sc, {q[0], q[1], q[2], q[3]}, moss * prr(&r, 0.6f, 1.0f), tint, (uint16_t)vi, (uint16_t)kind};
    }
    return cnt;
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
            names[k][strlen(names[k]) - 5] = 0; prop_catalog_add(c, names[k], radius, height);
        }
        free(names[k]);
    }
    return c->nvar > 0;
}
