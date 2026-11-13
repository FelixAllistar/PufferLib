// Flora probe: finds a review viewpoint in each kind of country from the
// world data (beach, river bank, wet valley floor, lowland and upland forest,
// dry ridge, clearing, pond or wetland) and prints them as a CRET_SHOT_CAM
// string, so captures before and after a change come from the same places.
//   make -C ocean/cretaceous floraprobe && ./build/cretaceous/floraprobe [--seed N]
// Run from the repository root (the world cache is relative to it).
#include "../world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }

// Per-cell fields the site scores read, at a coarse step so the search is quick.
typedef struct { const TerraMap* m; int n; } Map;
static float veg(const TerraMap* m, int x, int z, int k) { return m->veg[((size_t)z * m->n + x) * TERRA_VEG_COUNT + k] / 255.0f; }
static float mat(const TerraMap* m, int x, int z, int k) { return m->splat[((size_t)z * m->n + x) * TERRA_MAT_COUNT + k] / 255.0f; }
static float hgt(const TerraMap* m, int x, int z) { return m->height[(size_t)z * m->n + x]; }
static int wk(const TerraMap* m, int x, int z) { return m->water_kind[(size_t)z * m->n + x]; }
static float slope(const TerraMap* m, int x, int z) {
    float gx = (hgt(m, x + 1, z) - hgt(m, x - 1, z)) / (2 * m->cell), gz = (hgt(m, x, z + 1) - hgt(m, x, z - 1)) / (2 * m->cell);
    return sqrtf(gx * gx + gz * gz);
}
// Mean of a field over a square of radius r cells (step s).
typedef float (*Field)(const TerraMap*, int, int, int);
static float vegf(const TerraMap* m, int x, int z, int k) { return veg(m, x, z, k); }
static float matf(const TerraMap* m, int x, int z, int k) { return mat(m, x, z, k); }
static float waterf(const TerraMap* m, int x, int z, int k) { return wk(m, x, z) == k ? 1.0f : 0.0f; }
static float area(const TerraMap* m, int x, int z, int r, Field f, int k) {
    float s = 0; int c = 0;
    for (int dz = -r; dz <= r; dz += 2) for (int dx = -r; dx <= r; dx += 2) {
        int xx = x + dx, zz = z + dz; if (xx < 1 || zz < 1 || xx >= m->n - 1 || zz >= m->n - 1) continue;
        s += f(m, xx, zz, k); c++;
    }
    return c ? s / c : 0;
}
// Direction (radians, the capture's yaw convention: atan2(dz, dx)) toward the
// mean of a field around (x, z) within r cells.
static float toward(const TerraMap* m, int x, int z, int r, Field f, int k) {
    float sx = 0, sz = 0;
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        int xx = x + dx, zz = z + dz; if (xx < 1 || zz < 1 || xx >= m->n - 1 || zz >= m->n - 1) continue;
        float w = f(m, xx, zz, k); sx += w * dx; sz += w * dz;
    }
    return atan2f(sz, sx);
}

typedef struct { const char* name; float above, pitch; } Site;
enum { S_BEACH, S_RIVER, S_VALLEY, S_LOWLAND, S_UPLAND, S_RIDGE, S_CLEARING, S_WETLAND, S_COUNT };
static const Site SITES[S_COUNT] = {
    {"fl_beach", 1.8f, -0.04f}, {"fl_river", 1.8f, -0.08f}, {"fl_valley", 1.8f, -0.02f}, {"fl_lowland", 1.8f, 0.0f},
    {"fl_upland", 1.8f, 0.0f}, {"fl_ridge", 1.8f, -0.06f}, {"fl_clearing", 1.8f, -0.02f}, {"fl_wetland", 1.8f, -0.06f}};

