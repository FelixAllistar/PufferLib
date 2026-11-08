#include "props.h"
#include "raylib.h"
#include "texcache.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADER_DIR "ocean/cretaceous/shaders/"
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }

// ---------------------------------------------------------------- loading
typedef struct { float p[3], uv[2]; int8_t n[4], t[4]; } PVert;

static GfxMesh upload(const PVert* v, uint32_t nv, const uint32_t* idx, uint32_t ni) {
    GfxMesh g = {0};
    glGenVertexArrays(1, &g.vao); glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo); glBindBuffer(GL_ARRAY_BUFFER, g.vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(PVert) * nv, v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PVert), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(PVert), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_BYTE, GL_TRUE, sizeof(PVert), (void*)20);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_BYTE, GL_TRUE, sizeof(PVert), (void*)24);
    glGenBuffers(1, &g.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * ni, idx, GL_STATIC_DRAW);
    glBindVertexArray(0); g.index_count = (int)ni;
    return g;
}

static const char* PROP_TEX_SUF[3] = {"diff", "nor", "arm"};
typedef struct { int S; const char (*mats)[32]; void** a; void** b; } PropTexJob;
// One prop material's maps, decoded, resized and packed (a worker thread; no GL).
static void prop_tex_decode(int m, void* ctx) {
    PropTexJob* J = ctx; int S = J->S; Image im[3];
    for (int q = 0; q < 3; q++) {
        char path[512]; snprintf(path, sizeof path, PROP_DIR "%s_%s.jpg", J->mats[m], PROP_TEX_SUF[q]); im[q] = LoadImage(path);
        if (!im[q].data) im[q] = GenImageColor(S, S, q == 1 ? (Color){128, 128, 255, 255} : q == 2 ? (Color){255, 200, 0, 255} : GRAY);
        if (im[q].width != S || im[q].height != S) ImageResize(&im[q], S, S);
        ImageFormat(&im[q], PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }
    uint8_t* pa = malloc((size_t)S * S * 4); uint8_t* pb = malloc((size_t)S * S * 4);
    const uint8_t *cd = im[0].data, *cn = im[1].data, *cr = im[2].data;
    for (size_t i = 0; i < (size_t)S * S; i++) {
        pa[4 * i] = cd[4 * i]; pa[4 * i + 1] = cd[4 * i + 1]; pa[4 * i + 2] = cd[4 * i + 2]; pa[4 * i + 3] = cr[4 * i];
        // DXT5nm layout (as terrain): roughness, normal y, unused, normal x.
        pb[4 * i] = cr[4 * i + 1]; pb[4 * i + 1] = cn[4 * i + 1]; pb[4 * i + 2] = 0; pb[4 * i + 3] = cn[4 * i];
    }
    J->a[m] = pa; J->b[m] = pb;
    for (int q = 0; q < 3; q++) UnloadImage(im[q]);
}

static int cmp_str(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }

// Loads every .prop file (sorted, so scatter is deterministic) and the
// textures of each referenced material into two texture arrays.
static bool load_props(CretProps* p) {
    DIR* d = opendir(PROP_DIR); if (!d) { fprintf(stderr, "cretaceous: no props (run ocean/cretaceous/tools/fetch_props.sh)\n"); return false; }
    char* names[256]; int nn = 0; struct dirent* e;
    while ((e = readdir(d)) && nn < 256) { size_t L = strlen(e->d_name); if (L > 5 && !strcmp(e->d_name + L - 5, ".prop")) names[nn++] = strdup(e->d_name); }
    closedir(d);
    qsort(names, nn, sizeof(char*), cmp_str);
    char mats[PROP_MAX_VARIANTS][32]; int nmat = 0;
    for (int k = 0; k < nn && p->nvar < PROP_MAX_VARIANTS; k++) {
        char path[512]; snprintf(path, sizeof path, PROP_DIR "%s", names[k]);
        FILE* f = fopen(path, "rb"); if (!f) continue;
        uint32_t magic, ver, nlod; char mat[32]; float radius, height;
        bool ok = fread(&magic, 4, 1, f) && fread(&ver, 4, 1, f) && fread(mat, 32, 1, f) && fread(&radius, 4, 1, f) && fread(&height, 4, 1, f) && fread(&nlod, 4, 1, f);
        ok = ok && magic == 0x50525043u && ver == 1 && nlod == PROP_LODS && radius > 0 && height >= 0;
        mat[31] = 0;
        PropVariant* v = &p->var[p->nvar]; memset(v, 0, sizeof *v);
        for (int l = 0; ok && l < PROP_LODS; l++) {
            uint32_t nv, ni;
            if (fread(&nv, 4, 1, f) != 1 || fread(&ni, 4, 1, f) != 1 || nv == 0 || nv > (1u << 22) || ni == 0 || ni > (1u << 24) || ni % 3) { ok = false; break; }
            PVert* vb = malloc(sizeof(PVert) * nv); uint32_t* ib = malloc(4 * (size_t)ni);
            ok = fread(vb, sizeof(PVert), nv, f) == nv && fread(ib, 4, ni, f) == ni;
            for (uint32_t i = 0; ok && i < ni; i++) if (ib[i] >= nv) ok = false;
            if (ok) { v->lod[l] = upload(vb, nv, ib, ni); v->tris[l] = (int)ni / 3; }
            free(vb); free(ib);
        }
        fclose(f);
        if (!ok) { fprintf(stderr, "cretaceous: bad prop file %s\n", path); continue; }
        names[k][strlen(names[k]) - 5] = 0;
        snprintf(v->name, sizeof v->name, "%s", names[k]);
        v->radius = radius; v->height = height;
        int m = -1; for (int i = 0; i < nmat; i++) if (!strcmp(mats[i], mat)) m = i;
        if (m < 0) { m = nmat++; snprintf(mats[m], 32, "%s", mat); }
        v->layer = m;
        prop_catalog_add(&p->cat, v->name, radius, height); v->kind = p->cat.kind[p->nvar];
        p->nvar++;
    }
    for (int k = 0; k < nn; k++) free(names[k]);
    if (!p->nvar) return false;
    CRET_LOAD_MARK("props: meshes");
    // Textures: albedo.rgb + ao, normal.xy + roughness. CRET_PROP_TEX sets the size.
    // Cached GPU-ready under a key over the material list and its images
    // (render/texcache.h); a miss decodes on threads, then uploads.
    const char* ts = getenv("CRET_PROP_TEX"); int S = ts ? atoi(ts) : 1024; if (S < 128 || S > 2048) S = 1024;
    bool bc = gfx_use_bc3();
    char (*paths)[512] = malloc(sizeof(*paths) * 3 * nmat); const char** files = malloc(sizeof(char*) * 3 * nmat);
    for (int m = 0; m < nmat; m++) for (int q = 0; q < 3; q++) { snprintf(paths[m * 3 + q], 512, PROP_DIR "%s_%s.jpg", mats[m], PROP_TEX_SUF[q]); files[m * 3 + q] = paths[m * 3 + q]; }
    uint64_t key = cret_hash_str(cret_hash_u64(CRET_HASH_SEED, CRET_TEX_CACHE_VERSION), "prop materials v1");
    key = cret_hash_u64(cret_hash_u64(cret_hash_u64(key, (uint64_t)S), bc), (uint64_t)nmat);
    key = cret_hash_files(key, files, 3 * nmat);
    free(files); free(paths);
    p->t_albedo = cret_tex_cache_load("prop_albedo", key); p->t_detail = p->t_albedo ? cret_tex_cache_load("prop_detail", key) : 0;
    if (!p->t_albedo || !p->t_detail) {
        if (p->t_albedo) { glDeleteTextures(1, &p->t_albedo); p->t_albedo = 0; }
        void** a = calloc(nmat, sizeof(void*)); void** b = calloc(nmat, sizeof(void*));
        PropTexJob job = {S, (const char (*)[32])mats, a, b};
        cret_parallel_for(nmat, 0, prop_tex_decode, &job);
        CRET_LOAD_MARK("props: texture decode");
        // BC3 block compression (a quarter of the bandwidth; see load_materials in view.c).
        p->t_albedo = gfx_tex_array(S, S, nmat, bc ? 0x8C4F /*COMPRESSED_SRGB_ALPHA_S3TC_DXT5*/ : GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, (const void* const*)a);
        p->t_detail = gfx_tex_array(S, S, nmat, bc ? 0x83F3 /*COMPRESSED_RGBA_S3TC_DXT5*/ : GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, (const void* const*)b);
        for (int i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D_ARRAY, i ? p->t_detail : p->t_albedo);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameterf(GL_TEXTURE_2D_ARRAY, 0x84FE, 8.0f);
        }
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        for (int m = 0; m < nmat; m++) { free(a[m]); free(b[m]); }
        free(a); free(b);
        CRET_LOAD_MARK("props: texture upload");
        cret_tex_cache_store("prop_albedo", key, p->t_albedo, GL_TEXTURE_2D_ARRAY);
        cret_tex_cache_store("prop_detail", key, p->t_detail, GL_TEXTURE_2D_ARRAY);
        CRET_LOAD_MARK("props: store texture cache");
    } else CRET_LOAD_MARK("props: textures from cache");
    p->nmat = nmat;
    return true;
}

