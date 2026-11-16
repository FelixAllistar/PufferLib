// A .dino file read without GL (the layout is in tools/dino_convert.py's
// header), for tools/animcheck.c and tests/test_dino_ik.c. The game's own
// loader (render/dino.c, load_model) uploads as it reads, so it cannot run
// headless; this one keeps LOD0's vertices and the clips in memory and
// checks the file the same way (sizes, indices, joint parents). It also
// compiles as C++ (the locomotion env, loco/, is built into the trainer).
#ifndef CRET_DINO_FILE_H
#define CRET_DINO_FILE_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DF_MAX_JOINTS 256

typedef struct { float p[3], uv[2]; int8_t n[4], t[4]; uint8_t j[4], w[4]; } DfVert;
typedef struct { char name[32]; float duration, speed; int loop, nframes; float* frames; } DfClip;   // frames[f][joint][8]: local T, R (xyzw), S
typedef struct {
    float length, height, bmin[3], bmax[3];
    int njoint, parent[DF_MAX_JOINTS];
    char name[DF_MAX_JOINTS][32];           // as stored
    char low[DF_MAX_JOINTS][32];            // lower case, as render/dino.c matches them
    float inv_bind[DF_MAX_JOINTS][12];      // 3x4 row-major
    float rest[DF_MAX_JOINTS][8];
    int nvert; DfVert* vert;                // LOD0
    int nclip; DfClip clip[48];             // the game's DINO_MAX_CLIPS
} DinoFile;

static bool df_rd(FILE* f, void* p, size_t n) { return fread(p, 1, n, f) == n; }

static void df_free(DinoFile* d) {
    free(d->vert); d->vert = NULL;
    for (int c = 0; c < d->nclip; c++) free(d->clip[c].frames);
    d->nclip = 0;
}
// Reads path into d; false (with d freed) on a missing or malformed file.
static bool df_read(DinoFile* d, const char* path) {
    memset(d, 0, sizeof *d);
    FILE* f = fopen(path, "rb"); if (!f) return false;
    uint32_t hdr[2], nj = 0, nmat = 0, nlod = 0, nclip = 0;
    bool ok = df_rd(f, hdr, 8) && hdr[0] == 0x4F4E4443u && hdr[1] == 1 && df_rd(f, &d->length, 4) && df_rd(f, &d->height, 4) && df_rd(f, d->bmin, 12) && df_rd(f, d->bmax, 12);
    ok = ok && df_rd(f, &nj, 4) && nj > 0 && nj <= DF_MAX_JOINTS;
    for (uint32_t j = 0; ok && j < nj; j++) {
        int32_t par = -1; float ib[16];
        ok = df_rd(f, d->name[j], 32) && df_rd(f, &par, 4) && df_rd(f, ib, 64) && df_rd(f, d->rest[j], 32) && par < (int32_t)j && par >= -1;
        d->parent[j] = par; d->name[j][31] = 0;
        for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) d->inv_bind[j][r * 4 + c] = ib[c * 4 + r];   // column-major 4x4 -> 3x4 rows
        for (int c = 0; c < 32; c++) d->low[j][c] = (char)(d->name[j][c] >= 'A' && d->name[j][c] <= 'Z' ? d->name[j][c] + 32 : d->name[j][c]);
    }
    d->njoint = (int)nj;
    ok = ok && df_rd(f, &nmat, 4) && nmat > 0 && nmat <= 8;
    for (uint32_t k = 0; ok && k < nmat; k++) { char nm[32]; uint32_t fl; ok = df_rd(f, nm, 32) && df_rd(f, &fl, 4); }
    ok = ok && df_rd(f, &nlod, 4) && nlod >= 1 && nlod <= 8;
    for (uint32_t l = 0; ok && l < nlod; l++) {
        uint32_t nv, ni, ns; ok = df_rd(f, &nv, 4) && df_rd(f, &ni, 4) && df_rd(f, &ns, 4) && nv > 0 && nv < (1u << 22) && ni > 0 && ni < (1u << 24) && ni % 3 == 0 && ns <= 8;
        if (!ok) break;
        uint32_t sub[8][3]; ok = df_rd(f, sub, 12 * ns);
        DfVert* vb = (DfVert*)malloc(sizeof(DfVert) * nv); uint32_t* ib = (uint32_t*)malloc(4 * (size_t)ni);
        ok = ok && vb && ib && df_rd(f, vb, sizeof(DfVert) * nv) && df_rd(f, ib, 4 * (size_t)ni);
        for (uint32_t i = 0; ok && i < ni; i++) if (ib[i] >= nv) ok = false;
        for (uint32_t i = 0; ok && i < nv; i++) for (int q = 0; q < 4; q++) if (vb[i].j[q] >= nj) ok = false;
        free(ib);
        if (ok && l == 0) { d->vert = vb; d->nvert = (int)nv; } else free(vb);
    }
    ok = ok && df_rd(f, &nclip, 4) && nclip <= 48;
    for (uint32_t c = 0; ok && c < nclip; c++) {
        DfClip* C = &d->clip[c]; uint32_t fl = 0, nf = 0;
        ok = df_rd(f, C->name, 32) && df_rd(f, &C->duration, 4) && df_rd(f, &C->speed, 4) && df_rd(f, &fl, 4) && df_rd(f, &nf, 4) && nf >= 1 && nf < 100000;
        if (!ok) break;
        C->name[31] = 0; C->loop = fl & 1; C->nframes = (int)nf;
        C->frames = (float*)malloc(sizeof(float) * 8 * nj * nf);
        ok = C->frames && df_rd(f, C->frames, sizeof(float) * 8 * nj * nf);
        d->nclip = (int)c + 1;
    }
    fclose(f);
    if (!ok) df_free(d);
    return ok;
}

// Local T, R, S -> 3x4 row-major [R*S | T], and the 3x4 product a * b.
static void df_trs(const float* t, const float* q, float s, float* o) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    o[0] = (1 - 2 * (y * y + z * z)) * s; o[1] = 2 * (x * y - z * w) * s; o[2] = 2 * (x * z + y * w) * s; o[3] = t[0];
    o[4] = 2 * (x * y + z * w) * s; o[5] = (1 - 2 * (x * x + z * z)) * s; o[6] = 2 * (y * z - x * w) * s; o[7] = t[1];
    o[8] = 2 * (x * z - y * w) * s; o[9] = 2 * (y * z + x * w) * s; o[10] = (1 - 2 * (x * x + y * y)) * s; o[11] = t[2];
}
static void df_mul(const float* a, const float* b, float* o) {
    float r[12];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        r[i * 4 + 3] = a[i * 4] * b[3] + a[i * 4 + 1] * b[7] + a[i * 4 + 2] * b[11] + a[i * 4 + 3];
    }
    memcpy(o, r, sizeof r);
}
// Model-space joint frames G[njoint][12] of a local pose (njoint x 8 floats).
static void df_globals(const DinoFile* d, const float* local, float (*G)[12]) {
    for (int j = 0; j < d->njoint; j++) {
        const float* l = local + j * 8; float L[12]; df_trs(l, l + 3, l[7], L);
        if (d->parent[j] < 0) memcpy(G[j], L, sizeof L); else df_mul(G[d->parent[j]], L, G[j]);
    }
}
#endif
