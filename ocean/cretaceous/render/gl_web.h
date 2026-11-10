// WebGL2 (OpenGL ES 3.0) stand-in for vendor/glad.h, used only by the
// Emscripten build (render/gfx.h includes it under __EMSCRIPTEN__).
// The renderer is written against desktop GL 3.3. ES 3.0 has nearly all of
// it; this header supplies the few names it lacks and turns the calls that
// WebGL2 has no equivalent for into no-ops, so the desktop code compiles
// unchanged:
//   - timer queries (GL_TIME_ELAPSED): WebGL2 exposes them only through an
//     extension that browsers often hide, so profiling reads 0 ms;
//   - glPolygonMode (wireframe): ignored;
//   - GL_DEPTH_CLAMP: ignored, so shadow casters in front of a cascade's box
//     are clipped (a little missing shadow near the camera at most);
//   - GL_FRAMEBUFFER_SRGB: ES always encodes writes to sRGB attachments, so
//     enabling it is a no-op with the same result;
//   - glGetTexImage: ES cannot read a texture back; the caller gets zeros;
//   - glClearDepth (double) maps to glClearDepthf, glDrawBuffer to glDrawBuffers.
#ifndef CRET_GL_WEB_H
#define CRET_GL_WEB_H
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <string.h>

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP 0x864F
#endif
#ifndef GL_LINE
#define GL_LINE 0x1B01
#endif
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif

// Capabilities ES lacks are dropped before they reach WebGL (which would
// report GL_INVALID_ENUM for each).
static inline int cret_web_cap_ok(GLenum cap) { return cap != GL_DEPTH_CLAMP && cap != GL_FRAMEBUFFER_SRGB; }
static inline void cret_web_enable(GLenum cap) { if (cret_web_cap_ok(cap)) glEnable(cap); }
static inline void cret_web_disable(GLenum cap) { if (cret_web_cap_ok(cap)) glDisable(cap); }
#define glEnable cret_web_enable
#define glDisable cret_web_disable

static inline void cret_web_begin_query(GLenum target, GLuint id) { if (target != GL_TIME_ELAPSED) glBeginQuery(target, id); }
static inline void cret_web_end_query(GLenum target) { if (target != GL_TIME_ELAPSED) glEndQuery(target); }
#define glBeginQuery cret_web_begin_query
#define glEndQuery cret_web_end_query
static inline void glGetQueryObjectui64v(GLuint id, GLenum pname, GLuint64* out) { (void)id; (void)pname; *out = 0; }

static inline void glPolygonMode(GLenum face, GLenum mode) { (void)face; (void)mode; }
static inline void glClearDepth(double d) { glClearDepthf((float)d); }
static inline void glDrawBuffer(GLenum b) { glDrawBuffers(1, &b); }
static inline void glGetTexImage(GLenum target, GLint level, GLenum fmt, GLenum type, void* out) {
    (void)target; (void)level; (void)fmt; (void)type; (void)out;   // size unknown here; callers on web do not rely on it
}
#endif
