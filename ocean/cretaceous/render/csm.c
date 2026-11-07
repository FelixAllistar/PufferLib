// Cascaded sun shadow maps (see csm.h).
#include "csm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float SPLITS_HIGH[] = {0.0f, 20.0f, 60.0f, 200.0f, 800.0f};
static const float SPLITS_LOW[] = {0.0f, 22.0f, 80.0f, 240.0f};

static float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void norm3(float* a) { float l = sqrtf(dot3(a, a)); if (l > 1e-8f) { a[0] /= l; a[1] /= l; a[2] /= l; } }
static void cross3(const float* a, const float* b, float* o) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}

bool cret_csm_init(CretCsm* c, int quality) {
    memset(c, 0, sizeof *c);
    c->requested = quality;
    c->quality = quality < 0 ? 0 : quality > 2 ? 2 : quality;
    c->debug = getenv("CRET_SHADOW_DEBUG") != NULL;
    c->stagger = !(getenv("CRET_SHADOW_STAGGER") && atoi(getenv("CRET_SHADOW_STAGGER")) == 0);
    // Dummy 1x1 depth array: receivers always have a valid shadow sampler bound.
    glGenTextures(1, &c->dummy); glBindTexture(GL_TEXTURE_2D_ARRAY, c->dummy);
    float one = 1.0f;
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, 1, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &one);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    if (c->debug) glGenQueries(CSM_MAX + 1, c->q);
    if (c->quality == 0) { glBindTexture(GL_TEXTURE_2D_ARRAY, 0); return true; }
    const float* sp = c->quality == 2 ? SPLITS_HIGH : SPLITS_LOW;
    c->count = c->quality == 2 ? 4 : 3; c->res = c->quality == 2 ? 2048 : 1024;
    if (getenv("CRET_SHADOW_RES")) { int r = atoi(getenv("CRET_SHADOW_RES")); if (r >= 256 && r <= 4096) c->res = r; }
    if (getenv("CRET_SHADOW_CASCADES")) { int n = atoi(getenv("CRET_SHADOW_CASCADES")); if (n >= 1 && n <= c->count) c->count = n; }
    for (int k = 0; k <= c->count; k++) c->split[k] = sp[k];
    if (getenv("CRET_SHADOW_FAR")) { float f = (float)atof(getenv("CRET_SHADOW_FAR")); if (f > c->split[c->count - 1] + 10) c->split[c->count] = f; }
    glGenTextures(1, &c->tex); glBindTexture(GL_TEXTURE_2D_ARRAY, c->tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, c->res, c->res, c->count, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glGenFramebuffers(c->count, c->fbo);
    bool ok = true;
    for (int k = 0; k < c->count; k++) {
        glBindFramebuffer(GL_FRAMEBUFFER, c->fbo[k]);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, c->tex, 0, k);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        fprintf(stderr, "cretaceous: shadow map framebuffer incomplete, shadows off\n");
        cret_csm_close(c); cret_csm_init(c, 0); c->requested = quality; return false;
    }
    fprintf(stderr, "cretaceous: sun shadows %d cascades at %d^2 to %.0f m\n", c->count, c->res, c->split[c->count]);
    return true;
}