// How well cell (x, z) shows site s (higher is better, <= 0 never).
static float score(const TerraMap* m, int s, int x, int z) {
    float h = hgt(m, x, z), sl = slope(m, x, z);
    if (wk(m, x, z) != TERRA_WATER_NONE || h < 0.5f || sl > 0.35f) return 0;
    float con = veg(m, x, z, TERRA_VEG_CONIFER), brd = veg(m, x, z, TERRA_VEG_BROADLEAF);
    switch (s) {
    case S_BEACH:   // sand underfoot, the sea close, palms behind
        return mat(m, x, z, TERRA_MAT_SAND) * area(m, x, z, 10, waterf, TERRA_WATER_SEA) * (0.5f + area(m, x, z, 16, vegf, TERRA_VEG_PALM)) * (1 - sstep(4, 8, h));
    case S_RIVER: { // on a bank of a big river, with plants along it
        float riv = area(m, x, z, 6, waterf, TERRA_WATER_RIVER);
        return riv * sstep(0.15f, 0.4f, riv) * (1 - sstep(0.5f, 0.8f, riv)) * (0.3f + area(m, x, z, 6, vegf, TERRA_VEG_HORSETAIL)) * (h > 10 ? 1.0f : 0.3f);
    }
    case S_VALLEY:  // tree ferns and horsetails on a flat valley floor, no open water at hand
        return area(m, x, z, 8, vegf, TERRA_VEG_TREEFERN) * (0.5f + area(m, x, z, 8, vegf, TERRA_VEG_HORSETAIL)) * (1 - sstep(0.08f, 0.2f, sl))
             * (1 - area(m, x, z, 6, waterf, TERRA_WATER_RIVER)) * (h > 15 ? 1.0f : 0.2f);
    case S_LOWLAND: // broadleaf forest under 80 m, not at a river
        return area(m, x, z, 10, vegf, TERRA_VEG_BROADLEAF) * (1 - sstep(60, 90, h)) * (1 - sstep(0.15f, 0.3f, sl)) * (1 - area(m, x, z, 8, vegf, TERRA_VEG_HORSETAIL)) * (h > 8 ? 1.0f : 0.2f);
    case S_UPLAND:  // conifer forest high up
        return area(m, x, z, 10, vegf, TERRA_VEG_CONIFER) * sstep(250, 350, h) * (1 - sstep(0.2f, 0.33f, sl));
    case S_RIDGE:   // dry, open, high ground with cycads
        return area(m, x, z, 8, vegf, TERRA_VEG_CYCAD) * sstep(150, 300, h) * (1 - area(m, x, z, 8, vegf, TERRA_VEG_BROADLEAF)) * (sl > 0.05f ? 1.0f : 0.5f);
    case S_CLEARING: { // open fern ground ringed by forest
        float here = con + brd, ring = 0.5f * (area(m, x, z, 14, vegf, TERRA_VEG_CONIFER) + area(m, x, z, 14, vegf, TERRA_VEG_BROADLEAF));
        // The baked layers leave almost no open ground inland, so this takes
        // the thinnest canopy that is ringed by forest.
        float inner = area(m, x, z, 3, vegf, TERRA_VEG_CONIFER) + area(m, x, z, 3, vegf, TERRA_VEG_BROADLEAF);
        return (1.2f - inner) * (1.2f - here) * ring * (0.3f + mat(m, x, z, TERRA_MAT_FERN)) * (1 - area(m, x, z, 8, waterf, TERRA_WATER_SEA))
             * (1 - area(m, x, z, 6, waterf, TERRA_WATER_RIVER)) * (h > 10 ? 1.0f : 0.1f) * (1 - sstep(0.15f, 0.3f, sl));
    }
    case S_WETLAND: { // a lake shore, or (the island may have no lakes) a flat, muddy, slow reach of river
        float lake = area(m, x, z, 8, waterf, TERRA_WATER_LAKE), riv = area(m, x, z, 8, waterf, TERRA_WATER_RIVER);
        float still = lake > 0 ? lake * (1 - sstep(0.55f, 0.8f, lake)) * 2 : riv * (1 - sstep(0.4f, 0.7f, riv)) * (1 - sstep(0.03f, 0.08f, sl));
        return still * (0.2f + area(m, x, z, 6, matf, TERRA_MAT_MUD)) * (0.3f + area(m, x, z, 8, vegf, TERRA_VEG_HORSETAIL)) * (h > 3 ? 1.0f : 0.3f);
    }
    }
    return 0;
}

int main(int argc, char** argv) {
    uint32_t seed = 1;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)atoi(argv[++i]);
    CretWorld* w = cret_world_acquire(seed, 2048, 4.0f, 1);
    if (!w) { fprintf(stderr, "floraprobe: no world\n"); return 1; }
    const TerraMap* m = &w->map; int n = m->n, step = 6, margin = 40;
    char cams[2048] = {0};
    for (int s = 0; s < S_COUNT; s++) {
        float best = 0; int bx = -1, bz = -1;
        for (int z = margin; z < n - margin; z += step) for (int x = margin; x < n - margin; x += step) {
            float v = score(m, s, x, z); if (v > best) { best = v; bx = x; bz = z; }
        }
        if (bx < 0) { fprintf(stderr, "%-12s none\n", SITES[s].name); continue; }
        // Look where the site is: along the shore or bank (a quarter turn from
        // the water), across the clearing's middle, down the ridge, or along
        // the forest floor.
        float yaw;
        switch (s) {
        case S_BEACH: yaw = toward(m, bx, bz, 12, waterf, TERRA_WATER_SEA) + 1.1f; break;
        case S_RIVER: yaw = toward(m, bx, bz, 8, waterf, TERRA_WATER_RIVER) + 0.8f; break;
        case S_WETLAND: yaw = toward(m, bx, bz, 10, waterf, area(m, bx, bz, 8, waterf, TERRA_WATER_LAKE) > 0 ? TERRA_WATER_LAKE : TERRA_WATER_RIVER) + 0.5f; break;
        case S_RIDGE: yaw = atan2f(hgt(m, bx, bz) - hgt(m, bx, bz + 1), hgt(m, bx, bz) - hgt(m, bx + 1, bz)); break;
        case S_CLEARING: yaw = toward(m, bx, bz, 10, matf, TERRA_MAT_FERN); break;
        default: yaw = 0.7f; break;
        }
        float x = bx * m->cell, z = bz * m->cell;
        fprintf(stderr, "%-12s %7.0f %7.0f  h %5.1f m  score %.3f  layers within 40 m:", SITES[s].name, x, z, hgt(m, bx, bz), best);
        static const char* LN[TERRA_VEG_COUNT] = {"conifer", "broadleaf", "cycad", "fern", "treefern", "horsetail", "palm", "shrub"};
        for (int k = 0; k < TERRA_VEG_COUNT; k++) fprintf(stderr, " %s %.2f", LN[k], area(m, bx, bz, 10, vegf, k));
        fprintf(stderr, "\n");
        char one[128]; snprintf(one, sizeof one, "%s%s:%.0f:%.0f:%.1f:%.2f:%.2f:60", cams[0] ? ";" : "", SITES[s].name, x, z, SITES[s].above, yaw, SITES[s].pitch);
        strncat(cams, one, sizeof cams - strlen(cams) - 1);
    }
    printf("%s\n", cams);
    cret_world_release(w);
    return 0;
}
