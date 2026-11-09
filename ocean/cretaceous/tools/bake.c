// Bake a cretaceous world and write review images.
//   bake [--seed N] [--n 2048] [--cell 4] [--out FILE] [--preview DIR] [--k KEY=VAL]...
//   bake --load FILE --preview DIR      previews of an already baked world (no bake)
// Previews: shaded.png (materials + hillshade + water), height.png, rain.png,
// flow.png, veg.png. Raylib is used only to encode PNGs.
#include "../terra/terra.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double t0;
static void progress(const char* stage, float f, void* u) { (void)u; fprintf(stderr, "[%6.1fs] %-18s %3.0f%%\n", now() - t0, stage, f * 100); }

static const float MAT_RGB[TERRA_MAT_COUNT][3] = {
    {0.47f, 0.45f, 0.42f}, {0.60f, 0.57f, 0.52f}, {0.52f, 0.42f, 0.30f}, {0.27f, 0.25f, 0.15f},
    {0.33f, 0.45f, 0.18f}, {0.30f, 0.25f, 0.18f}, {0.80f, 0.74f, 0.58f}, {0.28f, 0.38f, 0.20f}};

static void save_rgb(const char* dir, const char* name, unsigned char* rgba, int w) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, name);
    Image im = {rgba, w, w, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    ExportImage(im, path); fprintf(stderr, "wrote %s\n", path);
}
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

