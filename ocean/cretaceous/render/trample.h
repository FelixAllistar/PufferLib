// Trample map: a small world-space render target around the camera that
// remembers where animals pushed the ground cover over. Each texel holds a
// bend vector (xz, length 0..~1.2, 1 = laid flat) and a slowly fading trail
// strength. Every frame one fullscreen pass (shaders/trample.fs) scrolls it
// with the camera, stamps the animals from the contact-shadow list
// (csm.h CretCsm.contact: the nearest 16 within 45 m, then small animals
// within 25 m in the slots left, view.c set_contacts) and relaxes
// everything else: blades follow a body quickly, spring part of the way
// back within about half a second, and a trail stays flattened for a few
// seconds. veg.vs reads it to bend grass, sward, ferns and scrub.
// Single header, included by render/veg.c only.
#ifndef CRET_TRAMPLE_H
#define CRET_TRAMPLE_H
#include "gfx.h"
#include "csm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CretTrample {
    GfxProgram prog; GfxTarget rt[2]; GfxMesh tri;
    int cur, res, ox, oz;   // origin of the map in whole texels
    float texel, last_time; bool ok, valid;
} CretTrample;

static bool cret_trample_init(CretTrample* t, const char* shader_dir, int res, float texel) {
    memset(t, 0, sizeof *t); t->res = res; t->texel = texel;
    char vs[256], fs[256]; snprintf(vs, sizeof vs, "%sfullscreen.vs", shader_dir); snprintf(fs, sizeof fs, "%strample.fs", shader_dir);
    if (!gfx_program_load(&t->prog, vs, fs) || !t->prog.id) return false;
    GLint prev; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    for (int i = 0; i < 2; i++) {
        if (!gfx_target(&t->rt[i], res, res, GL_RGBA16F, false)) return false;
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    t->tri = gfx_fullscreen_triangle(); t->ok = true;
    return true;
}

// Restores the caller's framebuffer, viewport and fixed-function state.
static void cret_trample_update(CretTrample* t, const CretCsm* c, const float cam[3], float time) {
    if (!t->ok) return;
    int nox = (int)floorf(cam[0] / t->texel) - t->res / 2, noz = (int)floorf(cam[2] / t->texel) - t->res / 2;
    float dt = time - t->last_time;
    bool reset = !t->valid || dt < 0 || dt > 0.5f || abs(nox - t->ox) > t->res / 2 || abs(noz - t->oz) > t->res / 2;
    if (reset) dt = 1.0f;
    if (dt <= 0) return;   // same frame again (a second view): keep the map
    GLint fbo, vp[4]; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo); glGetIntegerv(GL_VIEWPORT, vp);
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE), sc = glIsEnabled(GL_SCISSOR_TEST);
    int next = t->cur ^ 1;
    gfx_bind_target(&t->rt[next]);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    GfxProgram* p = &t->prog; glUseProgram(p->id);
    gfx_bind_tex(p, "t_prev", 0, GL_TEXTURE_2D, t->rt[t->cur].color);
    gfx_set2f(p, "u_shift", (float)(nox - t->ox), (float)(noz - t->oz));
    gfx_set2f(p, "u_origin", nox * t->texel, noz * t->texel);
    gfx_set1f(p, "u_texel", t->texel); gfx_set1f(p, "u_res", (float)t->res); gfx_set1f(p, "u_dt", dt);
    gfx_set1i(p, "u_reset", reset);
    int n = c ? c->ncontact : 0;
    float a[CSM_CONTACTS][4], b[CSM_CONTACTS][4];
    for (int i = 0; i < n; i++) { memcpy(a[i], c->contact[i], 16); memcpy(b[i], c->contact[i] + 4, 16); }
    gfx_set1i(p, "u_contact_n", n);
    if (n) { glUniform4fv(gfx_loc(p, "u_contact_a"), n, &a[0][0]); glUniform4fv(gfx_loc(p, "u_contact_b"), n, &b[0][0]); }
    gfx_draw(&t->tri);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo); glViewport(vp[0], vp[1], vp[2], vp[3]);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE);
    if (sc) glEnable(GL_SCISSOR_TEST);
    t->cur = next; t->ox = nox; t->oz = noz; t->last_time = time; t->valid = true;
    // CRET_TRAMPLE_DUMP=file.ppm: every 30th update writes the map (red/green: bend x/z, blue: trail).
    static int dump_n; const char* dump = getenv("CRET_TRAMPLE_DUMP");
    if (dump && ++dump_n % 30 == 0) {
        float* px = malloc(sizeof(float) * 4 * t->res * t->res); FILE* fp = fopen(dump, "wb");
        glBindTexture(GL_TEXTURE_2D, t->rt[t->cur].color); glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px);
        if (fp) {
            fprintf(fp, "P6 %d %d 255\n", t->res, t->res);
            for (int i = 0; i < t->res * t->res; i++) {
                unsigned char c[3] = {(unsigned char)fminf(fmaxf(128 + 127 * px[4 * i], 0), 255), (unsigned char)fminf(fmaxf(128 + 127 * px[4 * i + 1], 0), 255), (unsigned char)fminf(px[4 * i + 2] * 400, 255)};
                fwrite(c, 1, 3, fp);
            }
            fclose(fp);
        }
        free(px);
    }
}

// Binds the map for veg.vs: t_trample on `unit`, u_trample = (origin x, z,
// 1 / size, on). Off (or missing) binds nothing readable and turns it off.
static void cret_trample_bind(const CretTrample* t, const GfxProgram* p, int unit) {
    bool on = t && t->ok && t->valid;
    gfx_bind_tex(p, "t_trample", unit, GL_TEXTURE_2D, on ? t->rt[t->cur].color : 0);
    if (on) gfx_set4f(p, "u_trample", t->ox * t->texel, t->oz * t->texel, 1.0f / (t->res * t->texel), 1.0f);
    else gfx_set4f(p, "u_trample", 0, 0, 1, 0);
}

static void cret_trample_reload(CretTrample* t) { if (t->ok) { t->prog.vs_mtime = 0; gfx_program_reload_if_changed(&t->prog); } }

static void cret_trample_close(CretTrample* t) {
    if (t->ok) { gfx_program_free(&t->prog); gfx_target_free(&t->rt[0]); gfx_target_free(&t->rt[1]); gfx_mesh_free(&t->tri); }
    memset(t, 0, sizeof *t);
}
#endif
