// Headless terrain review images: no GPU, no window. Loads (or bakes) the island exactly as the game
// does (world.c: the bake, then the beaches and ravines cut at load), lays out the props with the
// same scatter as the renderer, and draws them with a small software rasteriser: the drawn cubic
// ground, coloured by the splat materials and hillshaded, with water, tree trunks and the props'
// real meshes (back faces near black, as an open back shows in the game). JPEGs, no libraries.
//
//   terrashade --shoulders DIR [--count 6]   before/after of rock shoulder sites (before: shoulders=0)
//   terrashade --ravines DIR [--count 2]     ravines: stepped (ravine=1) against wide (ravine=2)
//   terrashade --island FILE.jpg             the whole island, shoulders and summits marked
//   terrashade --relief                      the island's relief: peaks, summits, hypsometric curve
//   terrashade --at X Z --size M --out FILE.jpg [--yaw RAD]   one place, top and oblique views
//   terrashade --site K --out FILE.jpg [--size M]            rock shoulder K, looking into its notch
// Common: --seed N --n 2048 --cell 4 (the island; default the game's), --quality Q (JPEG, 80).
// CRET_TERRA applies as in the game. Run from the repository root (world cache, props).
#include "../sim.h"
#include "../render/props_place.h"
#include "jpeg_write.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float MAT_RGB[TERRA_MAT_COUNT][3] = {
    {0.47f, 0.45f, 0.42f}, {0.60f, 0.57f, 0.52f}, {0.52f, 0.42f, 0.30f}, {0.27f, 0.25f, 0.15f},
    {0.33f, 0.45f, 0.18f}, {0.30f, 0.25f, 0.18f}, {0.80f, 0.74f, 0.58f}, {0.28f, 0.38f, 0.20f}};
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

// ---------------------------------------------------------------- canvas and camera
typedef struct { int w, h; float* rgb; float* z; } Canvas;
typedef struct { float c[3], f[3], r[3], u[3], scale; } Cam;   // orthographic: centre, forward, right, up, metres per pixel
static void canvas_init(Canvas* cv, int w, int h, float sky) {
    cv->w = w; cv->h = h; cv->rgb = malloc(sizeof(float) * 3 * w * h); cv->z = malloc(sizeof(float) * w * h);
    for (int i = 0; i < w * h; i++) { cv->rgb[3 * i] = 0.62f * sky; cv->rgb[3 * i + 1] = 0.72f * sky; cv->rgb[3 * i + 2] = 0.82f * sky; cv->z[i] = 1e30f; }
}
static void canvas_free(Canvas* cv) { free(cv->rgb); free(cv->z); }
static void cam_set(Cam* k, float cx, float cy, float cz, float yaw, float pitch, float scale) {
    k->c[0] = cx; k->c[1] = cy; k->c[2] = cz; k->scale = scale;
    k->f[0] = cosf(pitch) * sinf(yaw); k->f[1] = -sinf(pitch); k->f[2] = cosf(pitch) * cosf(yaw);
    k->r[0] = cosf(yaw); k->r[1] = 0; k->r[2] = -sinf(yaw);
    // up = forward x right
    k->u[0] = k->f[1] * k->r[2] - k->f[2] * k->r[1]; k->u[1] = k->f[2] * k->r[0] - k->f[0] * k->r[2]; k->u[2] = k->f[0] * k->r[1] - k->f[1] * k->r[0];
}
// Top view: north (-z) up, x to the right.
static void cam_top(Cam* k, float cx, float cz, float scale) {
    k->c[0] = cx; k->c[1] = 0; k->c[2] = cz; k->scale = scale;
    k->f[0] = 0; k->f[1] = -1; k->f[2] = 0; k->r[0] = 1; k->r[1] = 0; k->r[2] = 0; k->u[0] = 0; k->u[1] = 0; k->u[2] = -1;
}
static void project(const Canvas* cv, const Cam* k, const float p[3], float o[3]) {
    float d[3] = {p[0] - k->c[0], p[1] - k->c[1], p[2] - k->c[2]};
    o[0] = cv->w * 0.5f + (d[0] * k->r[0] + d[1] * k->r[1] + d[2] * k->r[2]) / k->scale;
    o[1] = cv->h * 0.5f - (d[0] * k->u[0] + d[1] * k->u[1] + d[2] * k->u[2]) / k->scale;
    o[2] = d[0] * k->f[0] + d[1] * k->f[1] + d[2] * k->f[2];
}
// One triangle with per-vertex colours, depth tested.
static void tri(Canvas* cv, const float a[3], const float b[3], const float c[3], const float* ca, const float* cb, const float* cc) {
    int x0 = (int)fmaxf(0, floorf(fminf(a[0], fminf(b[0], c[0])))), x1 = (int)fminf(cv->w - 1, ceilf(fmaxf(a[0], fmaxf(b[0], c[0]))));
    int y0 = (int)fmaxf(0, floorf(fminf(a[1], fminf(b[1], c[1])))), y1 = (int)fminf(cv->h - 1, ceilf(fmaxf(a[1], fmaxf(b[1], c[1]))));
    float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); if (fabsf(area) < 1e-9f) return;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float px = x + 0.5f, py = y + 0.5f;
        float w0 = ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area;
        float w1 = ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area, w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        float z = w0 * a[2] + w1 * b[2] + w2 * c[2]; int i = y * cv->w + x;
        if (z >= cv->z[i]) continue;
        cv->z[i] = z;
        for (int k = 0; k < 3; k++) cv->rgb[3 * i + k] = w0 * ca[k] + w1 * cb[k] + w2 * cc[k];
    }
}
// The light: from the north-west for top views; low from the viewer's left for oblique ones, so
// the walls the camera looks at are lit.
static float SUN[3] = {-0.48f, 0.72f, -0.50f};
static float lambert(const float n[3]) { float d = n[0] * SUN[0] + n[1] * SUN[1] + n[2] * SUN[2]; return 0.38f + 0.75f * clampf(d, 0, 1); }