bool cret_csm_setup(CretCsm* c, const float cam[3], const float fwd_in[3], float fov_y_deg, float aspect, const float sun[3], const float* vp) {
    c->active = false;
    if (c->quality == 0 || sun[1] < 0.02f) { for (int k = 0; k < CSM_MAX; k++) c->valid[k] = false; return false; }
    c->frame++;
    // Camera frustum planes from the column-major view-projection (rows r_i = m[i], m[4+i], ...).
    for (int i = 0; i < 6; i++) {
        int row = i / 2; float sg = (i & 1) ? -1.0f : 1.0f, l = 0;
        for (int j = 0; j < 4; j++) c->view_planes[i][j] = vp[j * 4 + 3] + sg * vp[j * 4 + row];
        for (int j = 0; j < 3; j++) l += c->view_planes[i][j] * c->view_planes[i][j];
        l = 1.0f / sqrtf(fmaxf(l, 1e-20f)); for (int j = 0; j < 4; j++) c->view_planes[i][j] *= l;
    }
    // Light basis like a look-at camera at the sun: x right, y up, z away from the sun.
    float F[3] = {-sun[0], -sun[1], -sun[2]}; norm3(F);
    float upr[3] = {0, 1, 0}; if (fabsf(F[1]) > 0.995f) { upr[0] = 1; upr[1] = 0; }
    float R[3], U[3]; cross3(F, upr, R); norm3(R); cross3(R, F, U);
    memcpy(c->right, R, sizeof R); memcpy(c->up, U, sizeof U); memcpy(c->fwd, F, sizeof F);
    float fw[3] = {fwd_in[0], fwd_in[1], fwd_in[2]}; norm3(fw);
    float tv = tanf(fov_y_deg * 0.5f * 3.14159265f / 180.0f), th = tv * aspect, t2 = tv * tv + th * th;
    // Casters toward the sun: anything whose shadow can reach the sphere
    // (tall trees, low sun), capped so a sunset does not pull in the island.
    float elev = asinf(fminf(fmaxf(sun[1], -1.0f), 1.0f));
    float reach = fminf(45.0f / fmaxf(sinf(elev), 0.08f), 600.0f);
    for (int k = 0; k < c->count; k++) {
        float n = c->split[k], f = c->split[k + 1];
        // The farthest two cascades (one with 3) update on alternate frames;
        // a cascade whose fit moved by more than 5% of its radius, or whose
        // sun direction changed, updates at once (teleports, fast turns).
        int far0 = c->count >= 4 ? c->count - 2 : c->count - 1;
        bool due = !c->stagger || k < far0 || !c->valid[k] || dot3(F, c->fit_fwd[k]) < 0.99998f ||
                   ((c->frame + (unsigned)(k - far0)) & 1u) == 0;
        // Smallest sphere through the slice's near and far corners, centred on the view axis.
        float d = fminf((f + n) * (1.0f + t2) * 0.5f, f);
        float r = sqrtf((f - d) * (f - d) + f * f * t2);
        r = ceilf(r * 4.0f) / 4.0f;                    // fixed size: texel size never changes
        float cw[3] = {cam[0] + fw[0] * d, cam[1] + fw[1] * d, cam[2] + fw[2] * d};
        float texel = 2.0f * r / c->res;
        float lx = dot3(R, cw), ly = dot3(U, cw), lz = dot3(F, cw);
        lx = floorf(lx / texel) * texel; ly = floorf(ly / texel) * texel;   // texel snap: no shimmer
        float zn = lz - r - reach, zf = lz + r;
        if (!due && (fabsf(lx - c->cx[k]) > 0.05f * r || fabsf(ly - c->cy[k]) > 0.05f * r || fabsf(zf - c->zfar[k]) > 0.05f * r)) due = true;
        c->due[k] = due;
        if (!due) continue;
        c->valid[k] = true; memcpy(c->fit_fwd[k], F, sizeof F);
        c->cx[k] = lx; c->cy[k] = ly; c->radius[k] = r; c->znear[k] = zn; c->zfar[k] = zf; c->texel[k] = texel;
        float* m = c->vp[k]; memset(m, 0, sizeof(float) * 16);
        float s = 2.0f / (zf - zn);
        m[0] = R[0] / r; m[4] = R[1] / r; m[8] = R[2] / r; m[12] = -lx / r;
        m[1] = U[0] / r; m[5] = U[1] / r; m[9] = U[2] / r; m[13] = -ly / r;
        m[2] = F[0] * s; m[6] = F[1] * s; m[10] = F[2] * s; m[14] = -zn * s - 1.0f;
        m[15] = 1.0f;
    }
    c->active = true;
    return true;
}

void cret_csm_to_light(const CretCsm* c, const float p[3], float o[3]) {
    o[0] = dot3(c->right, p); o[1] = dot3(c->up, p); o[2] = dot3(c->fwd, p);
}

bool cret_csm_caster_visible(const CretCsm* c, int k, const float p[3], float rad, float h) {
    float l[3]; cret_csm_to_light(c, p, l); float r = c->radius[k] + rad;
    if (fabsf(l[0] - c->cx[k]) >= r || fabsf(l[1] - c->cy[k]) >= r || l[2] > c->zfar[k] + rad) return false;
    // Toward the sun: its shadow reaches about h / sin(elevation) along the ray.
    float reach = fminf((h + rad) / fmaxf(-c->fwd[1], 0.08f), c->zfar[k] - 2.0f * c->radius[k] - c->znear[k]);
    if (l[2] <= c->zfar[k] - 2.0f * c->radius[k] - reach - rad) return false;
    // The shadow (caster swept away from the sun by its reach) must touch the view frustum.
    float q[3] = {p[0] + c->fwd[0] * reach, p[1] + c->fwd[1] * reach, p[2] + c->fwd[2] * reach};
    for (int i = 0; i < 6; i++) {
        const float* pl = c->view_planes[i];
        float d0 = pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3], d1 = pl[0] * q[0] + pl[1] * q[1] + pl[2] * q[2] + pl[3];
        if (fmaxf(d0, d1) < -rad) return false;
    }
    return true;
}

bool cret_csm_caster_inside(const CretCsm* c, int k, const float p[3], float rad) {
    float l[3]; cret_csm_to_light(c, p, l); float r = c->radius[k] - rad;
    if (r <= 0 || fabsf(l[0] - c->cx[k]) > r || fabsf(l[1] - c->cy[k]) > r) return false;
    if (l[2] - rad < c->zfar[k] - 2.0f * c->radius[k] || l[2] + rad > c->zfar[k]) return false;
    for (int i = 0; i < 6; i++) {
        const float* pl = c->view_planes[i];
        if (pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] < rad) return false;
    }
    return true;
}