static void previews(const TerraMap* m, const char* dir, int outw) {
    int n = m->n, step = n / outw; if (step < 1) step = 1; int w = n / step;
    unsigned char* img = malloc((size_t)w * w * 4);
    float hmin = 1e9f, hmax = -1e9f;
    for (size_t i = 0; i < (size_t)n * n; i++) { if (m->height[i] < hmin) hmin = m->height[i]; if (m->height[i] > hmax) hmax = m->height[i]; }
    fprintf(stderr, "height range %.1f .. %.1f m\n", hmin, hmax);
    float sx = -0.6f, sy = 0.55f, sz = -0.58f; float sl = sqrtf(sx * sx + sy * sy + sz * sz); sx /= sl; sy /= sl; sz /= sl;
    for (int pass = 0; pass < 5; pass++) {
        for (int y = 0; y < w; y++) for (int x = 0; x < w; x++) {
            int gx = x * step, gz = y * step; size_t i = (size_t)gz * n + gx;
            int xl = gx > 0 ? gx - 1 : gx, xr = gx < n - 1 ? gx + 1 : gx, zl = gz > 0 ? gz - 1 : gz, zr = gz < n - 1 ? gz + 1 : gz;
            float hx = (m->height[(size_t)gz * n + xr] - m->height[(size_t)gz * n + xl]) / ((xr - xl) * m->cell);
            float hz = (m->height[(size_t)zr * n + gx] - m->height[(size_t)zl * n + gx]) / ((zr - zl) * m->cell);
            float nx = -hx, ny = 1, nz = -hz, nl = sqrtf(nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
            float shade = clampf(nx * sx + ny * sy + nz * sz, 0, 1) * 0.85f + 0.25f;
            float r = 0, g = 0, b = 0;
            if (pass == 0) {
                for (int k = 0; k < TERRA_MAT_COUNT; k++) { float wgt = m->splat[i * TERRA_MAT_COUNT + k] / 255.0f; r += wgt * MAT_RGB[k][0]; g += wgt * MAT_RGB[k][1]; b += wgt * MAT_RGB[k][2]; }
                const uint8_t* v = m->veg + i * TERRA_VEG_COUNT;
                float trees = clampf((v[TERRA_VEG_CONIFER] + v[TERRA_VEG_BROADLEAF] + v[TERRA_VEG_TREEFERN] + v[TERRA_VEG_PALM]) / 255.0f, 0, 1);
                r = r * (1 - 0.55f * trees) + 0.10f * trees; g = g * (1 - 0.45f * trees) + 0.22f * trees; b = b * (1 - 0.55f * trees) + 0.08f * trees;
                r *= shade; g *= shade; b *= shade;
                float wl = m->water[i];
                if (wl > m->height[i]) {
                    float d = wl - m->height[i], t = 1 - expf(-d / 6.0f), tt = 1 - expf(-d / 60.0f);
                    float wr = 0.05f + 0.1f * (1 - tt), wg = 0.25f + 0.25f * (1 - tt), wb = 0.35f + 0.15f * (1 - tt);
                    r = r * (1 - t) + wr * t; g = g * (1 - t) + wg * t; b = b * (1 - t) + wb * t;
                }
            } else if (pass == 1) {
                float t = (m->height[i] - hmin) / (hmax - hmin + 1e-6f); r = g = b = t;
            } else if (pass == 2) {
                float t = clampf(m->rain[i] / 4.0f, 0, 1); r = 1 - t; g = 1 - 0.5f * t; b = 1;
                r *= shade; g *= shade; b *= shade;
            } else if (pass == 3) {
                float t = clampf(log10f(m->flow[i] + 1) / 9.0f, 0, 1); t = t * t * t; r = g = b = shade * 0.5f; b += t; g += t * 0.6f;
            } else {
                const uint8_t* v = m->veg + i * TERRA_VEG_COUNT;
                r = v[TERRA_VEG_CYCAD] / 255.0f + v[TERRA_VEG_PALM] / 255.0f; g = v[TERRA_VEG_CONIFER] / 255.0f; b = v[TERRA_VEG_BROADLEAF] / 255.0f + v[TERRA_VEG_TREEFERN] / 255.0f;
                r = r * 0.8f + 0.2f * shade; g = g * 0.8f + 0.2f * shade; b = b * 0.8f + 0.2f * shade;
            }
            unsigned char* o = img + ((size_t)y * w + x) * 4;
            o[0] = (unsigned char)(clampf(r, 0, 1) * 255); o[1] = (unsigned char)(clampf(g, 0, 1) * 255); o[2] = (unsigned char)(clampf(b, 0, 1) * 255); o[3] = 255;
        }
        static const char* names[5] = {"shaded.png", "height.png", "rain.png", "flow.png", "veg.png"};
        save_rgb(dir, names[pass], img, w);
    }
    free(img);
}

int main(int argc, char** argv) {
    TerraParams p = terra_default_params(1);
    const char* out = NULL; const char* prev = NULL; const char* load = NULL; int pw = 1024;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) p.seed = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) p.n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cell") && i + 1 < argc) p.cell = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) load = argv[++i];
        else if (!strcmp(argv[i], "--preview") && i + 1 < argc) prev = argv[++i];
        else if (!strcmp(argv[i], "--preview-size") && i + 1 < argc) pw = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--uplift") && i + 1 < argc) p.uplift = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--k") && i + 1 < argc) p.erodibility = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--years") && i + 1 < argc) p.evolve_years = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--plates") && i + 1 < argc) p.plates = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--drops") && i + 1 < argc) p.droplets_per_cell_x100 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--diffusion") && i + 1 < argc) p.diffusion = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--land") && i + 1 < argc) p.land_fraction = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) p.threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) { if (terra_params_apply(&p, argv[++i]) < 0) { fprintf(stderr, "bad --set\n"); return 2; } }
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }
    SetTraceLogLevel(LOG_WARNING);
    t0 = now(); TerraMap m;
    if (load) {
        if (terra_load(&m, NULL, load)) { fprintf(stderr, "cannot load %s (missing, or not TERRA_VERSION %u)\n", load, TERRA_VERSION); return 1; }
        if (prev) previews(&m, prev, pw);
        terra_free(&m); return 0;
    }
    if (terra_generate(&p, &m, progress, NULL)) { fprintf(stderr, "generation failed\n"); return 1; }
    fprintf(stderr, "generated %dx%d (%.1f km) in %.1fs\n", m.n, m.n, terra_size(&m) / 1000, now() - t0);
    size_t N = (size_t)m.n * m.n; long land = 0, lake = 0, river = 0;
    for (size_t i = 0; i < N; i++) { land += m.height[i] > 0; lake += m.water_kind[i] == TERRA_WATER_LAKE; river += m.water_kind[i] == TERRA_WATER_RIVER; }
    fprintf(stderr, "land %.1f%%  lake %.2f%%  river %.2f%%\n", 100.0 * land / N, 100.0 * lake / N, 100.0 * river / N);
    if (out) { if (terra_save(&m, &p, out)) fprintf(stderr, "save failed\n"); else fprintf(stderr, "wrote %s\n", out); }
    if (prev) previews(&m, prev, pw);
    terra_free(&m); return 0;
}