// ---------------------------------------------------------------- the ground
// The drawn surface on a grid of `step` m over a square of side `size` round (cx, cz).
static void draw_ground(Canvas* cv, const Cam* k, const CretWorld* w, float cx, float cz, float size, float step) {
    const TerraMap* m = &w->map; int g = (int)(size / step) + 1; float x0 = cx - size / 2, z0 = cz - size / 2;
    float* hy = malloc(sizeof(float) * g * g); float* xs = malloc(sizeof(float) * g * g); float* zs = malloc(sizeof(float) * g * g);
    for (int j = 0; j < g; j++) for (int i = 0; i < g; i++) { xs[j * g + i] = clampf(x0 + i * step, 0, w->size); zs[j * g + i] = clampf(z0 + j * step, 0, w->size); }
    terra_bspline_heights(w->hcoef, m->n, m->cell, xs, zs, g * g, hy);
    float* col = malloc(sizeof(float) * 3 * g * g); float* scr = malloc(sizeof(float) * 3 * g * g);
    for (int j = 0; j < g; j++) for (int i = 0; i < g; i++) {
        int q = j * g + i, il = i > 0 ? q - 1 : q, ir = i < g - 1 ? q + 1 : q, jl = j > 0 ? q - g : q, jr = j < g - 1 ? q + g : q;
        float gx = (hy[ir] - hy[il]) / ((ir - il) * step + 1e-6f), gz = (hy[jr] - hy[jl]) / ((jr - jl) / g * step + 1e-6f);
        float nn[3] = {-gx, 1, -gz}, l = sqrtf(nn[0] * nn[0] + 1 + nn[2] * nn[2]); nn[0] /= l; nn[1] /= l; nn[2] /= l;
        float x = xs[q], z = zs[q], fx = clampf(x / m->cell, 0, m->n - 1.001f), fz = clampf(z / m->cell, 0, m->n - 1.001f);
        int ix = (int)fx, iz = (int)fz; float tx = fx - ix, tz = fz - iz, rgb[3] = {0, 0, 0}, trees = 0;
        for (int c = 0; c < 4; c++) {   // bilinear splat and canopy
            size_t s = (size_t)(iz + (c >> 1)) * m->n + ix + (c & 1); float wt = (c & 1 ? tx : 1 - tx) * (c >> 1 ? tz : 1 - tz);
            for (int t = 0; t < TERRA_MAT_COUNT; t++) for (int a = 0; a < 3; a++) rgb[a] += wt * m->splat[s * TERRA_MAT_COUNT + t] / 255.0f * MAT_RGB[t][a];
            const uint8_t* v = m->veg + s * TERRA_VEG_COUNT;
            trees += wt * clampf((v[TERRA_VEG_CONIFER] + v[TERRA_VEG_BROADLEAF] + v[TERRA_VEG_TREEFERN] + v[TERRA_VEG_PALM]) / 255.0f, 0, 1);
        }
        rgb[0] = rgb[0] * (1 - 0.35f * trees) + 0.07f * trees; rgb[1] = rgb[1] * (1 - 0.25f * trees) + 0.15f * trees; rgb[2] = rgb[2] * (1 - 0.35f * trees) + 0.05f * trees;
        float sh = lambert(nn), wl = cret_water(w, x, z);
        for (int a = 0; a < 3; a++) rgb[a] *= sh;
        if (wl > hy[q]) {
            float d = wl - hy[q], t = 1 - expf(-d / 3.0f), wc[3] = {0.08f, 0.28f, 0.36f};
            for (int a = 0; a < 3; a++) rgb[a] = rgb[a] * (1 - t) + wc[a] * t;
        }
        memcpy(col + 3 * q, rgb, sizeof rgb);
        float p[3] = {x, fmaxf(hy[q], wl > hy[q] ? wl : hy[q]), z}; project(cv, k, p, scr + 3 * q);
    }
    for (int j = 0; j + 1 < g; j++) for (int i = 0; i + 1 < g; i++) {
        int a = j * g + i, b = a + 1, c = a + g, d = c + 1;
        tri(cv, scr + 3 * a, scr + 3 * b, scr + 3 * d, col + 3 * a, col + 3 * b, col + 3 * d);
        tri(cv, scr + 3 * a, scr + 3 * d, scr + 3 * c, col + 3 * a, col + 3 * d, col + 3 * c);
    }
    free(hy); free(xs); free(zs); free(col); free(scr);
}

