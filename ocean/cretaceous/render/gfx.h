// Thin OpenGL 3.3 helpers. Function pointers come from raylib's glad loader
// (vendor/glad.h maps names onto raylib's loaded symbols), so this works
// wherever raylib runs. Call only after InitWindow. The web build uses WebGL2
// through the GLES 3 headers instead (render/gl_web.h).
#ifndef CRET_GFX_H
#define CRET_GFX_H
#ifdef __EMSCRIPTEN__
#include "gl_web.h"
#else
#include "glad.h"
#endif
#include <stdbool.h>
#include <stdint.h>

typedef struct GfxProgram {
    GLuint id;
    char vs_path[256], fs_path[256];
    long vs_mtime, fs_mtime;
} GfxProgram;

typedef struct GfxTarget {      // framebuffer with optional colour and depth textures
    GLuint fbo, color, depth; int w, h;
} GfxTarget;

typedef struct GfxMesh { GLuint vao, vbo, ibo; int index_count; } GfxMesh;

// Shaders are files so they can be edited and hot-reloaded (F5 in the viewer).
bool gfx_program_load(GfxProgram* p, const char* vs_path, const char* fs_path);
bool gfx_program_reload_if_changed(GfxProgram* p);
// "#define NAME\n" lines inserted after every shader's #version line from now
// on (programs built or reloaded later); the debug console's fog switch.
void gfx_set_defines(const char* defines);
void gfx_program_free(GfxProgram* p);
GLint gfx_loc(const GfxProgram* p, const char* name);
void gfx_set1i(const GfxProgram* p, const char* n, int v);
void gfx_set1f(const GfxProgram* p, const char* n, float v);
void gfx_set2f(const GfxProgram* p, const char* n, float a, float b);
void gfx_set3f(const GfxProgram* p, const char* n, float a, float b, float c);
void gfx_set4f(const GfxProgram* p, const char* n, float a, float b, float c, float d);
void gfx_setmat4(const GfxProgram* p, const char* n, const float* m);
void gfx_bind_tex(const GfxProgram* p, const char* n, int unit, GLenum target, GLuint tex);

GLuint gfx_tex2d(int w, int h, GLenum ifmt, GLenum fmt, GLenum type, const void* data, bool linear, bool mips, bool repeat);
GLuint gfx_tex3d(int w, int h, int d, GLenum ifmt, GLenum fmt, GLenum type, const void* data, bool repeat);
// True if the GL context lists the extension.
bool gfx_has_ext(const char* name);
// BC3 (DXT5) texture compression available and not disabled (CRET_TEX_COMPRESS=0).
bool gfx_use_bc3(void);
GLuint gfx_tex_array(int w, int h, int layers, GLenum ifmt, GLenum fmt, GLenum type, const void* const* layer_data);
bool gfx_target(GfxTarget* t, int w, int h, GLenum color_ifmt, bool depth);
void gfx_target_free(GfxTarget* t);
void gfx_bind_target(const GfxTarget* t);   // NULL = default framebuffer

GfxMesh gfx_grid_mesh(int n);               // (n+1)^2 vertices, attribute 0 = vec2 grid coords
GfxMesh gfx_fullscreen_triangle(void);
void gfx_draw(const GfxMesh* m);
void gfx_mesh_free(GfxMesh* m);
#endif