bool cret_props_init(CretProps* p, const TerraMap* map, float world_size) {
    memset(p, 0, sizeof *p); p->map = map; p->world_size = world_size;
    if (!load_props(p)) return false;
    if (!gfx_program_load(&p->prog, SHADER_DIR "props.vs", SHADER_DIR "props.fs")) return false;
    gfx_program_load(&p->shadow_prog, SHADER_DIR "props.vs", SHADER_DIR "depth.fs");
    glGenBuffers(1, &p->inst_vbo); glGenBuffers(CSM_MAX, p->shadow_vbo);
    const char* nr = getenv("CRET_PROP_NEAR"); p->near_range = nr ? (float)atof(nr) : 70.0f;
    p->tiles_n = (int)ceilf(world_size / PROP_TILE);
    p->tiles = calloc((size_t)p->tiles_n * p->tiles_n, sizeof(PropTile));
    PropInstance buf[4096]; long total = 0, per[PROP_KINDS] = {0};
    for (int tz = 0; tz < p->tiles_n; tz++) for (int tx = 0; tx < p->tiles_n; tx++) {
        int n = 0;
        for (int k = 0; k < PROP_KINDS; k++) if (!PROP_CLASS[k].near) { int c = prop_scatter(&p->cat, p->map, k, tx, tz, buf + n, 4096 - n); per[k] += c; n += c; }
        if (!n) continue;
        PropTile* t = &p->tiles[tz * p->tiles_n + tx];
        t->items = malloc(sizeof(PropInstance) * n); memcpy(t->items, buf, sizeof(PropInstance) * n); t->n = n;
        t->ymin = 1e9f; t->ymax = -1e9f;
        for (int i = 0; i < n; i++) {
            float r = p->var[buf[i].variant].radius * buf[i].scale, hh = p->var[buf[i].variant].height * buf[i].scale;
            t->ymin = fminf(t->ymin, buf[i].y - r); t->ymax = fmaxf(t->ymax, buf[i].y + hh + r);
        }
        total += n;
    }
    CRET_LOAD_MARK("props: static scatter");
    int tris0 = 0; for (int i = 0; i < p->nvar; i++) tris0 += p->var[i].tris[0];
    fprintf(stderr, "cretaceous: props %d variants (%d materials, %d LOD0 tris total), %ld static (boulder %ld outcrop %ld cliff %ld log %ld stump %ld roots %ld drift %ld coast %ld)\n",
            p->nvar, p->nmat, tris0, total, per[PROP_BOULDER], per[PROP_OUTCROP], per[PROP_CLIFF], per[PROP_LOG], per[PROP_STUMP], per[PROP_ROOTS], per[PROP_DRIFTWOOD], per[PROP_COAST]);
    return true;
}