// ---------------------------------------------------------------- props
typedef struct { float* pos; int32_t* idx; int nv, ni; } Mesh;
static Mesh g_mesh[PROP_MAX_VARIANTS];
static const Mesh* mesh_of(const PropCatalog* c, int v) {
    if (!g_mesh[v].pos) {
        char path[512]; snprintf(path, sizeof path, PROP_DIR "%s.prop", c->name[v]);
        g_mesh[v].nv = prop_read_lod(path, 1, &g_mesh[v].pos, &g_mesh[v].idx, &g_mesh[v].ni);
    }
    return &g_mesh[v];
}
static void qrot(const float q[4], const float v[3], float o[3]) {
    float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty); o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz); o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
static bool wooden(int k) { return k == PROP_LOG || k == PROP_STUMP || k == PROP_ROOTS || k == PROP_DEBRIS || k == PROP_DRIFTWOOD; }
static void draw_prop(Canvas* cv, const Cam* k, const PropCatalog* c, const PropInstance* in) {
    const Mesh* me = mesh_of(c, in->variant); if (me->nv <= 0) return;
    float base[3] = {0.50f, 0.48f, 0.45f};
    if (wooden(in->kind)) { base[0] = 0.40f; base[1] = 0.30f; base[2] = 0.20f; }
    float moss = clampf(in->moss, 0, 1);
    base[0] = base[0] * (1 - 0.5f * moss) + 0.26f * 0.5f * moss; base[1] = base[1] * (1 - 0.5f * moss) + 0.34f * 0.5f * moss; base[2] = base[2] * (1 - 0.5f * moss) + 0.16f * 0.5f * moss;
    for (int t = 0; t + 2 < me->ni; t += 3) {
        float wp[3][3], sp[3][3];
        for (int v = 0; v < 3; v++) {
            const float* p = me->pos + 3 * me->idx[t + v]; float s[3] = {p[0] * in->scale, p[1] * in->scale, p[2] * in->scale};
            qrot(in->q, s, wp[v]); wp[v][0] += in->x; wp[v][1] += in->y; wp[v][2] += in->z; project(cv, k, wp[v], sp[v]);
        }
        float e1[3] = {wp[1][0] - wp[0][0], wp[1][1] - wp[0][1], wp[1][2] - wp[0][2]}, e2[3] = {wp[2][0] - wp[0][0], wp[2][1] - wp[0][1], wp[2][2] - wp[0][2]};
        float nn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]}, l = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
        if (l < 1e-12f) continue;
        nn[0] /= l; nn[1] /= l; nn[2] /= l;
        float facing = nn[0] * k->f[0] + nn[1] * k->f[1] + nn[2] * k->f[2], cl[3];
        if (facing > 0) { cl[0] = 0.04f; cl[1] = 0.03f; cl[2] = 0.03f; }   // a back face: the hole in an open shell
        else { float s = lambert(nn); for (int a = 0; a < 3; a++) cl[a] = base[a] * s; }
        tri(cv, sp[0], sp[1], sp[2], cl, cl, cl);
    }
}
// Every prop of every class within `r` m of (cx, cz), as the renderer scatters them.
static void draw_props(Canvas* cv, const Cam* k, const CretWorld* w, float cx, float cz, float r, int* counts) {
    const PropCatalog* c = &w->prop_cat; PropInstance* buf = malloc(sizeof(PropInstance) * 4096);
    for (int tz = (int)floorf((cz - r) / PROP_TILE); tz <= (int)floorf((cz + r) / PROP_TILE); tz++)
        for (int tx = (int)floorf((cx - r) / PROP_TILE); tx <= (int)floorf((cx + r) / PROP_TILE); tx++) {
            if (tx < 0 || tz < 0 || tx >= w->prop_tiles || tz >= w->prop_tiles) continue;
            for (int kd = 0; kd < PROP_KINDS; kd++) {
                int n = prop_scatter(c, &w->map, kd, tx, tz, buf, 4096);
                for (int i = 0; i < n; i++) {
                    if (fabsf(buf[i].x - cx) > r || fabsf(buf[i].z - cz) > r) continue;
                    draw_prop(cv, k, c, &buf[i]);
                    if (counts) counts[kd] += (buf[i].key & 1023u) >= PROP_SHOULDER_CELL ? 1000 : 1;   // thousands: shoulder pieces
                }
            }
        }
    free(buf);
}
// Tree trunks: a dark stick each (oblique) or a canopy dot (top view).
static void draw_trees(Canvas* cv, const Cam* k, const CretWorld* w, float cx, float cz, float r, bool top) {
    float tr[5 * 256];
    for (int tz = (int)floorf((cz - r) / 64); tz <= (int)floorf((cz + r) / 64); tz++)
        for (int tx = (int)floorf((cx - r) / 64); tx <= (int)floorf((cx + r) / 64); tx++) {
            int n = cret_tree_trunks(w, tx, tz, tr, 256);
            for (int i = 0; i < n; i++) {
                const float* t = tr + 5 * i; if (fabsf(t[0] - cx) > r || fabsf(t[2] - cz) > r) continue;
                float rad = top ? fmaxf(1.2f, 0.12f * t[4]) : fmaxf(0.25f, t[3]);
                float col[3] = {0.10f, 0.20f, 0.07f};
                // A small upright quad facing the camera.
                float p0[3] = {t[0] - k->r[0] * rad, t[1], t[2] - k->r[2] * rad}, p1[3] = {t[0] + k->r[0] * rad, t[1], t[2] + k->r[2] * rad};
                float top_y = top ? t[1] + 0.1f : t[1] + t[4];
                float p2[3] = {p1[0], top_y, p1[2]}, p3[3] = {p0[0], top_y, p0[2]}, s[4][3];
                if (top) { p0[0] = t[0] - rad; p0[2] = t[2] - rad; p1[0] = t[0] + rad; p1[2] = t[2] - rad; p2[0] = t[0] + rad; p2[2] = t[2] + rad; p3[0] = t[0] - rad; p3[2] = t[2] + rad; p0[1] = p1[1] = p2[1] = p3[1] = t[1] + t[4]; }
                project(cv, k, p0, s[0]); project(cv, k, p1, s[1]); project(cv, k, p2, s[2]); project(cv, k, p3, s[3]);
                tri(cv, s[0], s[1], s[2], col, col, col); tri(cv, s[0], s[2], s[3], col, col, col);
            }
        }
}
// A shoulder's wall foot as a thin orange line just above the ground (top views only).
static void draw_wall_line(Canvas* cv, const Cam* k, const CretWorld* w, const TerraShoulder* s) {
    float col[3] = {0.95f, 0.55f, 0.10f};
    for (float t = -1.0f; t < 1.0f; t += 0.01f) {
        float f[4]; terra_shoulder_foot(s, t, f); float p[3] = {f[0], 1e4f, f[1]}, q[3]; project(cv, k, p, q);
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int x = (int)q[0] + dx, y = (int)q[1] + dy; if (x < 0 || y < 0 || x >= cv->w || y >= cv->h) continue;
            for (int a = 0; a < 3; a++) cv->rgb[3 * (y * cv->w + x) + a] = 0.5f * cv->rgb[3 * (y * cv->w + x) + a] + 0.5f * col[a];
        }
    }
}

