// GPU-ready texture caches on top of cache.h. A texture is built on the CPU
// (texbuild.h: mips and BC3 blocks on threads), uploaded and stored with
// every level as the GPU takes it plus its sampling state; a hit comes back
// with one glCompressedTexImage / glTexImage call per level: no decode,
// resize, compression or mip generation. Nothing is read back from the GPU
// (Mesa's d3d12 driver on WSL crashed in later allocations after readbacks).
// 2D textures and 2D arrays.
#ifndef CRET_TEXCACHE_H
#define CRET_TEXCACHE_H
#include "gfx.h"
#include "cache.h"
#include "texbuild.h"

// Bump when any loader changes what it packs into a cached texture without
// the change showing in the key (the keys already hash input files and sizes).
#define CRET_TEX_CACHE_VERSION 2u

// A new texture from the cache item, or 0 on a miss (stale, corrupt, absent,
// or caching off).
GLuint cret_tex_cache_load(const char* name, uint64_t key);
// Builds a texture from level-0 texels (cret_tex_payload_build), uploads it
// and stores it under name (NULL: not cached). 0 if it could not be built.
GLuint cret_tex_build(const char* name, uint64_t key, GLenum target, GLenum ifmt, int w, int h, int layers, const void* const* texels,
                      bool mips, bool linear, bool repeat, float aniso);
// Uploads a payload made with cret_tex_payload_alloc (mips filled by the
// caller), stores it under name (NULL: not cached) and frees it.
GLuint cret_tex_from_payload(const char* name, uint64_t key, uint8_t* payload, size_t size);
#endif