void cret_csm_bounds(const CretCsm* c, int k, float lo[3], float hi[3]) {
    for (int a = 0; a < 3; a++) { lo[a] = 1e30f; hi[a] = -1e30f; }
    for (int i = 0; i < 8; i++) {
        float x = c->cx[k] + ((i & 1) ? c->radius[k] : -c->radius[k]);
        float y = c->cy[k] + ((i & 2) ? c->radius[k] : -c->radius[k]);
        float z = (i & 4) ? c->zfar[k] : c->znear[k];
        for (int a = 0; a < 3; a++) {
            float w = c->right[a] * x + c->up[a] * y + c->fwd[a] * z;
            lo[a] = fminf(lo[a], w); hi[a] = fmaxf(hi[a], w);
        }
    }
}

void cret_csm_bind(const CretCsm* c, const GfxProgram* p, int unit) {
    bool on = c && c->active;
    gfx_bind_tex(p, "t_csm", unit, GL_TEXTURE_2D_ARRAY, on ? c->tex : (c ? c->dummy : 0));
    gfx_set1i(p, "u_csm_count", on ? c->count : 0);
    static int vis = -1; if (vis < 0) vis = getenv("CRET_SHADOW_VIS") ? atoi(getenv("CRET_SHADOW_VIS")) : 0;
    gfx_set1i(p, "u_csm_debug", vis);
    if (!on) return;
    glUniformMatrix4fv(gfx_loc(p, "u_csm_vp"), c->count, GL_FALSE, &c->vp[0][0]);
    float tx[4] = {0}, sp[4] = {0};
    for (int k = 0; k < c->count; k++) { tx[k] = c->texel[k]; sp[k] = c->split[k + 1]; }
    glUniform4fv(gfx_loc(p, "u_csm_texel"), 1, tx);
    glUniform4fv(gfx_loc(p, "u_csm_split"), 1, sp);
    gfx_set1f(p, "u_csm_res", (float)c->res);
}

void cret_csm_begin_cascade(CretCsm* c, int k) {
    c->q_used[k] = c->debug && !c->frame_on;
    if (c->q_used[k]) glBeginQuery(GL_TIME_ELAPSED, c->q[k]);
    glBindFramebuffer(GL_FRAMEBUFFER, c->fbo[k]);
    glViewport(0, 0, c->res, c->res);
    glDepthMask(GL_TRUE); glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_CLAMP);                       // casters in front of the box still occlude
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.0f);   // slope-scaled caster bias
    glDisable(GL_BLEND); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    c->casters[k] = 0;
}

void cret_csm_end_cascade(CretCsm* c, int k) {
    if (c->q_used[k]) glEndQuery(GL_TIME_ELAPSED);
}

void cret_csm_end(CretCsm* c) {
    glDisable(GL_DEPTH_CLAMP); glDisable(GL_POLYGON_OFFSET_FILL); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void cret_csm_frame_timer(CretCsm* c, bool begin) {
    if (!c->debug) return;
    if (begin) {
        c->frame_on = (c->nframes & 1) || c->quality == 0;
        if (c->frame_on) glBeginQuery(GL_TIME_ELAPSED, c->q[CSM_MAX]);
        return;
    }
    GLuint64 ns = 0; bool any = false;
    if (c->frame_on) { glEndQuery(GL_TIME_ELAPSED); glGetQueryObjectui64v(c->q[CSM_MAX], GL_QUERY_RESULT, &ns); c->ms[CSM_MAX] += ns / 1e6; c->nms[1]++; }
    for (int k = 0; k < c->count; k++) {
        if (!c->q_used[k]) continue;
        glGetQueryObjectui64v(c->q[k], GL_QUERY_RESULT, &ns); c->ms[k] += ns / 1e6; c->q_used[k] = false; any = true;
    }
    if (any) c->nms[0]++;
    if (++c->nframes % 24 == 0) {
        double tot = 0; int nc = c->nms[0] ? c->nms[0] : 1, nf = c->nms[1] ? c->nms[1] : 1;
        fprintf(stderr, "shadows:");
        for (int k = 0; k < c->count; k++) { fprintf(stderr, " c%d %.2f ms (%d casters)", k, c->ms[k] / nc, c->casters[k]); tot += c->ms[k]; c->ms[k] = 0; }
        fprintf(stderr, " | pass %.2f ms | frame %.2f ms GPU | culling %.2f ms CPU\n", tot / nc, c->ms[CSM_MAX] / nf, c->cpu_ms / 24);
        c->ms[CSM_MAX] = 0; c->cpu_ms = 0; c->nms[0] = c->nms[1] = 0;
    }
}

void cret_csm_close(CretCsm* c) {
    if (c->tex) glDeleteTextures(1, &c->tex);
    if (c->dummy) glDeleteTextures(1, &c->dummy);
    if (c->count && c->fbo[0]) glDeleteFramebuffers(c->count, c->fbo);
    if (c->q[0]) glDeleteQueries(CSM_MAX + 1, c->q);
    memset(c, 0, sizeof *c);
}