// One view: a top view (`top`) or an oblique one looking along `yaw` from 32 degrees above.
static void view(Canvas* cv, const CretWorld* w, float cx, float cz, float size, float yaw, bool top, const TerraShoulder* mark, int* counts) {
    Cam k; float cy = cret_height(w, cx, cz);
    if (top) { cam_top(&k, cx, cz, size / cv->w); SUN[0] = -0.48f; SUN[1] = 0.72f; SUN[2] = -0.50f; }
    else {
        cam_set(&k, cx, cy, cz, yaw, 0.56f, size * 0.85f / cv->w);
        float l[3] = {-0.35f * sinf(yaw) - 0.75f * k.r[0], 0.6f, -0.35f * cosf(yaw) - 0.75f * k.r[2]}, n = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        for (int a = 0; a < 3; a++) SUN[a] = l[a] / n;
    }
    float reach = top ? size * 0.75f : size * 0.9f;
    draw_ground(cv, &k, w, cx, cz, 2 * reach, top ? size / cv->w * 2.5f : 0.75f);
    draw_props(cv, &k, w, cx, cz, reach, counts);
    draw_trees(cv, &k, w, cx, cz, reach, top);
    if (top && mark) draw_wall_line(cv, &k, w, mark);
}

// ---------------------------------------------------------------- panels
typedef struct { int w, h; uint8_t* rgb; } Panel;
static void panel_init(Panel* p, int w, int h) { p->w = w; p->h = h; p->rgb = calloc((size_t)w * h * 3, 1); memset(p->rgb, 235, (size_t)w * h * 3); }
static void panel_put(Panel* p, const Canvas* cv, int ox, int oy) {
    for (int y = 0; y < cv->h; y++) for (int x = 0; x < cv->w; x++) for (int a = 0; a < 3; a++) {
        float v = powf(clampf(cv->rgb[3 * (y * cv->w + x) + a], 0, 1), 1 / 1.25f);
        p->rgb[((size_t)(oy + y) * p->w + ox + x) * 3 + a] = (uint8_t)(v * 255 + 0.5f);
    }
}
static CretWorld* world_with(uint32_t seed, int n, float cell, const char* spec) {
    if (spec) setenv("CRET_TERRA", spec, 1); else unsetenv("CRET_TERRA");
    CretWorld* w = cret_world_acquire(seed, n, cell, 1);
    if (!w) { fprintf(stderr, "terrashade: the world did not load\n"); exit(1); }
    return w;
}
// Wall pieces (cliff class, shoulder cells) each shoulder of `w` stands in.
static int wall_pieces(const CretWorld* w, const TerraShoulder* s) {
    PropInstance* buf = malloc(sizeof(PropInstance) * 4096); int cnt = 0; float R = terra_shoulder_reach(s);
    for (int tz = (int)floorf((s->z - R) / PROP_TILE); tz <= (int)floorf((s->z + R) / PROP_TILE); tz++)
        for (int tx = (int)floorf((s->x - R) / PROP_TILE); tx <= (int)floorf((s->x + R) / PROP_TILE); tx++) {
            if (tx < 0 || tz < 0 || tx >= w->prop_tiles || tz >= w->prop_tiles) continue;
            int n = prop_scatter(&w->prop_cat, &w->map, PROP_CLIFF, tx, tz, buf, 4096);
            for (int i = 0; i < n; i++) if ((buf[i].key & 1023u) >= PROP_SHOULDER_CELL && hypotf(buf[i].x - s->x, buf[i].z - s->z) < R) cnt++;
        }
    free(buf); return cnt;
}

