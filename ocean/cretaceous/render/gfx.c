#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static long file_mtime(const char* path) { struct stat st; return stat(path, &st) == 0 ? (long)st.st_mtime : 0; }

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char* s = malloc(n + 1); if (fread(s, 1, n, f) != (size_t)n) { free(s); fclose(f); return NULL; }
    s[n] = 0; fclose(f); return s;
}

static char g_defines[256];   // "#define X\n" lines added after every shader's #version (debug switches)
void gfx_set_defines(const char* defines) { snprintf(g_defines, sizeof g_defines, "%s", defines ? defines : ""); }

// Expands `#include "file"` lines (recursively) relative to the including file.
static char* load_source(const char* path);
static char* load_source(const char* path) {
    char* src = read_file(path); if (!src) { fprintf(stderr, "gfx: cannot read %s\n", path); return NULL; }
    char dir[256]; snprintf(dir, sizeof dir, "%s", path); char* slash = strrchr(dir, '/'); if (slash) slash[1] = 0; else dir[0] = 0;
    size_t cap = strlen(src) * 2 + 4096, len = 0; char* out = malloc(cap); out[0] = 0;
    char* line = src;
    while (*line) {
        char* end = strchr(line, '\n'); size_t ll = end ? (size_t)(end - line + 1) : strlen(line);
        char inc[256];
        if (sscanf(line, "#include \"%255[^\"]\"", inc) == 1) {
            char ip[512]; snprintf(ip, sizeof ip, "%s%s", dir, inc);
            char* body = load_source(ip);       // nested includes resolve relative to their file
            if (!body) { fprintf(stderr, "gfx: cannot include %s\n", ip); body = calloc(1, 1); }
            size_t bl = strlen(body);
            while (len + bl + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
            memcpy(out + len, body, bl); len += bl; out[len++] = '\n'; out[len] = 0; free(body);
        } else {
            while (len + ll + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
#ifdef __EMSCRIPTEN__
            // WebGL2: the desktop "#version 330" line becomes GLSL ES 3.00 plus
            // the precision prelude in shaders/web_es.glsl, so the shader files
            // stay the single source for both builds.
            if (!strncmp(line, "#version", 8)) {
                char wp[512]; snprintf(wp, sizeof wp, "%sweb_es.glsl", dir);
                char* pre = read_file(wp); if (!pre) { fprintf(stderr, "gfx: cannot read %s\n", wp); pre = calloc(1, 1); }
                const char* ver = "#version 300 es\n"; size_t vl = strlen(ver), pl = strlen(pre);
                while (len + vl + pl + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                memcpy(out + len, ver, vl); len += vl; memcpy(out + len, pre, pl); len += pl; out[len] = 0; free(pre);
            } else { memcpy(out + len, line, ll); len += ll; out[len] = 0; }
#else
            memcpy(out + len, line, ll); len += ll; out[len] = 0;
#endif
            if (!strncmp(line, "#version", 8) && g_defines[0]) {   // global switches (gfx_set_defines) follow the version line
                size_t dl = strlen(g_defines);
                while (len + dl + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                if (out[len - 1] != '\n') out[len++] = '\n';
                memcpy(out + len, g_defines, dl); len += dl; out[len] = 0;
            }
        }
        line += ll;
    }
    free(src); return out;
}

static GLuint compile(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "gfx: %s:\n%s\n", name, log); glDeleteShader(s); return 0; }
    return s;
}

static GLuint link_program(const char* vsp, const char* fsp) {
    char* vs = load_source(vsp); char* fs = load_source(fsp);
    if (!vs || !fs) { free(vs); free(fs); return 0; }
    GLuint v = compile(GL_VERTEX_SHADER, vs, vsp), f = compile(GL_FRAGMENT_SHADER, fs, fsp);
    free(vs); free(fs);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
    glBindAttribLocation(p, 0, "a_pos");     // raylib mesh layout: 0 pos, 1 uv, 2 normal
    glBindAttribLocation(p, 1, "a_uv");
    glBindAttribLocation(p, 2, "a_normal");
    glLinkProgram(p); glDeleteShader(v); glDeleteShader(f);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, NULL, log); fprintf(stderr, "gfx: link %s + %s:\n%s\n", vsp, fsp, log); glDeleteProgram(p); return 0; }
    return p;
}

