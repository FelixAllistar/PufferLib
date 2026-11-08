// GPU-ready texture caches on top of cache.h: a texture is stored with every
// mip level exactly as the driver holds it (BC3 blocks after the driver's
// compression, or raw texels) plus its sampling state, and comes back with
// one glCompressedTexImage / glTexImage call per level: no decode, resize,
// compression or mip generation. 2D textures and 2D arrays.
#ifndef CRET_TEXCACHE_H
#define CRET_TEXCACHE_H
#include "gfx.h"
#include "cache.h"

// Bump when any loader changes what it packs into a cached texture without
// the change showing in the key (the keys already hash input files and sizes).
#define CRET_TEX_CACHE_VERSION 1u

// A new texture from the cache item, or 0 on a miss (stale, corrupt, absent,
// or caching off).
GLuint cret_tex_cache_load(const char* name, uint64_t key);
// Reads back every level of tex and stores it. Formats: BC1-3 (s3tc, sRGB or
// not), RGBA8, SRGB8_ALPHA8, R32F, RG32F, RGBA16F. False if not stored.
bool cret_tex_cache_store(const char* name, uint64_t key, GLuint tex, GLenum target);
#endif