// A before/after picture: before on the left, after on the right, top view above, oblique below.
// Worlds are cached by seed and size, so the two are loaded one after the other and each draws its
// column of every picture.
typedef struct { float cx, cz, size, yaw; TerraShoulder mark; bool marked; Panel p; } Frame;
#define FT 400
#define FO 300
#define FG 6
static void frames_draw(Frame* f, int nf, const CretWorld* w, int column) {
    for (int k = 0; k < nf; k++) {
        if (!f[k].p.rgb) panel_init(&f[k].p, 2 * FT + FG, FT + FO + FG);
        Canvas a, b; canvas_init(&a, FT, FT, 0.0f); canvas_init(&b, FT, FO, 1.0f);
        view(&a, w, f[k].cx, f[k].cz, f[k].size, f[k].yaw, true, column && f[k].marked ? &f[k].mark : NULL, NULL);
        view(&b, w, f[k].cx, f[k].cz, f[k].size, f[k].yaw, false, NULL, NULL);
        panel_put(&f[k].p, &a, column * (FT + FG), 0); panel_put(&f[k].p, &b, column * (FT + FG), FT + FG);
        canvas_free(&a); canvas_free(&b);
    }
}
static void frames_write(Frame* f, int nf, const char* dir, const char* stem, int quality) {
    for (int k = 0; k < nf; k++) {
        char path[512]; snprintf(path, sizeof path, "%s/%s_%d.jpg", dir, stem, k + 1);
        if (jpeg_write(path, f[k].p.rgb, f[k].p.w, f[k].p.h, quality)) fprintf(stderr, "terrashade: cannot write %s\n", path); else fprintf(stderr, "wrote %s\n", path);
        free(f[k].p.rgb);
    }
}