static void refresh_near(CretProps* p, float cx, float cz) {
    int gx = (int)floorf(cx / 16.0f), gz = (int)floorf(cz / 16.0f);
    if (p->near_valid && gx == p->near_cx && gz == p->near_cz) return;
    p->near_cx = gx; p->near_cz = gz; p->near_valid = true; p->nnear = 0;
    float R = p->near_range + 16;
    int t0x = (int)floorf((cx - R) / PROP_TILE), t1x = (int)floorf((cx + R) / PROP_TILE), t0z = (int)floorf((cz - R) / PROP_TILE), t1z = (int)floorf((cz + R) / PROP_TILE);
    PropInstance* buf = malloc(sizeof(PropInstance) * 4096);
    for (int tz = t0z; tz <= t1z; tz++) for (int tx = t0x; tx <= t1x; tx++) {
        if (tx < 0 || tz < 0 || tx >= p->tiles_n || tz >= p->tiles_n) continue;
        for (int k = 0; k < PROP_KINDS; k++) {
            if (!PROP_CLASS[k].near) continue;
            int n = prop_scatter(&p->cat, p->map, k, tx, tz, buf, 4096);
            if (p->nnear + n > p->near_cap) { p->near_cap = (p->nnear + n) * 2; p->near = realloc(p->near, sizeof(PropInstance) * p->near_cap); }
            memcpy(p->near + p->nnear, buf, sizeof(PropInstance) * n); p->nnear += n;
        }
    }
    free(buf);
}