bool gfx_program_load(GfxProgram* p, const char* vs, const char* fs) {
    memset(p, 0, sizeof(*p));
    snprintf(p->vs_path, sizeof p->vs_path, "%s", vs); snprintf(p->fs_path, sizeof p->fs_path, "%s", fs);
    p->vs_mtime = file_mtime(vs); p->fs_mtime = file_mtime(fs);
    p->id = link_program(vs, fs); return p->id != 0;
}
bool gfx_program_reload_if_changed(GfxProgram* p) {
    long a = file_mtime(p->vs_path), b = file_mtime(p->fs_path);
    if (a == p->vs_mtime && b == p->fs_mtime) return false;
    p->vs_mtime = a; p->fs_mtime = b;
    GLuint id = link_program(p->vs_path, p->fs_path);
    if (id) { if (p->id) glDeleteProgram(p->id); p->id = id; fprintf(stderr, "gfx: reloaded %s\n", p->fs_path); return true; }
    return false;
}
void gfx_program_free(GfxProgram* p) { if (p->id) glDeleteProgram(p->id); p->id = 0; }
GLint gfx_loc(const GfxProgram* p, const char* n) { return glGetUniformLocation(p->id, n); }
void gfx_set1i(const GfxProgram* p, const char* n, int v) { glUniform1i(gfx_loc(p, n), v); }
void gfx_set1f(const GfxProgram* p, const char* n, float v) { glUniform1f(gfx_loc(p, n), v); }
void gfx_set2f(const GfxProgram* p, const char* n, float a, float b) { glUniform2f(gfx_loc(p, n), a, b); }
void gfx_set3f(const GfxProgram* p, const char* n, float a, float b, float c) { glUniform3f(gfx_loc(p, n), a, b, c); }
void gfx_set4f(const GfxProgram* p, const char* n, float a, float b, float c, float d) { glUniform4f(gfx_loc(p, n), a, b, c, d); }
void gfx_setmat4(const GfxProgram* p, const char* n, const float* m) { glUniformMatrix4fv(gfx_loc(p, n), 1, GL_FALSE, m); }
void gfx_bind_tex(const GfxProgram* p, const char* n, int unit, GLenum target, GLuint tex) {
    glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(target, tex); glUniform1i(gfx_loc(p, n), unit);
}

GLuint gfx_tex2d(int w, int h, GLenum ifmt, GLenum fmt, GLenum type, const void* data, bool linear, bool mips, bool repeat) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, data);
    GLenum wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : (linear ? GL_LINEAR : GL_NEAREST));
    if (mips) glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0); return t;
}
GLuint gfx_tex3d(int w, int h, int d, GLenum ifmt, GLenum fmt, GLenum type, const void* data, bool repeat) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_3D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, ifmt, w, h, d, 0, fmt, type, data);
    GLenum wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, wrap); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, wrap);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_3D);
    glBindTexture(GL_TEXTURE_3D, 0); return t;
}
bool gfx_has_ext(const char* name) {
    GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) { const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i); if (e && !strcmp(e, name)) return true; }
    return false;
}
bool gfx_use_bc3(void) {
    const char* e = getenv("CRET_TEX_COMPRESS");
    if (e && atoi(e) == 0) return false;
#ifdef PLATFORM_WEB
    // WebGL names it WEBGL_compressed_texture_s3tc (Emscripten adds "GL_"),
    // and the sRGB blocks (TB_BC3_SRGB) are a separate extension. Desktop
    // browsers have both; phones have neither and keep RGBA8.
    return gfx_has_ext("GL_WEBGL_compressed_texture_s3tc") && gfx_has_ext("GL_WEBGL_compressed_texture_s3tc_srgb");
#else
    return gfx_has_ext("GL_EXT_texture_compression_s3tc");
#endif
}