// ---------------------------------------------------------------- relief
// Summits: the highest point within 400 m; a summit's prominence is approximated by how far the
// ground falls within 1.2 km (the lowest point of the ring there), enough to tell peaks from bumps.
typedef struct { float h, x, z, drop; } Summit;
static int summits(const TerraMap* m, Summit* out, int cap) {
    int n = m->n, step = 4, cnt = 0; float c = m->cell; int r = (int)(400 / (c * step)), R = (int)(1200 / (c * step));
    int g = n / step; float* hs = malloc(sizeof(float) * g * g);
    for (int j = 0; j < g; j++) for (int i = 0; i < g; i++) hs[j * g + i] = m->height[(size_t)(j * step) * n + i * step];
    for (int j = 0; j < g && cnt < cap; j++) for (int i = 0; i < g && cnt < cap; i++) {
        float h = hs[j * g + i]; if (h < 50) continue;
        bool top = true; float low = h;
        for (int dj = -R; dj <= R && top; dj++) for (int di = -R; di <= R; di++) {
            int a = i + di, b = j + dj; if (a < 0 || b < 0 || a >= g || b >= g) continue;
            float v = hs[b * g + a], d2 = (float)(di * di + dj * dj);
            if (d2 <= r * r && (v > h || (v == h && (b * g + a) < j * g + i))) { top = false; break; }
            if (d2 <= R * R && d2 > (R - 2) * (R - 2)) low = fminf(low, v);
        }
        if (top) out[cnt++] = (Summit){h, i * step * c, j * step * c, h - fmaxf(low, 0)};
    }
    free(hs);
    for (int a = 1; a < cnt; a++) for (int b = a; b > 0 && out[b].h > out[b - 1].h; b--) { Summit t = out[b]; out[b] = out[b - 1]; out[b - 1] = t; }
    return cnt;
}
static void relief(const CretWorld* w) {
    const TerraMap* m = &w->map; size_t N = (size_t)m->n * m->n; float c2 = m->cell * m->cell; long land = 0; float top = -1e9f; size_t ti = 0;
    for (size_t i = 0; i < N; i++) { if (m->height[i] > 0) land++; if (m->height[i] > top) { top = m->height[i]; ti = i; } }
    printf("island %.1f km across, land %.1f km2; highest point %.0f m at (%.0f, %.0f)\n", w->size / 1000, land * c2 / 1e6, top, (ti % m->n) * m->cell, (ti / m->n) * m->cell);
    Summit s[256]; int ns = summits(m, s, 256), a150 = 0, a300 = 0, a500 = 0;
    for (int k = 0; k < ns; k++) { a150 += s[k].h > 150; a300 += s[k].h > 300; a500 += s[k].h > 500; }
    printf("summits (highest point within 400 m) above 150 / 300 / 500 m: %d / %d / %d\n", a150, a300, a500);
    for (int k = 0; k < ns && k < 12; k++) printf("  %4.0f m at (%4.0f, %4.0f), falls %3.0f m within 1.2 km%s\n", s[k].h, s[k].x, s[k].z, s[k].drop,
                                               s[k].x < 0.1f * w->size || s[k].z < 0.1f * w->size || s[k].x > 0.9f * w->size || s[k].z > 0.9f * w->size ? " (outside the spawn area)" : "");
    printf("hypsometric curve (share of the land above each height):\n");
    for (int hgt = 0; hgt <= 700; hgt += 50) {
        long above = 0; for (size_t i = 0; i < N; i++) above += m->height[i] > hgt;
        if (hgt > 0 && !above) break;
        printf("  %4d m  %5.1f%%  %6.2f km2\n", hgt, 100.0 * above / fmax(land, 1), above * c2 / 1e6);
    }
    long st[4] = {0}; long nl = 0;
    for (int z = 1; z < m->n - 1; z++) for (int x = 1; x < m->n - 1; x++) {
        size_t i = (size_t)z * m->n + x; if (m->height[i] <= 0) continue;
        float gx = (m->height[i + 1] - m->height[i - 1]) / (2 * m->cell), gz = (m->height[i + m->n] - m->height[i - m->n]) / (2 * m->cell), sl = sqrtf(gx * gx + gz * gz);
        nl++; st[0] += sl > 0.36f; st[1] += sl > 0.58f; st[2] += sl > 0.84f; st[3] += sl > 1.19f;
    }
    printf("slopes of the land: over 20 deg %.1f%%, 30 deg %.1f%%, 40 deg %.1f%%, 50 deg %.1f%%\n", 100.0 * st[0] / nl, 100.0 * st[1] / nl, 100.0 * st[2] / nl, 100.0 * st[3] / nl);
    printf("rock shoulders: %d (%.1f per km2 of land)\n", m->nshoulder, m->nshoulder / fmax(land * c2 / 1e6, 1e-6));
}
// The whole island from above, shoulders as red dots and summits over 300 m as white ones.
static void island(const char* path, const CretWorld* w, int quality) {
    int W = 1024; Canvas cv; canvas_init(&cv, W, W, 0.0f); Cam k; cam_top(&k, w->size / 2, w->size / 2, w->size / W);
    draw_ground(&cv, &k, w, w->size / 2, w->size / 2, w->size, w->size / W);
    Summit s[256]; int ns = summits(&w->map, s, 256);
    for (int pass = 0; pass < 2; pass++) for (int q = 0; q < (pass ? ns : w->map.nshoulder); q++) {
        float x = pass ? s[q].x : w->map.shoulder[q].x, z = pass ? s[q].z : w->map.shoulder[q].z; if (pass && s[q].h < 300) continue;
        int px = (int)(x / w->size * W), pz = (int)(z / w->size * W), r = pass ? 5 : 3;
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
            int X = px + dx, Y = pz + dy; if (X < 0 || Y < 0 || X >= W || Y >= W || dx * dx + dy * dy > r * r) continue;
            float* o = cv.rgb + 3 * (Y * W + X); o[0] = pass ? 1.0f : 0.9f; o[1] = pass ? 1.0f : 0.1f; o[2] = pass ? 1.0f : 0.1f;
        }
    }
    Panel p; panel_init(&p, W, W); panel_put(&p, &cv, 0, 0);
    if (jpeg_write(path, p.rgb, W, W, quality)) fprintf(stderr, "terrashade: cannot write %s\n", path); else fprintf(stderr, "wrote %s\n", path);
    free(p.rgb); canvas_free(&cv);
}