// ---------------------------------------------------------------- drawing
typedef struct { float ps[4], q[4], ex[4], misc[4]; } GpuProp;   // ex: fade, layer, moss, tint; misc: height
typedef struct { GpuProp* a; int n, cap; } PList;
// Draw lists [variant][lod] for slot (0 view, 1 + cascade), emptied but kept.
static PList (*get_lists(CretProps* p, int slot))[PROP_LODS] {
    PList (*l)[PROP_LODS] = p->lists[slot];
    if (!l) l = p->lists[slot] = calloc(p->nvar, sizeof(*l));
    for (int v = 0; v < p->nvar; v++) for (int k = 0; k < PROP_LODS; k++) l[v][k].n = 0;
    return l;
}
static void plist_push(PList* l, const PropInstance* in, float fade, int layer, float height) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 256; l->a = realloc(l->a, sizeof(GpuProp) * l->cap); }
    l->a[l->n++] = (GpuProp){{in->x, in->y, in->z, in->scale}, {in->q[0], in->q[1], in->q[2], in->q[3]}, {fade, (float)layer, in->moss, in->tint}, {height, 0, 0, 0}};
}

typedef struct { float p[6][4]; } PFrustum;
static PFrustum pfrustum(const float* m) {
    PFrustum f; float r[4][4];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r[i][j] = m[j * 4 + i];
    for (int j = 0; j < 4; j++) {
        f.p[0][j] = r[3][j] + r[0][j]; f.p[1][j] = r[3][j] - r[0][j]; f.p[2][j] = r[3][j] + r[1][j];
        f.p[3][j] = r[3][j] - r[1][j]; f.p[4][j] = r[3][j] + r[2][j]; f.p[5][j] = r[3][j] - r[2][j];
    }
    return f;
}
static bool pbox(const PFrustum* f, float x0, float y0, float z0, float x1, float y1, float z1) {
    for (int i = 0; i < 6; i++) {
        const float* q = f->p[i];
        if (q[0] * (q[0] > 0 ? x1 : x0) + q[1] * (q[1] > 0 ? y1 : y0) + q[2] * (q[2] > 0 ? z1 : z0) + q[3] < 0) return false;
    }
    return true;
}

// LOD by distance over footprint radius; dithered crossfade at each switch.
static float g_lod_scale = 1.0f;
static void classify(CretProps* p, PList lists[][PROP_LODS], const PropInstance* in, float cx, float cy, float cz, const PFrustum* fr) {
    const PropVariant* v = &p->var[in->variant]; float r = v->radius * in->scale, hh = v->height * in->scale;
    if (!pbox(fr, in->x - r, in->y - r * 0.3f, in->z - r, in->x + r, in->y + hh + r * 0.3f, in->z + r)) return;
    float dx = in->x - cx, dy = in->y - cy, dz = in->z - cz, d = sqrtf(dx * dx + dy * dy + dz * dz) / fmaxf(r, 0.05f);
    float k = g_lod_scale * PROP_CLASS[in->kind].lod_mul;
    float t0 = 22.0f * k, t1 = 90.0f * k, cull = (in->kind == PROP_STONE || in->kind == PROP_DEBRIS) ? 200.0f * k : (in->kind == PROP_CLIFF || in->kind == PROP_COAST) ? 1100.0f * k : 600.0f * k;
    if (PROP_CLASS[in->kind].near) {   // near classes fade out at the regeneration radius instead
        float dd = sqrtf(dx * dx + dz * dz); float f = 1 - sstep(p->near_range * 0.8f, p->near_range, dd);
        if (f <= 0) return;
        if (d > cull) return;
        int lod = d < t0 ? 0 : d < t1 ? 1 : 2;
        plist_push(&lists[in->variant][lod], in, f, v->layer, v->height);
        return;
    }
    if (d > cull) return;
    float band = 0.15f, fade_out = 1 - sstep(cull * 0.85f, cull, d);
    float th[2] = {t0, t1};
    int lod = d < t0 ? 0 : d < t1 ? 1 : 2;
    // Within the band before a threshold, draw this LOD fading and the next fading in.
    if (lod < 2 && d > th[lod] * (1 - band)) {
        float f = sstep(th[lod] * (1 - band), th[lod], d);
        plist_push(&lists[in->variant][lod], in, 1 - f, v->layer, v->height);
        plist_push(&lists[in->variant][lod + 1], in, -1 - (1 - f), v->layer, v->height);   // complement
        return;
    }
    plist_push(&lists[in->variant][lod], in, lod == 2 ? fade_out : 1.0f, v->layer, v->height);
}

