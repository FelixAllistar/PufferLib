// Small animals (render/critters.h). Included by view.c.
#include "critters.h"
#include "critter_mesh.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CR_SHADER_DIR "ocean/cretaceous/shaders/"
#define CR_MAX_EXTRA 256                                   // cret_critter_draw_one per frame
#define CR_MAX_INST (CRET_MAX_CRITTERS + CR_MAX_EXTRA)
#define CR_SHADOW_LOD 1                                    // casters use the middle LOD: a shadow needs no eyes

// Smoothed draw state by pool slot: a step from the sim (or a snapshot) is
// eased over about 70 ms; a jump of more than 3 m, a new critter or a change
// of kind snaps. The pose numbers ease toward what the state asks for.
typedef struct {
    float x, y, z, yaw, alt;
    float phase, gait, flee_t, crouch, wings, seed, look, rear;
    uint8_t live, kind, state;
} CrDraw;

// One instance: shaders/critter.vs i_ps, i_q, i_anim, i_ex, i_st.
#define CR_INST_VEC4 5
typedef struct { float ps[4], q[4], anim[4], ex[4], st[4]; } CrInst;
typedef struct { GLuint vao, vbo, ibo; int index_count, tris; } CrMesh;

static struct {
    bool ready, failed;
    GfxProgram prog, shadow;
    CrMesh mesh[CRET_CR_KINDS][CRM_LODS];
    GLuint inst_vbo;
    CrDraw slot[CRET_MAX_CRITTERS];
    float last_time;
    int force_lod;
    bool mark;
    // This frame: instances by kind and LOD, then the shadow casters by kind,
    // all uploaded to inst_vbo in that order.
    CrInst inst[CRET_CR_KINDS][CRM_LODS][CR_MAX_INST]; int n[CRET_CR_KINDS][CRM_LODS];
    CrInst cast[CRET_CR_KINDS][CR_MAX_INST]; int ncast[CRET_CR_KINDS];
    int first[CRET_CR_KINDS][CRM_LODS], cast_first[CRET_CR_KINDS];
    // Queued by cret_critter_draw_one for the next frame.
    CrInst pend[CR_MAX_EXTRA]; uint8_t pend_kind[CR_MAX_EXTRA]; int npend;
    CretCritterDrawStats stats;
} g_cr = {.last_time = -1, .force_lod = -1};

// Speed (m/s) at which each kind's gait is full: its flee speed; a bird's
// hop on the ground tops out far below its flight.
static const float CR_FULL_GAIT[CRET_CR_KINDS] = {4.6f, 4.2f, 5.5f, 2.0f, 3.0f};

static float cr_clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float cr_ease(float cur, float want, float rate, float dt) { return cur + (want - cur) * (1 - expf(-rate * dt)); }

void cret_critter_yaw_quat(float yaw, float out[4]) { out[0] = 0; out[1] = sinf(-yaw * 0.5f); out[2] = 0; out[3] = cosf(-yaw * 0.5f); }

void cret_critter_grip(int kind, float kg, float out[3]) {
    int k = kind < 0 || kind >= CRET_CR_KINDS ? 0 : kind;
    float L = cret_critter_length(k, kg);
    out[0] = 0; out[1] = CRM_RIG[k].centre_y * L; out[2] = 0;
}

// Gait phase rate (rad/s) at `speed`: one cycle per stride (stride in body
// lengths, CRM_RIG). A fish beats its tail slowly even when holding still.
static float cr_phase_rate(int kind, float L, float speed) {
    float rate = speed / fmaxf(CRM_RIG[kind].stride * L, 0.02f) * 6.2831853f;
    if (kind == CRET_CR_FISH) rate = fmaxf(rate, 2.5f);
    return fminf(rate, 60.0f);
}

void cret_critter_pose_advance(CretCritterPose* p, int kind, float kg, float speed, float dt) {
    if (kind < 0 || kind >= CRET_CR_KINDS) return;
    float L = cret_critter_length(kind, kg);
    p->phase = fmodf(p->phase + cr_phase_rate(kind, L, speed) * dt, 6.2831853f * 64);
    p->gait = cr_clamp01(speed / CR_FULL_GAIT[kind]);
}