// ---------------------------------------------------------------- choosing what to show
// Shoulders with the most wall pieces, 700 m apart; a partner within 140 m is framed with its site.
static int pick_shoulders(const CretWorld* w, int want, int* pick) {
    const TerraMap* m = &w->map; int np = 0; int* score = malloc(sizeof(int) * (m->nshoulder + 1));
    for (int q = 0; q < m->nshoulder; q++) score[q] = wall_pieces(w, &m->shoulder[q]);
    for (int k = 0; k < want; k++) {
        int best = -1;
        for (int q = 0; q < m->nshoulder; q++) {
            bool far = true;
            for (int a = 0; a < np && far; a++) far = hypotf(m->shoulder[q].x - m->shoulder[pick[a]].x, m->shoulder[q].z - m->shoulder[pick[a]].z) > 700;
            if (far && (best < 0 || score[q] > score[best])) best = q;
        }
        if (best < 0) break;
        pick[np++] = best;
    }
    free(score); return np;
}
static int partner_of(const TerraMap* m, int q) {
    for (int k = 0; k < m->nshoulder; k++)
        if (k != q && hypotf(m->shoulder[k].x - m->shoulder[q].x, m->shoulder[k].z - m->shoulder[q].z) < 140 && m->shoulder[k].ux * m->shoulder[q].ux + m->shoulder[k].uz * m->shoulder[q].uz < -0.3f) return k;
    return -1;
}