void cret_props_draw(CretProps* p, const VegFrameDesc* f) {
    if (getenv("CRET_PROPS_HIDE")) return;   // A/B comparisons
    const char* ls = getenv("CRET_PROP_LOD"); g_lod_scale = ls ? (float)atof(ls) : p->lod_scale > 0 ? p->lod_scale : 1.0f;
    PFrustum fr = pfrustum(f->viewproj);
    float cx = f->cam[0], cy = f->cam[1], cz = f->cam[2];
    static GLuint q_time; bool dbg = getenv("CRET_PROP_DEBUG") && f->frame % 24 == 23;
    if (dbg) { if (!q_time) glGenQueries(1, &q_time); glBeginQuery(GL_TIME_ELAPSED, q_time); }
    PList (*lists)[PROP_LODS] = get_lists(p, 0);
    float R = 1500.0f; int T = p->tiles_n;
    int t0x = (int)floorf((cx - R) / PROP_TILE), t1x = (int)floorf((cx + R) / PROP_TILE), t0z = (int)floorf((cz - R) / PROP_TILE), t1z = (int)floorf((cz + R) / PROP_TILE);
    for (int tz = t0z < 0 ? 0 : t0z; tz <= t1z && tz < T; tz++) for (int tx = t0x < 0 ? 0 : t0x; tx <= t1x && tx < T; tx++) {
        PropTile* t = &p->tiles[tz * T + tx]; if (!t->n) continue;
        float x0 = tx * PROP_TILE, z0 = tz * PROP_TILE;
        if (!pbox(&fr, x0 - 16, t->ymin, z0 - 16, x0 + PROP_TILE + 16, t->ymax, z0 + PROP_TILE + 16)) continue;
        for (int i = 0; i < t->n; i++) classify(p, lists, &t->items[i], cx, cy, cz, &fr);
    }
    refresh_near(p, cx, cz);
    for (int i = 0; i < p->nnear; i++) classify(p, lists, &p->near[i], cx, cy, cz, &fr);
    size_t total = 0; for (int v = 0; v < p->nvar; v++) for (int l = 0; l < PROP_LODS; l++) total += lists[v][l].n;
    for (int l = 0; l < PROP_LODS; l++) p->drawn[l] = 0;
    p->drawn_tris = 0;
    if (total) {
        glBindBuffer(GL_ARRAY_BUFFER, p->inst_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(GpuProp) * total, NULL, GL_STREAM_DRAW);
        GfxProgram* g = &p->prog; glUseProgram(g->id);
        gfx_setmat4(g, "u_viewproj", f->viewproj);
        gfx_set3f(g, "u_cam", cx, cy, cz); gfx_set3f(g, "u_sun", f->sun[0], f->sun[1], f->sun[2]);
        gfx_set1f(g, "u_far", f->far_plane); gfx_set1f(g, "u_time", f->time); gfx_set1i(g, "u_frame", f->frame);
        gfx_set1f(g, "u_cloud_cover", f->cloud_cover); gfx_set2f(g, "u_map", (float)f->map_n, f->map_cell);
        gfx_set1f(g, "u_overcast", f->overcast); gfx_set1f(g, "u_wet", f->wet);   // view.c weather
        gfx_set2f(g, "u_depth", f->depth_ab[0], f->depth_ab[1]);
        gfx_bind_tex(g, "t_albedo", 0, GL_TEXTURE_2D_ARRAY, p->t_albedo);
        gfx_bind_tex(g, "t_trans", 1, GL_TEXTURE_2D, f->t_trans);
        gfx_bind_tex(g, "t_skyview", 2, GL_TEXTURE_2D, f->t_skyview);
        gfx_bind_tex(g, "t_horizon0", 3, GL_TEXTURE_2D, f->t_horizon0);
        gfx_bind_tex(g, "t_horizon1", 4, GL_TEXTURE_2D, f->t_horizon1);
        gfx_bind_tex(g, "t_weather", 5, GL_TEXTURE_2D, f->t_weather);
        gfx_bind_tex(g, "t_detail", 6, GL_TEXTURE_2D_ARRAY, p->t_detail);
        cret_csm_bind(f->csm, g, 7);
        gfx_bind_tex(g, "t_misc", 8, GL_TEXTURE_2D, f->t_misc);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
        size_t off = 0;
        for (int v = 0; v < p->nvar; v++) for (int l = 0; l < PROP_LODS; l++) {
            PList* L = &lists[v][l]; if (!L->n) continue;
            glBindBuffer(GL_ARRAY_BUFFER, p->inst_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, off * sizeof(GpuProp), sizeof(GpuProp) * L->n, L->a);
            glBindVertexArray(p->var[v].lod[l].vao);
            for (int k = 0; k < 4; k++) {
                glEnableVertexAttribArray(4 + k);
                glVertexAttribPointer(4 + k, 4, GL_FLOAT, GL_FALSE, sizeof(GpuProp), (void*)(off * sizeof(GpuProp) + 16 * k));
                glVertexAttribDivisor(4 + k, 1);
            }
            glDrawElementsInstanced(GL_TRIANGLES, p->var[v].lod[l].index_count, GL_UNSIGNED_INT, 0, L->n);
            p->drawn[l] += L->n; p->drawn_tris += L->n * p->var[v].tris[l];
            off += L->n;
        }
        glDisable(GL_CULL_FACE);
        glBindVertexArray(0);
    }

    if (dbg) {
        glEndQuery(GL_TIME_ELAPSED); GLuint64 ns = 0; glGetQueryObjectui64v(q_time, GL_QUERY_RESULT, &ns);
        fprintf(stderr, "props: lod0 %d lod1 %d lod2 %d instances, %d tris, %d near, %.2f ms GPU\n", p->drawn[0], p->drawn[1], p->drawn[2], p->drawn_tris, p->nnear, ns / 1e6);
    }
}