void cret_critters_mark(bool on) { g_cr.mark = on; }
void cret_critters_force_lod(int lod) { g_cr.force_lod = lod < 0 || lod >= CRM_LODS ? -1 : lod; }
int cret_critters_tris(int kind, int lod) { return kind >= 0 && kind < CRET_CR_KINDS && lod >= 0 && lod < CRM_LODS ? g_cr.mesh[kind][lod].tris : 0; }
CretCritterDrawStats cret_critters_stats(void) { return g_cr.stats; }

void cret_critter_draw_one(int kind, float kg, const float pos[3], const float quat[4], const CretCritterPose* pose) {
    if (kind < 0 || kind >= CRET_CR_KINDS || g_cr.npend >= CR_MAX_EXTRA) return;
    CretCritterPose z = {0}; if (!pose) pose = &z;
    CrInst* in = &g_cr.pend[g_cr.npend]; g_cr.pend_kind[g_cr.npend++] = (uint8_t)kind;
    *in = (CrInst){{pos[0], pos[1], pos[2], cret_critter_length(kind, kg)}, {quat[0], quat[1], quat[2], quat[3]},
                   {pose->phase, pose->gait, pose->flinch, pose->crouch}, {pose->wings, pose->death, pose->seed, 0}, {pose->look, pose->rear, 0, 0}};
}

// ---------------------------------------------------------------- GL setup
static void cr_upload(CrMesh* out, const CrmMesh* m) {
    glGenVertexArrays(1, &out->vao); glBindVertexArray(out->vao);
    glGenBuffers(1, &out->vbo); glBindBuffer(GL_ARRAY_BUFFER, out->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CrmVertex) * m->nv, m->v, GL_STATIC_DRAW);
    GLsizei st = sizeof(CrmVertex);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(CrmVertex, pos));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(CrmVertex, nrm));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, st, (void*)offsetof(CrmVertex, seg));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(CrmVertex, piv));
    glGenBuffers(1, &out->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * m->ni, m->idx, GL_STATIC_DRAW);
    // Instance attributes: pointers are set per draw (each draw starts at its own offset).
    glBindBuffer(GL_ARRAY_BUFFER, g_cr.inst_vbo);
    for (int a = 0; a < CR_INST_VEC4; a++) { glEnableVertexAttribArray(4 + a); glVertexAttribDivisor(4 + a, 1); }
    glBindVertexArray(0);
    out->index_count = m->ni; out->tris = m->ni / 3;
}

static bool cr_init(void) {
    if (g_cr.ready || g_cr.failed) return g_cr.ready;
    if (!gfx_program_load(&g_cr.prog, CR_SHADER_DIR "critter.vs", CR_SHADER_DIR "critter.fs") ||
        !gfx_program_load(&g_cr.shadow, CR_SHADER_DIR "critter.vs", CR_SHADER_DIR "depth.fs")) {
        fprintf(stderr, "cretaceous: critter shaders failed, small animals are not drawn\n");
        g_cr.failed = true; return false;
    }
    glGenBuffers(1, &g_cr.inst_vbo); glBindBuffer(GL_ARRAY_BUFFER, g_cr.inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CrInst) * CR_MAX_INST * 2, NULL, GL_STREAM_DRAW);
    for (int k = 0; k < CRET_CR_KINDS; k++) for (int l = 0; l < CRM_LODS; l++) {
        CrmMesh m; crm_build(&m, k, l); cr_upload(&g_cr.mesh[k][l], &m); crm_free(&m);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    g_cr.ready = true;
    return true;
}

void cret_critters_reload_shaders(void) {
    if (!g_cr.ready) return;
    g_cr.prog.vs_mtime = 0; gfx_program_reload_if_changed(&g_cr.prog);
    g_cr.shadow.vs_mtime = 0; gfx_program_reload_if_changed(&g_cr.shadow);
}

void cret_critters_close(void) {
    if (g_cr.ready) {
        for (int k = 0; k < CRET_CR_KINDS; k++) for (int l = 0; l < CRM_LODS; l++) {
            CrMesh* m = &g_cr.mesh[k][l];
            glDeleteVertexArrays(1, &m->vao); glDeleteBuffers(1, &m->vbo); glDeleteBuffers(1, &m->ibo);
        }
        glDeleteBuffers(1, &g_cr.inst_vbo);
        gfx_program_free(&g_cr.prog); gfx_program_free(&g_cr.shadow);
    }
    int lod = g_cr.force_lod;
    memset(&g_cr, 0, sizeof g_cr); g_cr.last_time = -1; g_cr.force_lod = lod;
}