int main(int argc, char** argv) {
    CretConfig cfg = cret_default_config(); uint32_t seed = cfg.world_seed; int n = cfg.world_n; float cell = cfg.world_cell;
    const char* mode = NULL; const char* arg = NULL; int count = -1, quality = 80, site = -1; float ax = 0, az = 0, size = 160, yaw = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cell") && i + 1 < argc) cell = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--count") && i + 1 < argc) count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--quality") && i + 1 < argc) quality = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) size = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) yaw = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--at") && i + 2 < argc) { ax = (float)atof(argv[++i]); az = (float)atof(argv[++i]); mode = "at"; }
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) arg = argv[++i];
        else if (!strcmp(argv[i], "--site") && i + 1 < argc) { site = atoi(argv[++i]); mode = "at"; }
        else if ((!strcmp(argv[i], "--shoulders") || !strcmp(argv[i], "--ravines") || !strcmp(argv[i], "--island")) && i + 1 < argc) { mode = argv[i] + 2; arg = argv[++i]; }
        else if (!strcmp(argv[i], "--relief")) mode = "relief";
        else { fprintf(stderr, "terrashade: unknown argument %s (see the top of tools/terrashade.c)\n", argv[i]); return 2; }
    }
    if (!mode) { fprintf(stderr, "terrashade: --shoulders DIR, --ravines DIR, --island FILE, --relief or --at X Z --out FILE\n"); return 2; }
    const char* user = getenv("CRET_TERRA"); char spec[256];
    if (!strcmp(mode, "relief") || !strcmp(mode, "island") || !strcmp(mode, "at")) {
        CretWorld* w = world_with(seed, n, cell, user);
        if (!strcmp(mode, "relief")) relief(w);
        else if (!strcmp(mode, "island")) island(arg, w, quality);
        else {
            Panel p; int T = 400, O = 300; panel_init(&p, T, T + O + 6); Canvas a, b; canvas_init(&a, T, T, 0); canvas_init(&b, T, O, 1);
            if (site >= 0 && site < w->map.nshoulder) {
                const TerraShoulder* s = &w->map.shoulder[site]; ax = s->x + s->ux * s->depth * 0.6f; az = s->z + s->uz * s->depth * 0.6f; yaw = atan2f(s->ux, s->uz);
                printf("site %d at (%.0f, %.0f): mouth %.0f m wide, %.0f m deep, wall %.1f m planned, %d wall pieces\n", site, s->x, s->z, 2 * s->half, s->depth, s->wall, wall_pieces(w, s));
            } else if (site >= 0) { fprintf(stderr, "terrashade: the island has %d shoulders\n", w->map.nshoulder); return 1; }
            int near = -1; terra_shoulder_near(&w->map, ax, az, 0, &near);
            view(&a, w, ax, az, size, yaw, true, near >= 0 ? &w->map.shoulder[near] : NULL, NULL); view(&b, w, ax, az, size, yaw, false, NULL, NULL);
            panel_put(&p, &a, 0, 0); panel_put(&p, &b, 0, T + 6);
            if (!arg || jpeg_write(arg, p.rgb, p.w, p.h, quality)) fprintf(stderr, "terrashade: give --out FILE.jpg\n"); else fprintf(stderr, "wrote %s\n", arg);
            free(p.rgb); canvas_free(&a); canvas_free(&b);
        }
        cret_world_release(w); return 0;
    }
    Frame f[16]; memset(f, 0, sizeof f); int nf = 0;
    if (!strcmp(mode, "shoulders")) {
        CretWorld* w = world_with(seed, n, cell, user);
        int pick[16], np = pick_shoulders(w, count > 0 && count <= 16 ? count : 6, pick);
        for (int k = 0; k < np; k++) {
            const TerraShoulder* s = &w->map.shoulder[pick[k]]; int pt = partner_of(&w->map, pick[k]);
            Frame* F = &f[nf++]; F->cx = s->x + s->ux * s->depth * 0.5f; F->cz = s->z + s->uz * s->depth * 0.5f; F->size = 110;
            F->yaw = atan2f(s->ux, s->uz); F->mark = *s; F->marked = true;
            if (pt >= 0) { const TerraShoulder* o = &w->map.shoulder[pt]; F->cx = 0.5f * (F->cx + o->x); F->cz = 0.5f * (F->cz + o->z); F->size = fmaxf(150, 1.3f * hypotf(o->x - s->x, o->z - s->z) + 60); }
            printf("shoulder_%d: site %d at (%.0f, %.0f), wall %.1f m planned, mouth %.0f m wide, %d wall pieces%s\n", k + 1, pick[k], s->x, s->z, s->wall, 2 * s->half,
                   wall_pieces(w, s), pt >= 0 ? ", with its partner across the valley" : "");
        }
        frames_draw(f, nf, w, 1); cret_world_release(w);
        // Before: the same bake without shoulders (baked once more the first time: shoulders are baked).
        snprintf(spec, sizeof spec, "%s%sshoulders=0", user ? user : "", user ? " " : "");
        w = world_with(seed, n, cell, spec); frames_draw(f, nf, w, 0); cret_world_release(w);
        frames_write(f, nf, arg, "shoulder", quality);
        return 0;
    }
    // Ravines: the deepest floors of the wide ones, 1 km apart, looked at along the ravine; stepped
    // (the first version) on the left. The same bake: the ravines are cut at load.
    snprintf(spec, sizeof spec, "%s%sravine=2", user ? user : "", user ? " " : "");
    CretWorld* w = world_with(seed, n, cell, spec);
    int want = count > 0 && count <= 8 ? count : 2, h = w->map.n / 2;
    for (int k = 0; k < want; k++) {
        int best = -1; float bv = 0;
        for (int i = 0; i < h * h; i += 3) {
            float x = (i % h) * 2 * w->map.cell, z = (i / h) * 2 * w->map.cell, v = cret_world_gorge(w, x, z); bool far = true;
            for (int a = 0; a < nf && far; a++) far = hypotf(x - f[a].cx, z - f[a].cz) > 1000;
            if (far && v > bv + 0.01f) { bv = v; best = i; }
        }
        if (best < 0) break;
        Frame* F = &f[nf++]; F->cx = (best % h) * 2 * w->map.cell; F->cz = (best / h) * 2 * w->map.cell; F->size = 200;
        float gx = 0, gz = 0;   // along the ravine: across the gradient of its depth
        for (int d = 1; d <= 3; d++) {
            gx += cret_world_gorge(w, F->cx + 8 * d, F->cz) - cret_world_gorge(w, F->cx - 8 * d, F->cz);
            gz += cret_world_gorge(w, F->cx, F->cz + 8 * d) - cret_world_gorge(w, F->cx, F->cz - 8 * d);
        }
        F->yaw = atan2f(-gz, gx);
        printf("ravine_%d: (%.0f, %.0f)\n", k + 1, F->cx, F->cz);
    }
    frames_draw(f, nf, w, 1); cret_world_release(w);
    snprintf(spec, sizeof spec, "%s%sravine=1", user ? user : "", user ? " " : "");
    w = world_with(seed, n, cell, spec); frames_draw(f, nf, w, 0); cret_world_release(w);
    frames_write(f, nf, arg, "ravine", quality);
    return 0;
}
