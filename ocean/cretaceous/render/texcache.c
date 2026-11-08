// GPU-ready texture caches (texcache.h). Compiled by including it from view.c.
#include "texcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TC_MAGIC 0x31435854u   // "TXC1"
#define TC_ANISO 0x84FE        // GL_TEXTURE_MAX_ANISOTROPY

typedef struct { GLenum ifmt, fmt, type; int bpp, block; } TcFormat;   // block: bytes per 4x4 block (compressed), else 0
static const TcFormat TC_FORMATS[] = {
    {0x83F0, 0, 0, 0, 8}, {0x83F1, 0, 0, 0, 8}, {0x83F2, 0, 0, 0, 16}, {0x83F3, 0, 0, 0, 16},   // s3tc DXT1 rgb/rgba, DXT3, DXT5
    {0x8C4C, 0, 0, 0, 8}, {0x8C4D, 0, 0, 0, 8}, {0x8C4E, 0, 0, 0, 16}, {0x8C4F, 0, 0, 0, 16},   // sRGB variants
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0}, {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 0},
    {GL_R32F, GL_RED, GL_FLOAT, 4, 0}, {GL_RG32F, GL_RG, GL_FLOAT, 8, 0}, {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8, 0},
};
static const TcFormat* tc_format(GLenum ifmt) {
    for (size_t i = 0; i < sizeof TC_FORMATS / sizeof TC_FORMATS[0]; i++) if (TC_FORMATS[i].ifmt == ifmt) return &TC_FORMATS[i];
    return NULL;
}
static size_t tc_level_bytes(const TcFormat* f, int w, int h, int d) {
    if (f->block) return (size_t)((w + 3) / 4) * ((h + 3) / 4) * f->block * d;
    return (size_t)w * h * d * f->bpp;
}
static int tc_dim(int s, int l) { s >>= l; return s > 0 ? s : 1; }
static bool tc_is_mip_filter(GLint f) { return f == GL_LINEAR_MIPMAP_LINEAR || f == GL_LINEAR_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR || f == GL_NEAREST_MIPMAP_NEAREST; }

// Payload: u32 magic, target, ifmt, w, h, d, levels, wrap_s, wrap_t, min, mag, f32 aniso; then each level's bytes.
typedef struct { uint32_t magic, target, ifmt, w, h, d, levels, wrap_s, wrap_t, min, mag; float aniso; } TcHeader;

GLuint cret_tex_cache_load(const char* name, uint64_t key) {
    CretBlob b; if (!cret_cache_load(name, key, &b)) return 0;
    CretReader r = {b.data, b.size, 0, false};
    const TcHeader* hp = cret_read(&r, sizeof(TcHeader)); TcHeader h; if (hp) memcpy(&h, hp, sizeof h);
    const TcFormat* f = hp ? tc_format(h.ifmt) : NULL;
    bool ok = hp && f && h.magic == TC_MAGIC && (h.target == GL_TEXTURE_2D || h.target == GL_TEXTURE_2D_ARRAY) &&
              h.w >= 1 && h.h >= 1 && h.d >= 1 && h.w <= 16384 && h.h <= 16384 && h.d <= 2048 && h.levels >= 1 && h.levels <= 15 &&
              (h.target == GL_TEXTURE_2D_ARRAY || h.d == 1);
    // Every level present with its exact size, and nothing after them.
    size_t at0 = r.at;
    for (uint32_t l = 0; ok && l < h.levels; l++) ok = cret_read(&r, tc_level_bytes(f, tc_dim(h.w, l), tc_dim(h.h, l), h.d)) != NULL;
    ok = ok && r.at == r.n;
    if (!ok) { fprintf(stderr, "cache: %s has a bad texture payload, rebuilding\n", name); free(b.data); return 0; }
    r.at = at0;
    GLuint t; glGenTextures(1, &t); glBindTexture(h.target, t); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (uint32_t l = 0; l < h.levels; l++) {
        int w = tc_dim(h.w, l), hh = tc_dim(h.h, l); size_t n = tc_level_bytes(f, w, hh, h.d); const void* px = cret_read(&r, n);
        if (h.target == GL_TEXTURE_2D_ARRAY) {
            if (f->block) glCompressedTexImage3D(h.target, (GLint)l, h.ifmt, w, hh, h.d, 0, (GLsizei)n, px);
            else glTexImage3D(h.target, (GLint)l, h.ifmt, w, hh, h.d, 0, f->fmt, f->type, px);
        } else {
            if (f->block) glCompressedTexImage2D(h.target, (GLint)l, h.ifmt, w, hh, 0, (GLsizei)n, px);
            else glTexImage2D(h.target, (GLint)l, h.ifmt, w, hh, 0, f->fmt, f->type, px);
        }
    }
    glTexParameteri(h.target, GL_TEXTURE_BASE_LEVEL, 0); glTexParameteri(h.target, GL_TEXTURE_MAX_LEVEL, (GLint)h.levels - 1);
    glTexParameteri(h.target, GL_TEXTURE_WRAP_S, (GLint)h.wrap_s); glTexParameteri(h.target, GL_TEXTURE_WRAP_T, (GLint)h.wrap_t);
    glTexParameteri(h.target, GL_TEXTURE_MIN_FILTER, (GLint)h.min); glTexParameteri(h.target, GL_TEXTURE_MAG_FILTER, (GLint)h.mag);
    if (h.aniso > 1.0f) glTexParameterf(h.target, TC_ANISO, h.aniso);
    glBindTexture(h.target, 0);
    free(b.data);
    return t;
}