// ---------------------------------------------------------------- per frame
// Frustum side planes (left, right, bottom, top) from a column-major view-projection.
static void cr_planes(const float m[16], float pl[4][4]) {
    for (int i = 0; i < 4; i++) {
        int row = i >> 1; float s = (i & 1) ? -1.0f : 1.0f;
        for (int c = 0; c < 4; c++) pl[i][c] = m[c * 4 + 3] + s * m[c * 4 + row];
        float l = sqrtf(pl[i][0] * pl[i][0] + pl[i][1] * pl[i][1] + pl[i][2] * pl[i][2]);
        for (int c = 0; c < 4; c++) pl[i][c] /= fmaxf(l, 1e-9f);
    }
}
static bool cr_visible(float pl[4][4], const float p[3], float r) {
    for (int i = 0; i < 4; i++) if (pl[i][0] * p[0] + pl[i][1] * p[1] + pl[i][2] * p[2] + pl[i][3] < -r) return false;
    return true;
}

// Files one instance: culled by range and frustum, LOD by distance over body length.
static void cr_add(int kind, const CrInst* in, const float cam[3], float pl[4][4]) {
    float L = in->ps[3], c[3] = {in->ps[0], in->ps[1] + L * 0.2f, in->ps[2]};
    float dx = c[0] - cam[0], dy = c[1] - cam[1], dz = c[2] - cam[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d > CRET_CRITTER_DRAW_RANGE || !cr_visible(pl, c, L * 1.6f)) return;
    int lod = g_cr.force_lod >= 0 ? g_cr.force_lod : d < 30 * L ? 0 : d < 90 * L ? 1 : 2;
    if (g_cr.n[kind][lod] < CR_MAX_INST) g_cr.inst[kind][lod][g_cr.n[kind][lod]++] = *in;
    if (d < CRET_CRITTER_SHADOW_RANGE && g_cr.ncast[kind] < CR_MAX_INST) g_cr.cast[kind][g_cr.ncast[kind]++] = *in;
}

// The pose of pool slot k from the sim's critter, eased over dt. The head
// follows the sim's alert cycle (cret_critter_watch): nose down while it
// feeds, up and turning while it watches, so a stalker can read when to
// move; every other small mammal sits up on its hind legs to look round.
static void cr_track(CrDraw* d, const CretSim* sim, const CretCritter* c, int k, float dt, float ease) {
    float jump = hypotf(c->x - d->x, c->z - d->z);
    if (!d->live || d->kind != c->kind || jump > 3) {
        memset(d, 0, sizeof *d);
        d->x = c->x; d->y = c->y; d->z = c->z; d->yaw = c->yaw; d->alt = c->alt; d->live = 1; d->kind = c->kind; d->state = c->state;
        d->flee_t = 1e3f; d->wings = c->alt > 0.05f ? 1 : 0; d->crouch = c->state == CRET_CRS_HIDE ? 1 : 0;
        // The seed comes from the slot and the mass, which a client receives too.
        uint32_t h = (uint32_t)k * 2654435761u ^ (uint32_t)(c->kg * 1000.0f) * 40503u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        d->seed = (h & 0xFFFF) / 65535.0f;
    } else {
        float dy = c->yaw - d->yaw; while (dy > 3.14159f) dy -= 6.28318f; while (dy < -3.14159f) dy += 6.28318f;
        d->x += (c->x - d->x) * ease; d->y += (c->y - d->y) * ease; d->z += (c->z - d->z) * ease; d->yaw += dy * ease; d->alt += (c->alt - d->alt) * ease;
    }
    // Bolting (running off, flying off, turning to fight) restarts the flinch.
    bool bolt = c->state == CRET_CRS_FLEE || c->state == CRET_CRS_FLY || c->state == CRET_CRS_FIGHT;
    bool was = d->state == CRET_CRS_FLEE || d->state == CRET_CRS_FLY || d->state == CRET_CRS_FIGHT;
    if (bolt && !was) d->flee_t = 0;
    d->state = c->state; d->flee_t += dt;
    float L = cret_critter_length(c->kind, c->kg);
    d->phase = fmodf(d->phase + cr_phase_rate(c->kind, L, c->speed) * dt, 6.2831853f * 64);
    d->gait = cr_ease(d->gait, cr_clamp01(c->speed / CR_FULL_GAIT[c->kind]), 8, dt);
    d->crouch = cr_ease(d->crouch, c->state == CRET_CRS_HIDE ? 1 : 0, 6, dt);
    float watch = cret_critter_watch(sim, k);
    d->look = cr_ease(d->look, watch, 9, dt);
    d->rear = cr_ease(d->rear, c->kind == CRET_CR_MOUSE && (k & 1) && watch > 0.5f && c->speed < 0.05f ? 1.0f : 0.0f, 7, dt);
    float wings = c->kind == CRET_CR_BIRD ? (c->alt > 0.05f || c->state == CRET_CRS_FLY ? 1.0f : 0.0f) : c->kind == CRET_CR_BADGER && c->state == CRET_CRS_FIGHT ? 1.0f : 0.0f;
    d->wings = cr_ease(d->wings, wings, c->kind == CRET_CR_BIRD ? 10 : 6, dt);
}

