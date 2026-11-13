// GPU-ready texture caches (texcache.h). Compiled by including it from view.c.
#include "texcache.h"
#include "texbuild.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { GLenum ifmt, fmt, type; int block; } TcFormat;   // block: bytes per 4x4 block (compressed), else 0
static const TcFormat TC_FORMATS[] = {
    {0x83F0, 0, 0, 8}, {0x83F1, 0, 0, 8}, {0x83F2, 0, 0, 16}, {0x83F3, 0, 0, 16},   // s3tc DXT1 rgb/rgba, DXT3, DXT5
    {0x8C4C, 0, 0, 8}, {0x8C4D, 0, 0, 8}, {0x8C4E, 0, 0, 16}, {0x8C4F, 0, 0, 16},   // sRGB variants
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 0}, {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 0},
    {GL_R32F, GL_RED, GL_FLOAT, 0}, {GL_RG32F, GL_RG, GL_FLOAT, 0}, {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 0},
};
static const TcFormat* tc_format(GLenum ifmt) {
    for (size_t i = 0; i < sizeof TC_FORMATS / sizeof TC_FORMATS[0]; i++) if (TC_FORMATS[i].ifmt == ifmt) return &TC_FORMATS[i];
    return NULL;
}
static int tc_dim(int s, int l) { s >>= l; return s > 0 ? s : 1; }

// A texture from a payload (texbuild.h layout), checked first: 0 if it is
// malformed (every level present with its exact size, nothing after them).
static GLuint tc_upload(const uint8_t* data, size_t size, const char* name) {
    CretReader r = {data, size, 0, false};
    const TcHeader* hp = cret_read(&r, sizeof(TcHeader)); TcHeader h; if (hp) memcpy(&h, hp, sizeof h);
    const TcFormat* f = hp ? tc_format(h.ifmt) : NULL;
    bool ok = hp && f && h.magic == TC_MAGIC && (h.target == GL_TEXTURE_2D || h.target == GL_TEXTURE_2D_ARRAY) &&
              h.w >= 1 && h.h >= 1 && h.d >= 1 && h.w <= 16384 && h.h <= 16384 && h.d <= 2048 && h.levels >= 1 && h.levels <= 15 &&
              (h.target == GL_TEXTURE_2D_ARRAY || h.d == 1);
    size_t at0 = r.at;
    for (uint32_t l = 0; ok && l < h.levels; l++) ok = cret_read(&r, cret_tex_level_bytes(h.ifmt, tc_dim(h.w, l), tc_dim(h.h, l), h.d)) != NULL;
    ok = ok && r.at == r.n;
    if (!ok) { fprintf(stderr, "cache: %s has a bad texture payload, rebuilding\n", name); return 0; }
    r.at = at0;
    GLint unpack = 4; glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    GLuint t; glGenTextures(1, &t); glBindTexture(h.target, t); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (uint32_t l = 0; l < h.levels; l++) {
        int w = tc_dim(h.w, l), hh = tc_dim(h.h, l); size_t n = cret_tex_level_bytes(h.ifmt, w, hh, h.d); const void* px = cret_read(&r, n);
        if (h.target == GL_TEXTURE_2D_ARRAY) {
            if (f->block) glCompressedTexImage3D(h.target, (GLint)l, h.ifmt, w, hh, h.d, 0, (GLsizei)n, px);
            else glTexImage3D(h.target, (GLint)l, h.ifmt, w, hh, h.d, 0, f->fmt, f->type, px);
        } else {
            if (f->block) glCompressedTexImage2D(h.target, (GLint)l, h.ifmt, w, hh, 0, (GLsizei)n, px);
            else glTexImage2D(h.target, (GLint)l, h.ifmt, w, hh, 0, f->fmt, f->type, px);
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
    glTexParameteri(h.target, GL_TEXTURE_BASE_LEVEL, 0); glTexParameteri(h.target, GL_TEXTURE_MAX_LEVEL, (GLint)h.levels - 1);
    glTexParameteri(h.target, GL_TEXTURE_WRAP_S, (GLint)h.wrap_s); glTexParameteri(h.target, GL_TEXTURE_WRAP_T, (GLint)h.wrap_t);
    glTexParameteri(h.target, GL_TEXTURE_MIN_FILTER, (GLint)h.min); glTexParameteri(h.target, GL_TEXTURE_MAG_FILTER, (GLint)h.mag);
    gfx_set_aniso(h.target, h.aniso);   // clamped, and skipped where the context has no anisotropy
    glBindTexture(h.target, 0);
    return t;
}

GLuint cret_tex_cache_load(const char* name, uint64_t key) {
    CretBlob b; if (!cret_cache_load(name, key, &b)) return 0;
    GLuint t = tc_upload(b.data, b.size, name);
    free(b.data);
    return t;
}

GLuint cret_tex_from_payload(const char* name, uint64_t key, uint8_t* payload, size_t size) {
    if (!payload) return 0;
    GLuint t = tc_upload(payload, size, name ? name : "texture");
    if (t && name) { const void* parts[1] = {payload}; cret_cache_store(name, key, parts, &size, 1); }
    free(payload);
    return t;
}

GLuint cret_tex_build(const char* name, uint64_t key, GLenum target, GLenum ifmt, int w, int h, int layers, const void* const* texels,
                      bool mips, bool linear, bool repeat, float aniso) {
    size_t size = 0;
    uint8_t* p = cret_tex_payload_build(target, ifmt, w, h, layers, texels, mips, linear, repeat, aniso, &size);
    if (!p) { fprintf(stderr, "cretaceous: texture %s (format 0x%x) could not be built\n", name ? name : "?", ifmt); return 0; }
    return cret_tex_from_payload(name, key, p, size);
}