bool cret_tex_cache_store(const char* name, uint64_t key, GLuint tex, GLenum target) {
    if (!cret_cache_enabled() || !tex) return false;
    while (glGetError() != GL_NO_ERROR) {}   // earlier errors are not ours
    glBindTexture(target, tex);
    GLint w = 0, h = 0, d = 1, ifmt = 0, ws = 0, wt = 0, mn = 0, mg = 0, maxl = 1000; GLfloat an = 1;
    glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &w); glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &h);
    if (target == GL_TEXTURE_2D_ARRAY) glGetTexLevelParameteriv(target, 0, GL_TEXTURE_DEPTH, &d);
    glGetTexLevelParameteriv(target, 0, GL_TEXTURE_INTERNAL_FORMAT, &ifmt);
    glGetTexParameteriv(target, GL_TEXTURE_WRAP_S, &ws); glGetTexParameteriv(target, GL_TEXTURE_WRAP_T, &wt);
    glGetTexParameteriv(target, GL_TEXTURE_MIN_FILTER, &mn); glGetTexParameteriv(target, GL_TEXTURE_MAG_FILTER, &mg);
    glGetTexParameteriv(target, GL_TEXTURE_MAX_LEVEL, &maxl);
    if (gfx_has_ext("GL_EXT_texture_filter_anisotropic") || gfx_has_ext("GL_ARB_texture_filter_anisotropic")) glGetTexParameterfv(target, TC_ANISO, &an);
    const TcFormat* f = tc_format((GLenum)ifmt);
    if (!f || w < 1 || h < 1 || d < 1) { glBindTexture(target, 0); fprintf(stderr, "cache: %s: format 0x%x not cacheable\n", name, ifmt); return false; }
    int levels = 1;
    if (tc_is_mip_filter(mn)) { int m = w > h ? w : h; while ((m >> levels) > 0) levels++; if (maxl + 1 < levels) levels = maxl + 1; }
    TcHeader hd = {TC_MAGIC, target, (uint32_t)ifmt, (uint32_t)w, (uint32_t)h, (uint32_t)d, (uint32_t)levels, (uint32_t)ws, (uint32_t)wt, (uint32_t)mn, (uint32_t)mg, an};
    size_t total = 0; for (int l = 0; l < levels; l++) total += tc_level_bytes(f, tc_dim(w, l), tc_dim(h, l), d);
    uint8_t* buf = malloc(sizeof hd + total); if (!buf) { glBindTexture(target, 0); return false; }
    memcpy(buf, &hd, sizeof hd);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    size_t at = sizeof hd; bool ok = true;
    for (int l = 0; ok && l < levels; l++) {
        size_t n = tc_level_bytes(f, tc_dim(w, l), tc_dim(h, l), d);
        if (f->block) {
            GLint cs = 0; glGetTexLevelParameteriv(target, l, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &cs);
            if ((size_t)cs != n) { ok = false; break; }
            glGetCompressedTexImage(target, l, buf + at);
        } else glGetTexImage(target, l, f->fmt, f->type, buf + at);
        at += n;
    }
    glBindTexture(target, 0);
    ok = ok && glGetError() == GL_NO_ERROR;
    const void* parts[1] = {buf}; size_t sizes[1] = {sizeof hd + total};
    if (ok) ok = cret_cache_store(name, key, parts, sizes, 1);
    else fprintf(stderr, "cache: %s: could not read the texture back; not cached\n", name);
    free(buf);
    return ok;
}
