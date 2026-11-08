// GPU-ready texture payloads built on the CPU (no GL): mip chains and BC3
// (DXT5) block compression on threads, in the layout texcache.c uploads and
// caches. Building them here, rather than letting the driver compress and
// reading the result back, keeps cold starts off Mesa's d3d12 (WSL) readback
// path, which broke later allocations, and is several times faster than the
// driver's single-threaded compression.
#ifndef CRET_TEXBUILD_H
#define CRET_TEXBUILD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// GL enums (texbuild.c does not include GL).
#define TB_TEX_2D 0x0DE1u
#define TB_TEX_2D_ARRAY 0x8C1Au
#define TB_RGBA8 0x8058u
#define TB_SRGB8_A8 0x8C43u
#define TB_BC3 0x83F3u        // COMPRESSED_RGBA_S3TC_DXT5
#define TB_BC3_SRGB 0x8C4Fu   // COMPRESSED_SRGB_ALPHA_S3TC_DXT5
#define TB_R32F 0x822Eu
#define TB_RG32F 0x8230u

// Payload: this header, then each level's bytes (all layers, layer-major).
typedef struct { uint32_t magic, target, ifmt, w, h, d, levels, wrap_s, wrap_t, min, mag; float aniso; } TcHeader;
#define TC_MAGIC 0x31435854u   // "TXC1"

// Bytes of one level (all d layers); 0 for a format this file does not know.
size_t cret_tex_level_bytes(uint32_t ifmt, int w, int h, int d);
// A zeroed payload with its header filled: levels 0 means a full mip chain.
// Sampling: linear (or nearest), trilinear when it has mips, repeat (or clamp).
uint8_t* cret_tex_payload_alloc(uint32_t target, uint32_t ifmt, int w, int h, int d, int levels, bool linear, bool repeat, float aniso, size_t* size);
// Start of level l's bytes in a payload.
uint8_t* cret_tex_payload_level(uint8_t* payload, int l);
// A payload from level-0 texels, one pointer per layer:
//   RGBA8, SRGB8_ALPHA8, BC3, BC3 sRGB: w*h RGBA8 texels. With mips, a 2x2
//     box filter (sRGB formats averaged in linear light); BC3 compressed.
//   R32F, RG32F: raw floats, one level (mips is ignored).
// NULL on an unknown format or out of memory.
uint8_t* cret_tex_payload_build(uint32_t target, uint32_t ifmt, int w, int h, int layers, const void* const* texels,
                                bool mips, bool linear, bool repeat, float aniso, size_t* size);

// One 4x4 block (rows of 4 RGBA8 texels) to BC3, and back (tests).
void cret_bc3_encode(const uint8_t rgba[64], uint8_t out[16]);
void cret_bc3_decode(const uint8_t in[16], uint8_t rgba[64]);
#endif