// ---------------------------------------------------------------- sun shadow casters
static bool big_caster(int kind) {
    return kind == PROP_BOULDER || kind == PROP_OUTCROP || kind == PROP_CLIFF || kind == PROP_COAST || kind == PROP_LOG || kind == PROP_ROOTS;
}
static void shadow_add(const CretProps* p, PList lists[][PROP_LODS], const PropInstance* in, const CretCsm* c, int k, const float* cam) {
    const PropVariant* v = &p->var[in->variant]; float r = v->radius * in->scale, hh = v->height * in->scale;
    if (k >= 2 && !big_caster(in->kind)) return;
    if (k >= 3 && r < 1.5f) return;
    float pc[3] = {in->x, in->y + hh * 0.5f, in->z};
    if (!cret_csm_caster_visible(c, k, pc, fmaxf(r, hh * 0.5f), hh)) return;
    float dx = in->x - cam[0], dy = in->y - cam[1], dz = in->z - cam[2], d = sqrtf(dx * dx + dy * dy + dz * dz) / fmaxf(r, 0.05f);
    float m = g_lod_scale * PROP_CLASS[in->kind].lod_mul;
    int lod = d < 22.0f * m ? 0 : d < 90.0f * m ? 1 : 2;
    if (lod < k) lod = k > 2 ? 2 : k;           // coarser cascades: coarser meshes
    plist_push(&lists[in->variant][lod], in, 1.0f, v->layer, v->height);
}
int cret_props_draw_shadow(CretProps* p, const CretCsm* c, int k, const VegFrameDesc* f) {
    if (getenv("CRET_PROPS_HIDE")) return 0;
    float lo[3], hi[3]; cret_csm_bounds(c, k, lo, hi);
    PList (*lists)[PROP_LODS] = get_lists(p, 1 + k);
    int T = p->tiles_n;
    int t0x = (int)floorf((lo[0] - 16) / PROP_TILE), t1x = (int)floorf((hi[0] + 16) / PROP_TILE), t0z = (int)floorf((lo[2] - 16) / PROP_TILE), t1z = (int)floorf((hi[2] + 16) / PROP_TILE);
    for (int tz = t0z < 0 ? 0 : t0z; tz <= t1z && tz < T; tz++) for (int tx = t0x < 0 ? 0 : t0x; tx <= t1x && tx < T; tx++) {
        PropTile* t = &p->tiles[tz * T + tx]; if (!t->n || t->ymax < lo[1] || t->ymin > hi[1]) continue;
        for (int i = 0; i < t->n; i++) shadow_add(p, lists, &t->items[i], c, k, f->cam);
    }
    if (k <= 1) { refresh_near(p, f->cam[0], f->cam[2]); for (int i = 0; i < p->nnear; i++) shadow_add(p, lists, &p->near[i], c, k, f->cam); }
    size_t total = 0; for (int v = 0; v < p->nvar; v++) for (int l = 0; l < PROP_LODS; l++) total += lists[v][l].n;
    int drawn = 0;
    if (total) {
        GLuint vbo = p->shadow_vbo[k];
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(GpuProp) * total, NULL, GL_STREAM_DRAW);
        GfxProgram* g = &p->shadow_prog; glUseProgram(g->id);
        gfx_setmat4(g, "u_viewproj", c->vp[k]);
        glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);   // back faces cast: no self-shadow acne on lit faces
        size_t off = 0;
        for (int v = 0; v < p->nvar; v++) for (int l = 0; l < PROP_LODS; l++) {
            PList* L = &lists[v][l]; if (!L->n) continue;
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferSubData(GL_ARRAY_BUFFER, off * sizeof(GpuProp), sizeof(GpuProp) * L->n, L->a);
            glBindVertexArray(p->var[v].lod[l].vao);
            for (int a = 0; a < 4; a++) {
                glEnableVertexAttribArray(4 + a);
                glVertexAttribPointer(4 + a, 4, GL_FLOAT, GL_FALSE, sizeof(GpuProp), (void*)(off * sizeof(GpuProp) + 16 * a));
                glVertexAttribDivisor(4 + a, 1);
            }
            glDrawElementsInstanced(GL_TRIANGLES, p->var[v].lod[l].index_count, GL_UNSIGNED_INT, 0, L->n);
            drawn += L->n; off += L->n;
        }
        glCullFace(GL_BACK); glDisable(GL_CULL_FACE);
        glBindVertexArray(0);
    }

    return drawn;
}