GLuint gfx_tex_array(int w, int h, int layers, GLenum ifmt, GLenum fmt, GLenum type, const void* const* layer_data) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D_ARRAY, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, ifmt, w, h, layers, 0, fmt, type, NULL);
    for (int l = 0; l < layers; l++) if (layer_data[l]) glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, l, w, h, 1, fmt, type, layer_data[l]);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0); return t;
}

bool gfx_target(GfxTarget* t, int w, int h, GLenum color_ifmt, bool depth) {
    memset(t, 0, sizeof(*t)); t->w = w; t->h = h;
    glGenFramebuffers(1, &t->fbo); glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    if (color_ifmt) {
        GLenum type = (color_ifmt == GL_RGBA16F || color_ifmt == GL_R11F_G11F_B10F || color_ifmt == GL_RGBA32F) ? GL_FLOAT : GL_UNSIGNED_BYTE;
        t->color = gfx_tex2d(w, h, color_ifmt, GL_RGBA, type, NULL, true, false, false);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->color, 0);
    }
    if (depth) {
        glGenTextures(1, &t->depth); glBindTexture(GL_TEXTURE_2D, t->depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t->depth, 0);
    }
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "gfx: framebuffer incomplete 0x%x\n", st); return false; }
    return true;
}
void gfx_target_free(GfxTarget* t) {
    if (t->color) glDeleteTextures(1, &t->color);
    if (t->depth) glDeleteTextures(1, &t->depth);
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    memset(t, 0, sizeof(*t));
}
void gfx_bind_target(const GfxTarget* t) {
    if (t) { glBindFramebuffer(GL_FRAMEBUFFER, t->fbo); glViewport(0, 0, t->w, t->h); }
    else glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GfxMesh gfx_grid_mesh(int n) {
    GfxMesh m = {0}; int vn = (n + 1) * (n + 1);
    float* v = malloc(sizeof(float) * 2 * vn);
    for (int z = 0; z <= n; z++) for (int x = 0; x <= n; x++) { v[2 * (z * (n + 1) + x)] = (float)x; v[2 * (z * (n + 1) + x) + 1] = (float)z; }
    unsigned* idx = malloc(sizeof(unsigned) * n * n * 6); int k = 0;
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        unsigned a = z * (n + 1) + x, b = a + 1, c = a + (n + 1), d = c + 1;
        // Alternate the diagonal for a more isotropic tessellation.
        if ((x + z) & 1) { idx[k++] = a; idx[k++] = c; idx[k++] = b; idx[k++] = b; idx[k++] = c; idx[k++] = d; }
        else { idx[k++] = a; idx[k++] = c; idx[k++] = d; idx[k++] = a; idx[k++] = d; idx[k++] = b; }
    }
    glGenVertexArrays(1, &m.vao); glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo); glBindBuffer(GL_ARRAY_BUFFER, m.vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * vn, v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glGenBuffers(1, &m.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned) * k, idx, GL_STATIC_DRAW);
    glBindVertexArray(0); m.index_count = k; free(v); free(idx);
    return m;
}
GfxMesh gfx_fullscreen_triangle(void) {
    GfxMesh m = {0}; float v[6] = {-1, -1, 3, -1, -1, 3};
    glGenVertexArrays(1, &m.vao); glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo); glBindBuffer(GL_ARRAY_BUFFER, m.vbo); glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glBindVertexArray(0); m.index_count = -3; return m;
}
void gfx_draw(const GfxMesh* m) {
    glBindVertexArray(m->vao);
    if (m->index_count < 0) glDrawArrays(GL_TRIANGLES, 0, -m->index_count);
    else glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
void gfx_mesh_free(GfxMesh* m) {
    if (m->vao) glDeleteVertexArrays(1, &m->vao);
    if (m->vbo) glDeleteBuffers(1, &m->vbo);
    if (m->ibo) glDeleteBuffers(1, &m->ibo);
    memset(m, 0, sizeof(*m));
}