void cret_critters_update(const CretSim* sim, const struct CretVeg* veg, const float cam[3], const float viewproj[16], float time) {
    if (!cr_init()) { g_cr.npend = 0; return; }
    float dt = g_cr.last_time < 0 ? 0 : fminf(fmaxf(time - g_cr.last_time, 0), 0.1f); g_cr.last_time = time;
    float ease = 1 - expf(-dt * 15);
    float pl[4][4]; cr_planes(viewproj, pl);
    memset(g_cr.n, 0, sizeof g_cr.n); memset(g_cr.ncast, 0, sizeof g_cr.ncast);
    for (int k = 0; k < CRET_MAX_CRITTERS; k++) {
        const CretCritter* c = &sim->critters[k]; CrDraw* d = &g_cr.slot[k];
        if (!c->active || c->kind >= CRET_CR_KINDS) { d->live = 0; continue; }
        cr_track(d, sim, c, k, dt, ease);
        float flinch = d->flee_t < 0.3f ? sinf(3.14159f * d->flee_t / 0.3f) : 0;
        CrInst in = {{d->x, d->y + d->alt, d->z, cret_critter_length(c->kind, c->kg)}, {0, 0, 0, 1},
                     {d->phase, d->gait, flinch, d->crouch}, {d->wings, 0, d->seed, fmodf(time + d->seed * 100, 1000)},
                     {d->look, d->rear, 0, 0}};
        if (c->kind != CRET_CR_FISH && d->alt < 0.3f) {   // ground cover round it: plants a herbivore grazes, and tall grass
            float wet, grass = veg ? cret_veg_grass_density(veg, d->x, d->z, &wet) : 0;
            in.st[2] = fminf(fmaxf(cret_forage(sim->world, d->x, d->z), 0.8f * grass), 1);
        }
        cret_critter_yaw_quat(d->yaw, in.q);
        cr_add(c->kind, &in, cam, pl);
    }
    for (int i = 0; i < g_cr.npend; i++) {
        CrInst in = g_cr.pend[i]; in.ex[3] = fmodf(time + in.ex[2] * 100, 1000);
        cr_add(g_cr.pend_kind[i], &in, cam, pl);
    }
    g_cr.npend = 0;
    // One upload for the frame: colour lists by kind and LOD, then the casters.
    glBindBuffer(GL_ARRAY_BUFFER, g_cr.inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CrInst) * CR_MAX_INST * 2, NULL, GL_STREAM_DRAW);
    int at = 0;
    for (int k = 0; k < CRET_CR_KINDS; k++) for (int l = 0; l < CRM_LODS; l++) {
        int n = g_cr.n[k][l]; if (at + n > CR_MAX_INST * 2) n = g_cr.n[k][l] = CR_MAX_INST * 2 - at;
        g_cr.first[k][l] = at;
        if (n) glBufferSubData(GL_ARRAY_BUFFER, sizeof(CrInst) * at, sizeof(CrInst) * n, g_cr.inst[k][l]);
        at += n;
    }
    for (int k = 0; k < CRET_CR_KINDS; k++) {
        int n = g_cr.ncast[k]; if (at + n > CR_MAX_INST * 2) n = g_cr.ncast[k] = CR_MAX_INST * 2 - at;
        g_cr.cast_first[k] = at;
        if (n) glBufferSubData(GL_ARRAY_BUFFER, sizeof(CrInst) * at, sizeof(CrInst) * n, g_cr.cast[k]);
        at += n;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    memset(&g_cr.stats, 0, sizeof g_cr.stats);
    for (int k = 0; k < CRET_CR_KINDS; k++) for (int l = 0; l < CRM_LODS; l++) {
        g_cr.stats.n[k][l] = g_cr.n[k][l]; g_cr.stats.tris += g_cr.n[k][l] * g_cr.mesh[k][l].tris; g_cr.stats.draws += g_cr.n[k][l] > 0;
    }
}