bool cret_props_find(const CretProps* p, int kind, float x, float z, float out[4]) {
    float best = 1e30f; bool found = false; int T = p->tiles_n;
    for (int t = 0; t < T * T; t++) {
        const PropTile* tl = &p->tiles[t];
        for (int i = 0; i < tl->n; i++) {
            const PropInstance* in = &tl->items[i]; if (in->kind != kind) continue;
            // Prefer large props in the open (little canopy) near the point.
            float r = p->var[in->variant].radius * in->scale;
            float d = sqrtf((in->x - x) * (in->x - x) + (in->z - z) * (in->z - z)) * (1 + 6 * prop_canopy_at(p->map, in->x, in->z)) - 150 * r;
            if (d < best) { best = d; out[0] = in->x; out[1] = in->y; out[2] = in->z; out[3] = p->var[in->variant].radius * in->scale; found = true; }
        }
    }
    return found;
}

void cret_props_reload_shaders(CretProps* p) { gfx_program_reload_if_changed(&p->prog); gfx_program_reload_if_changed(&p->shadow_prog); }

void cret_props_close(CretProps* p) {
    for (int v = 0; v < p->nvar; v++) for (int l = 0; l < PROP_LODS; l++) gfx_mesh_free(&p->var[v].lod[l]);
    for (int s = 0; s < 5; s++) { PList (*l)[PROP_LODS] = p->lists[s]; if (!l) continue; for (int v = 0; v < p->nvar; v++) for (int k = 0; k < PROP_LODS; k++) free(l[v][k].a); free(l); }
    if (p->tiles) for (int t = 0; t < p->tiles_n * p->tiles_n; t++) free(p->tiles[t].items);
    free(p->tiles); free(p->near);
    glDeleteTextures(1, &p->t_albedo); glDeleteTextures(1, &p->t_detail); glDeleteBuffers(1, &p->inst_vbo);
    glDeleteBuffers(CSM_MAX, p->shadow_vbo);
    gfx_program_free(&p->prog); gfx_program_free(&p->shadow_prog);
    memset(p, 0, sizeof *p);
}
