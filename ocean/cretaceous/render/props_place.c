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
static float seat_height(const PropCatalog* p, const TerraMap* m, int vi, float x, float z, float sc, const float q[4]) {
    float y = 1e30f;
    for (int k = 0; k < p->nbase[vi]; k++) {
        float b[3] = {p->base[vi][k][0] * sc, p->base[vi][k][1] * sc, p->base[vi][k][2] * sc}, r[3];
        quat_rot(q, b, r);
        y = fminf(y, terra_height(m, x + r[0], z + r[2]) - r[1]);
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

// One candidate per lattice cell; deterministic per (tile, class).
int prop_scatter(const PropCatalog* p, const TerraMap* m, int kind, int tx, int tz, PropInstance* out, int cap) {
    const PropClass* C = &PROP_CLASS[kind];
    if (!p->kind_count[kind]) return 0;
    float sp = C->spacing; int steps = sp >= PROP_TILE ? 1 : (int)(PROP_TILE / sp);
    float cell = PROP_TILE / steps, chance = sp > PROP_TILE ? (PROP_TILE * PROP_TILE) / (sp * sp) : 1.0f;
    bool seated = kind != PROP_COAST && kind != PROP_CLIFF;   // the big set pieces keep the centre sink
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
        // Orientation: yaw, a random tilt, then align up toward the terrain
        // normal. Small rocks lie with the slope; big boulders stand nearer
        // upright (and are refused where the slope would bury them).
        float align = C->align;
        if (kind == PROP_BOULDER || kind == PROP_STONE) align = 0.95f - 0.45f * sstep(0.5f, 1.6f, target);
        float qy[4], qt[4], qa[4], t1[4], q[4];
        quat_axis(0, 1, 0, prr(&r, 0, 2 * PPI), qy);
        float ta = prr(&r, 0, 2 * PPI); quat_axis(cosf(ta), 0, sinf(ta), prr(&r, -C->tilt, C->tilt), qt);
        float ux = n[0] * align, uy = 1 - align + n[1] * align, uz = n[2] * align, ul = sqrtf(ux * ux + uy * uy + uz * uz);
        ux /= ul; uy /= ul; uz /= ul;
        quat_axis(uz, 0, -ux, acosf(clampf(uy, -1, 1)), qa);   // (0,1,0) x u = (uz, 0, -ux)
        quat_mul(qt, qy, t1); quat_mul(qa, t1, q);
        float hh = p->height[vi] * sc, y;
        if (seated && p->nbase[vi]) {
            float ys = seat_height(p, m, vi, x, z, sc, q);
            if (h - ys > MAX_BURY[kind] * hh + 0.05f) continue;   // a ridge, spur or gully it cannot sit on
            y = ys - (0.03f + 0.02f * hh) - EMBED[kind] * hh;
        } else y = h - (C->sink * hh + fminf(slope, 1.0f) * p->radius[vi] * sc * 0.2f * (1 - C->align * 0.7f));
        float moss = clampf(1.6f * splat(m, ci, TERRA_MAT_MOSS) + 0.5f * canopy_at(m, ci) + 0.3f * splat(m, ci, TERRA_MAT_FERN) - 0.6f * splat(m, ci, TERRA_MAT_SAND), 0, 1);
        if (kind == PROP_DRIFTWOOD || kind == PROP_COAST) moss = 0;
        if (kind == PROP_STONE || kind == PROP_DEBRIS) moss *= 0.5f;
        float tint = kind == PROP_DRIFTWOOD ? 1.0f : 0.0f;
        out[cnt++] = (PropInstance){x, y, z, sc, {q[0], q[1], q[2], q[3]}, moss * prr(&r, 0.6f, 1.0f), tint, (uint16_t)vi, (uint16_t)kind,
                                    prop_key(m, kind, tx, tz, j * steps + i)};
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

bool prop_catalog_base_file(PropCatalog* c, int v, const char* path) {
    float* pos; int32_t* idx; int ni, nv = prop_read_lod(path, 0, &pos, &idx, &ni);
    if (nv) prop_catalog_set_base(c, v, pos, nv);
    free(pos); free(idx);
    return nv > 0;
}