// One instanced draw of `n` instances from inst_vbo slot `first` with mesh m.
static void cr_draw_mesh(const GfxProgram* p, int kind, const CrMesh* m, int first, int n) {
    const CrmRig* R = &CRM_RIG[kind];
    gfx_set1i(p, "u_kind", kind);
    gfx_set4f(p, "u_rig0", R->neck_x, R->neck_y, R->tail_x, R->tail_y);
    gfx_set4f(p, "u_rig1", R->centre_y, R->hip_y, 0, 0);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_cr.inst_vbo);
    for (int a = 0; a < CR_INST_VEC4; a++) glVertexAttribPointer(4 + a, 4, GL_FLOAT, GL_FALSE, sizeof(CrInst), (void*)(sizeof(CrInst) * first + sizeof(float) * 4 * a));
    glDrawElementsInstanced(GL_TRIANGLES, m->index_count, GL_UNSIGNED_SHORT, 0, n);
    glBindVertexArray(0);
}

int cret_critters_draw(const VegFrameDesc* f) {
    if (!g_cr.ready) return 0;
    const GfxProgram* g = &g_cr.prog; glUseProgram(g->id);
    gfx_setmat4(g, "u_viewproj", f->viewproj);
    gfx_set3f(g, "u_cam", f->cam[0], f->cam[1], f->cam[2]); gfx_set3f(g, "u_sun", f->sun[0], f->sun[1], f->sun[2]);
    gfx_set1f(g, "u_far", f->far_plane); gfx_set1f(g, "u_time", f->time); gfx_set1i(g, "u_frame", f->frame);
    gfx_set1f(g, "u_cloud_cover", f->cloud_cover); gfx_set2f(g, "u_map", (float)f->map_n, f->map_cell);
    gfx_set1f(g, "u_overcast", f->overcast); gfx_set1f(g, "u_wet", f->wet);
    gfx_set2f(g, "u_depth", f->depth_ab[0], f->depth_ab[1]); gfx_set1i(g, "u_mark", g_cr.mark);
    gfx_bind_tex(g, "t_trans", 0, GL_TEXTURE_2D, f->t_trans);
    gfx_bind_tex(g, "t_skyview", 1, GL_TEXTURE_2D, f->t_skyview);
    gfx_bind_tex(g, "t_horizon0", 2, GL_TEXTURE_2D, f->t_horizon0);
    gfx_bind_tex(g, "t_horizon1", 3, GL_TEXTURE_2D, f->t_horizon1);
    gfx_bind_tex(g, "t_weather", 4, GL_TEXTURE_2D, f->t_weather);
    gfx_bind_tex(g, "t_misc", 5, GL_TEXTURE_2D, f->t_misc);
    gfx_set1i(g, "t_biome", 5);   // veg_light.glsl declares it; critters are not leaves, so its tint does not apply
    if (f->csm) cret_csm_bind(f->csm, g, 6);
    int drawn = 0;
    for (int k = 0; k < CRET_CR_KINDS; k++) for (int l = 0; l < CRM_LODS; l++) {
        if (!g_cr.n[k][l]) continue;
        cr_draw_mesh(g, k, &g_cr.mesh[k][l], g_cr.first[k][l], g_cr.n[k][l]); drawn += g_cr.n[k][l];
    }
    return drawn;
}

int cret_critters_draw_shadow(const CretCsm* c, int k) {
    if (!g_cr.ready || k != 0) return 0;
    const GfxProgram* g = &g_cr.shadow; glUseProgram(g->id);
    gfx_setmat4(g, "u_viewproj", c->vp[k]); gfx_set2f(g, "u_depth", 0, 0);
    int drawn = 0;
    for (int kind = 0; kind < CRET_CR_KINDS; kind++) {
        if (!g_cr.ncast[kind]) continue;
        cr_draw_mesh(g, kind, &g_cr.mesh[kind][CR_SHADOW_LOD], g_cr.cast_first[kind], g_cr.ncast[kind]); drawn += g_cr.ncast[kind];
    }
    g_cr.stats.casters = drawn;
    return drawn;
}
