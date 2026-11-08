// Combat feedback (fx.h). Compiled by including it from cretaceous.c.
#include "fx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FX_SHADER_DIR "ocean/cretaceous/shaders/"
#define FX_VSTRIDE 10   // pos 3, uv 2, colour 4, kind 1

static inline float fx_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float fx_rand(CretFx* f) { uint32_t x = f->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; f->rng = x ? x : 0x9e3779b9u; return (f->rng >> 8) * (1.0f / 16777216.0f); }
static float fx_srand(CretFx* f) { return fx_rand(f) * 2 - 1; }
// Impulse response peaking at 1 after tau seconds, then decaying.
static float fx_pulse(float t, float tau) { if (t <= 0) return 0; float x = t / tau; return x * expf(1 - x); }

bool cret_fx_init(CretFx* f, const CretWorld* w, CretDinos* dinos) {
    memset(f, 0, sizeof *f);
    f->world = w; f->dinos = dinos; f->rng = 0x51ed27u; f->stop = 0;
    f->hud.marker_t = 10; f->hud.target = -1;
    for (int k = 0; k < 6; k++) f->hud.dir[k].t = 10;
    f->gl_ok = gfx_program_load(&f->prog, FX_SHADER_DIR "fx.vs", FX_SHADER_DIR "fx.fs");
    if (!f->gl_ok) { fprintf(stderr, "cretaceous: fx shaders failed (no blood or trails)\n"); return false; }
    glGenVertexArrays(1, &f->vao); glGenBuffers(1, &f->vbo);
    glBindVertexArray(f->vao); glBindBuffer(GL_ARRAY_BUFFER, f->vbo);
    GLsizei st = FX_VSTRIDE * sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, st, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*)(9 * sizeof(float)));
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void cret_fx_clear(CretFx* f) {
    f->primed = false; f->npart = 0; f->ndecal = 0; f->decal_next = 0; f->stop = 0; f->trauma = 0; f->kick_amp = 0; f->stats_hits = 0;
    memset(f->decal, 0, sizeof f->decal); memset(f->trail, 0, sizeof f->trail); memset(f->drip_t, 0, sizeof f->drip_t);
    memset(&f->hud, 0, sizeof f->hud); f->hud.marker_t = 10; f->hud.target = -1;
    for (int k = 0; k < 6; k++) f->hud.dir[k].t = 10;
}

void cret_fx_close(CretFx* f) {
    if (f->vbo) glDeleteBuffers(1, &f->vbo);
    if (f->vao) glDeleteVertexArrays(1, &f->vao);
    if (f->gl_ok) gfx_program_free(&f->prog);
    free(f->verts); f->verts = NULL;
}

bool cret_fx_frozen(const CretFx* f) { return f->stop > 0; }

// ---------------------------------------------------------------- spawning
static void spawn_particle(CretFx* f, const float p[3], const float v[3], float life, float size, int drip) {
    if (f->npart >= FX_PARTICLES) {   // full: replace the oldest-looking one
        int best = 0; float most = -1;
        for (int i = 0; i < 32; i++) { int k = (int)(fx_rand(f) * FX_PARTICLES) % FX_PARTICLES; float a = f->part[k].age / f->part[k].life; if (a > most) { most = a; best = k; } }
        f->part[best] = f->part[--f->npart];
    }
    FxParticle* q = &f->part[f->npart++];
    memcpy(q->p, p, sizeof q->p); memcpy(q->v, v, sizeof q->v); q->age = 0; q->life = life; q->size = size; q->drip = drip;
}
static void spawn_decal(CretFx* f, float x, float z, float r, float alpha) {
    if (!f->world) return;
    float h = cret_height(f->world, x, z);
    if (cret_water(f->world, x, z) > h + 0.05f) return;   // under water: no stain
    FxDecal* d = &f->decal[f->decal_next]; f->decal_next = (f->decal_next + 1) % FX_DECALS; if (f->ndecal < FX_DECALS) f->ndecal++;
    *d = (FxDecal){x, z, r, fx_rand(f) * 6.2832f, 0, 26.0f + 8.0f * fx_rand(f), alpha, fx_rand(f)};
}

// Where the blow lands: on the victim's body toward the weapon (the
// attacker's head, its tail tip for tail blows; the biter's head for spikes).
static void strike_point(CretFx* f, const CretSim* s, const CretHit* h, float out[3], float blow[2]) {
    const CretAgent* a = &s->agents[h->attacker]; const CretAgent* b = &s->agents[h->victim];
    float dx = b->x - a->x, dz = b->z - a->z, dl = sqrtf(dx * dx + dz * dz);
    if (dl < 1e-3f) { dx = cosf(a->yaw); dz = sinf(a->yaw); dl = 1; }
    blow[0] = dx / dl; blow[1] = dz / dl;
    float c[3] = {b->x, b->y + b->height * 0.55f, b->z}, w[3];
    int which = h->kind == CRET_HIT_TAIL ? 2 : 0;
    const int wslot = h->kind == CRET_HIT_THORNS ? h->victim : h->attacker;
    if (!(f->dinos && cret_dino_joint(f->dinos, wslot, which, w))) {
        const CretAgent* ws = &s->agents[wslot];
        float reach = ws->radius * 1.6f, sg = which == 2 ? -1.0f : 1.0f;
        w[0] = ws->x + cosf(ws->yaw) * reach * sg; w[1] = ws->y + ws->height * 0.6f; w[2] = ws->z + sinf(ws->yaw) * reach * sg;
    }
    if (h->kind == CRET_HIT_THORNS) { memcpy(out, w, sizeof(float) * 3); return; }
    float ex = w[0] - c[0], ey = w[1] - c[1], ez = w[2] - c[2], el = sqrtf(ex * ex + ey * ey + ez * ez);
    float r = fmaxf(b->radius, 0.3f) * 1.1f, k = el > 1e-3f ? fminf(el, r) / el : 0;
    out[0] = c[0] + ex * k; out[1] = c[1] + ey * k; out[2] = c[2] + ez * k;
}

static float mass_k(const CretAgent* a, const CretAgent* b) {   // ~0.55 for two raptors, ~1 for a tyrannosaur and a stegosaur
    return fx_clamp(log10f(fmaxf(cret_mass(a) + cret_mass(b), 1.0f)) / 4.0f, 0.35f, 1.2f);
}

static void on_hit(CretFx* f, const CretSim* sim, const CretSim* draw, int me, const CretHit* h, const CretFxSettings* st, CretAudio* au) {
    const CretAgent* a = &draw->agents[h->attacker]; const CretAgent* b = &draw->agents[h->victim];
    float dmg = h->dmg / 255.0f, k = fx_clamp(dmg / 0.15f, 0, 1);
    float p[3], blow[2]; strike_point(f, draw, h, p, blow);
    float cx = p[0] - f->cam_pos[0], cy = p[1] - f->cam_pos[1], cz = p[2] - f->cam_pos[2];
    bool near = cx * cx + cy * cy + cz * cz < FX_RANGE * FX_RANGE || h->attacker == me || h->victim == me;
    f->stats_hits++;
    if (near) {
        // Blood: droplets out of the wound and along the blow, then decals.
        float size = fx_clamp(b->height * 0.011f, 0.012f, 0.045f);
        int n = 6 + (int)(26 * k);
        for (int i = 0; i < n; i++) {
            float sp = (1.5f + 3.5f * fx_rand(f)) * (0.6f + 0.6f * k);
            float v[3] = {blow[0] * sp + fx_srand(f) * 1.6f, 1.2f + 2.6f * fx_rand(f), blow[1] * sp + fx_srand(f) * 1.6f};
            float q[3] = {p[0] + fx_srand(f) * size * 2, p[1] + fx_srand(f) * size * 2, p[2] + fx_srand(f) * size * 2};
            spawn_particle(f, q, v, 0.4f + 0.5f * fx_rand(f), size * (0.5f + fx_rand(f)), 0);
        }
        float rk = fx_clamp(b->height / 2.5f, 0.6f, 2.5f);
        spawn_decal(f, p[0] + blow[0] * 0.4f * rk, p[2] + blow[1] * 0.4f * rk, (0.25f + 0.8f * k) * rk, 0.9f);
        if (k > 0.6f) spawn_decal(f, p[0] + blow[0] * 1.3f * rk, p[2] + blow[1] * 1.3f * rk, (0.15f + 0.4f * k) * rk, 0.8f);
        // The victim flinches away from the blow, more from a heavier attacker.
        float ratio = sqrtf(fx_clamp(cret_mass(a) / fmaxf(cret_mass(b), 1), 0.1f, 4.0f));
        if (f->dinos) cret_dino_hit(f->dinos, h->victim, blow[0], blow[1], fx_clamp((0.35f + 0.65f * k) * ratio, 0.2f, 1.3f));
    }
    if (me < 0) return;
    CretHudFeedback* u = &f->hud; float mk = mass_k(a, b);
    if (h->attacker == me && h->kind != CRET_HIT_THORNS) {   // you landed a blow
        f->stop = fmaxf(f->stop, (0.040f + 0.040f * k) * fx_clamp(st->hitstop, 0, 1));
        f->trauma = fminf(1.0f, f->trauma + st->shake * (0.22f + 0.38f * k) * mk);
        u->marker_t = 0; u->marker_k = k; u->marker_kill = !sim->agents[h->victim].alive || sim->agents[h->victim].health <= 0.01f;
        const CretAgent* tv = &draw->agents[h->victim];
        float hp = tv->max_health > 0 ? fx_clamp(tv->health / tv->max_health, 0, 1) : 0;
        u->target_shown = fx_clamp(fmaxf(u->target == h->victim ? u->target_shown : 0, hp + dmg), 0, 1);   // the bar before this blow
        u->target = h->victim; u->target_t = 4.0f;
        cret_audio_hit(au, h->kind, k);
    }
    if (h->victim == me) {   // you were hit
        f->trauma = fminf(1.0f, f->trauma + st->shake * (0.3f + 0.5f * k) * mk);
        f->kick_t = 0; f->kick_amp = st->shake * (0.35f + 0.65f * k); f->kick_dir[0] = blow[0]; f->kick_dir[1] = blow[1];
        u->vignette = fmaxf(u->vignette, 0.45f + 0.55f * k);
        int slot = 0; float oldest = -1;   // reuse the oldest arc
        for (int i = 0; i < 6; i++) if (u->dir[i].t > oldest) { oldest = u->dir[i].t; slot = i; }
        u->dir[slot].x = a->x; u->dir[slot].z = a->z; u->dir[slot].t = 0; u->dir[slot].k = 0.4f + 0.6f * k;
    }
}

// ---------------------------------------------------------------- update
void cret_fx_update(CretFx* f, const CretSim* sim, const CretSim* draw, int me, const CretFxSettings* st, CretAudio* au, float dt) {
    if (dt < 0) dt = 0; if (dt > 0.1f) dt = 0.1f;
    // New hits (the log is a ring; a reset sim starts it again).
    if (!f->primed || sim->hit_count < f->seen) { f->seen = sim->hit_count; f->primed = true; }
    if (sim->hit_count - f->seen > CRET_HIT_LOG) f->seen = sim->hit_count - CRET_HIT_LOG;
    int na = sim->cfg.num_agents;
    for (; f->seen < sim->hit_count; f->seen++) {
        const CretHit* h = &sim->hits[f->seen % CRET_HIT_LOG];
        if (h->attacker >= na || h->victim >= na || h->attacker == h->victim) continue;
        on_hit(f, sim, draw, me, h, st, au);
    }
    // Hit-stop: the world holds still (real time still runs out the stop).
    bool frozen = f->stop > 0;
    f->stop = fmaxf(0, f->stop - dt);
    float wdt = frozen ? 0 : dt;
    f->time += dt; f->trauma = fmaxf(0, f->trauma - dt * 1.7f); f->kick_t += dt;
    // HUD timers run in real time.
    CretHudFeedback* u = &f->hud;
    u->marker_t += dt; u->vignette = fmaxf(0, u->vignette - dt * 2.2f);
    for (int i = 0; i < 6; i++) u->dir[i].t += dt;
    if (u->target >= na) u->target = -1;
    if (u->target >= 0) {   // the damage chunk holds 0.4 s, then drains to the health
        const CretAgent* t = &draw->agents[u->target];
        float hp = t->max_health > 0 ? fx_clamp(t->health / t->max_health, 0, 1) : 0;
        u->target_t -= dt;
        if (u->target_shown < hp) u->target_shown = hp;
        else if (u->target_t < 3.6f) u->target_shown = fmaxf(hp, u->target_shown - dt * 0.6f);
        if (u->target_t <= 0) u->target = -1;
    }
    if (frozen) return;
    // Particles: ballistic with a little drag; drips stain the ground where they land.
    for (int i = 0; i < f->npart;) {
        FxParticle* q = &f->part[i];
        q->age += wdt; q->v[1] -= 9.8f * wdt;
        float drag = expf(-0.6f * wdt); q->v[0] *= drag; q->v[2] *= drag;
        for (int c = 0; c < 3; c++) q->p[c] += q->v[c] * wdt;
        float g = f->world ? cret_height(f->world, q->p[0], q->p[2]) : -1e9f;
        bool landed = q->p[1] < g;
        if (landed && q->drip) spawn_decal(f, q->p[0], q->p[2], q->size * 2.4f, 0.75f);
        if (landed || q->age >= q->life) { *q = f->part[--f->npart]; continue; }
        i++;
    }
    for (int i = 0; i < FX_DECALS; i++) if (f->decal[i].life > 0) { f->decal[i].age += wdt; if (f->decal[i].age >= f->decal[i].life) f->decal[i].life = 0; }
    // Bleeding animals near the camera drip.
    for (int i = 0; i < na; i++) {
        const CretAgent* a = &draw->agents[i];
        float dx = a->x - f->cam_pos[0], dz = a->z - f->cam_pos[2];
        if (!a->alive || a->bleed <= 0.002f * a->max_health || dx * dx + dz * dz > 80.0f * 80.0f) { f->drip_t[i] = 0.2f * fx_rand(f); continue; }
        f->drip_t[i] -= wdt;
        if (f->drip_t[i] > 0) continue;
        float rate = fx_clamp(a->bleed / (0.01f * a->max_health), 0.3f, 3.0f);
        f->drip_t[i] = (0.5f + 1.0f * fx_rand(f)) / rate;
        float along = fx_srand(f) * a->radius * 1.2f, side = fx_srand(f) * a->radius * 0.3f;
        float p[3] = {a->x + cosf(a->yaw) * along - sinf(a->yaw) * side, a->y + a->height * 0.4f, a->z + sinf(a->yaw) * along + cosf(a->yaw) * side};
        float v[3] = {a->vx * 0.8f, -0.3f, a->vz * 0.8f};
        spawn_particle(f, p, v, 3.0f, fx_clamp(a->height * 0.012f, 0.02f, 0.06f), 1);
    }
    // Tail trails: the tail tip and a joint halfway down it while a swipe whips round.
    for (int i = 0; i < na && i < CRET_MAX_AGENTS; i++) {
        FxTrail* t = &f->trail[i]; const CretAgent* a = &draw->agents[i];
        for (int s = 0; s < t->n; s++) t->t[s] += wdt;
        while (t->n > 0 && t->t[t->n - 1] > 0.18f) t->n--;
        const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
        bool swipe = a->alive && ((a->act == CRET_ACT_ATTACK2 && S->alt.kind == CRET_ALT_TAIL) || (a->act == CRET_ACT_ATTACK && S->tail));
        float dx = a->x - f->cam_pos[0], dz = a->z - f->cam_pos[2];
        float tip[3], mid[3];
        if (!swipe || dx * dx + dz * dz > FX_RANGE * FX_RANGE || !f->dinos || !cret_dino_joint(f->dinos, i, 2, tip) || !cret_dino_joint(f->dinos, i, 1, mid)) continue;
        if (t->n == FX_TRAIL) t->n--;
        memmove(t->a + 1, t->a, sizeof(t->a[0]) * t->n); memmove(t->b + 1, t->b, sizeof(t->b[0]) * t->n); memmove(t->t + 1, t->t, sizeof(t->t[0]) * t->n);
        for (int k = 0; k < 3; k++) t->a[0][k] = tip[k] + (mid[k] - tip[k]) * 0.5f;   // the outer half of the tail sweeps
        memcpy(t->b[0], tip, sizeof tip); t->t[0] = 0; t->n++;
    }
}

void cret_fx_camera(CretFx* f, CretCamera* cam) {
    // Shake: trauma squared, smooth noise from incommensurate sines.
    float s = f->trauma * f->trauma, t = f->time;   // real time: the shake keeps moving through a hit-stop
    float n1 = sinf(t * 37.1f) * 0.6f + sinf(t * 23.3f + 1.7f) * 0.4f, n2 = sinf(t * 31.7f + 0.5f) * 0.6f + sinf(t * 19.1f + 2.9f) * 0.4f;
    float n3 = sinf(t * 29.3f + 4.1f), n4 = sinf(t * 41.9f + 3.3f);
    cam->yaw += s * 0.020f * n1; cam->pitch += s * 0.020f * n2;
    cam->pos[0] += s * 0.10f * n3; cam->pos[1] += s * 0.08f * n4; cam->pos[2] += s * 0.10f * n1;
    // Kick: the camera is shoved along the blow, nods up and turns away.
    if (f->kick_amp > 0) {
        float e = fx_pulse(f->kick_t, 0.07f) * f->kick_amp;
        float rx = -sinf(cam->yaw), rz = cosf(cam->yaw), side = f->kick_dir[0] * rx + f->kick_dir[1] * rz;
        cam->pos[0] += f->kick_dir[0] * 0.35f * e; cam->pos[2] += f->kick_dir[1] * 0.35f * e;
        cam->pitch += 0.03f * e; cam->yaw += side * 0.045f * e;
        if (f->kick_t > 0.6f) f->kick_amp = 0;
    }
    // Basis for billboards.
    memcpy(f->cam_pos, cam->pos, sizeof f->cam_pos);
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float fw[3] = {cp * cy, sp, cp * sy}, r[3] = {-sy, 0, cy};
    f->cam_right[0] = r[0]; f->cam_right[1] = r[1]; f->cam_right[2] = r[2];
    f->cam_up[0] = r[1] * fw[2] - r[2] * fw[1]; f->cam_up[1] = r[2] * fw[0] - r[0] * fw[2]; f->cam_up[2] = r[0] * fw[1] - r[1] * fw[0];
    if (f->cam_up[1] < 0) for (int c = 0; c < 3; c++) f->cam_up[c] = -f->cam_up[c];
}

// ---------------------------------------------------------------- drawing
static float* fx_reserve(CretFx* f, int n) {
    if (f->nverts + n > f->vcap) {
        int cap = f->vcap ? f->vcap : 4096; while (cap < f->nverts + n) cap *= 2;
        float* nv = realloc(f->verts, sizeof(float) * FX_VSTRIDE * cap); if (!nv) return NULL;
        f->verts = nv; f->vcap = cap;
    }
    float* o = f->verts + (size_t)f->nverts * FX_VSTRIDE; f->nverts += n; return o;
}
static float* fx_vert(float* o, const float p[3], float u, float v, const float c[4], float kind) {
    o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = u; o[4] = v; o[5] = c[0]; o[6] = c[1]; o[7] = c[2]; o[8] = c[3]; o[9] = kind; return o + FX_VSTRIDE;
}
// Two triangles a b c, a c d.
static void fx_quad(CretFx* f, const float a[3], const float b[3], const float c[3], const float d[3], const float uv[8], const float col[4], float kind) {
    float* o = fx_reserve(f, 6); if (!o) return;
    o = fx_vert(o, a, uv[0], uv[1], col, kind); o = fx_vert(o, b, uv[2], uv[3], col, kind); o = fx_vert(o, c, uv[4], uv[5], col, kind);
    o = fx_vert(o, a, uv[0], uv[1], col, kind); o = fx_vert(o, c, uv[4], uv[5], col, kind); fx_vert(o, d, uv[6], uv[7], col, kind);
}

static void build_geometry(CretFx* f) {
    f->nverts = 0;
    static const float UV[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    // Decals: a 3x3 grid that the vertex shader drapes over the terrain.
    for (int i = 0; i < FX_DECALS; i++) {
        const FxDecal* d = &f->decal[i]; if (d->life <= 0) continue;
        float fade = 1 - fx_clamp((d->age - d->life * 0.5f) / (d->life * 0.5f), 0, 1), dry = fx_clamp(d->age / 12.0f, 0, 1);
        float col[4] = {0.06f - 0.03f * dry, 0.004f, 0.004f, d->alpha * fade};   // fresh dark red, drying darker
        float ca = cosf(d->rot) * d->r, sa = sinf(d->rot) * d->r, kind = 1.0f + 0.49f * d->seed;
        for (int gz = 0; gz < 3; gz++) for (int gx = 0; gx < 3; gx++) {
            float p[4][3], uv[8];
            for (int c = 0; c < 4; c++) {
                float u = (gx + (c == 1 || c == 2)) / 3.0f, v = (gz + (c >= 2)) / 3.0f, x = u * 2 - 1, z = v * 2 - 1;
                p[c][0] = d->x + x * ca - z * sa; p[c][1] = 0; p[c][2] = d->z + x * sa + z * ca; uv[2 * c] = u; uv[2 * c + 1] = v;
            }
            fx_quad(f, p[0], p[1], p[2], p[3], uv, col, kind);
        }
    }
    f->ndecal_verts = f->nverts;
    // Droplets: camera-facing, a little stretched along their fall.
    for (int i = 0; i < f->npart; i++) {
        const FxParticle* q = &f->part[i];
        float a = 1 - fx_clamp((q->age - q->life * 0.6f) / (q->life * 0.4f), 0, 1), s = q->size;
        float col[4] = {0.09f, 0.005f, 0.005f, 0.95f * a};
        const float *R = f->cam_right, *U = f->cam_up; float p[4][3];
        for (int c = 0; c < 4; c++) {
            float x = (c == 1 || c == 2) ? s : -s, y = c >= 2 ? s * 1.4f : -s * 1.4f;
            for (int k = 0; k < 3; k++) p[c][k] = q->p[k] + R[k] * x + U[k] * y;
        }
        fx_quad(f, p[0], p[1], p[2], p[3], UV, col, 0.0f);
    }
    // Tail trails: ribbons between the halfway joint and the tip, fading with age.
    for (int i = 0; i < CRET_MAX_AGENTS; i++) {
        const FxTrail* t = &f->trail[i];
        for (int s = 0; s + 1 < t->n; s++) {
            float a0 = 1 - t->t[s] / 0.18f, a1 = 1 - t->t[s + 1] / 0.18f; if (a0 <= 0) continue;
            float dx = t->b[s][0] - t->b[s + 1][0], dy = t->b[s][1] - t->b[s + 1][1], dz = t->b[s][2] - t->b[s + 1][2];
            if (dx * dx + dy * dy + dz * dz < 0.02f * 0.02f) continue;   // the tip barely moved: no smear
            float col[4] = {0.70f, 0.68f, 0.62f, 0.32f * 0.5f * (a0 + fmaxf(a1, 0))};
            float uv[8] = {(float)s / FX_TRAIL, 0, (float)(s + 1) / FX_TRAIL, 0, (float)(s + 1) / FX_TRAIL, 1, (float)s / FX_TRAIL, 1};
            fx_quad(f, t->a[s], t->a[s + 1], t->b[s + 1], t->b[s], uv, col, 2.0f);
        }
    }
}

void cret_fx_draw(void* ctx, const CretView* v, const VegFrameDesc* fd) {
    CretFx* f = ctx; if (!f || !f->gl_ok) return;
    build_geometry(f);
    if (!f->nverts) return;
    gfx_program_reload_if_changed(&f->prog);
    GfxProgram* p = &f->prog; glUseProgram(p->id);
    gfx_setmat4(p, "u_viewproj", fd->viewproj);
    gfx_set2f(p, "u_depth", fd->depth_ab[0], fd->depth_ab[1]);
    gfx_set3f(p, "u_cam", fd->cam[0], fd->cam[1], fd->cam[2]);
    gfx_set3f(p, "u_sun", fd->sun[0], fd->sun[1], fd->sun[2]);
    gfx_set2f(p, "u_map", (float)v->world->map.n, v->world->map.cell);
    gfx_set1f(p, "u_overcast", fd->overcast); gfx_set1f(p, "u_wet", fd->wet);
    gfx_bind_tex(p, "t_trans", 0, GL_TEXTURE_2D, fd->t_trans);
    gfx_bind_tex(p, "t_skyview", 1, GL_TEXTURE_2D, fd->t_skyview);
    gfx_bind_tex(p, "t_height", 2, GL_TEXTURE_2D, v->t_height);
    gfx_bind_tex(p, "t_horizon0", 3, GL_TEXTURE_2D, fd->t_horizon0);
    gfx_bind_tex(p, "t_horizon1", 4, GL_TEXTURE_2D, fd->t_horizon1);
    gfx_bind_tex(p, "t_weather", 5, GL_TEXTURE_2D, fd->t_weather);
    gfx_bind_tex(p, "t_misc", 6, GL_TEXTURE_2D, fd->t_misc);
    gfx_set1f(p, "u_time", fd->time); gfx_set1f(p, "u_cloud_cover", fd->cloud_cover);
    cret_csm_bind(fd->csm, p, 7);   // sun shadows and contact occlusion, as the animals get them
    glBindVertexArray(f->vao); glBindBuffer(GL_ARRAY_BUFFER, f->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * FX_VSTRIDE * f->nverts, f->verts, GL_STREAM_DRAW);
    glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glDisable(GL_CULL_FACE);
    // Decals lie on the terrain and only test depth. Droplets and trails also
    // write it (their faint fringes are discarded), so the post pass's
    // depth-aware cloud and haze upsampling treats them as near, not as the
    // far background behind them (which showed its march noise on a trail).
    glDepthMask(GL_FALSE);
    if (f->ndecal_verts) glDrawArrays(GL_TRIANGLES, 0, f->ndecal_verts);
    glDepthMask(GL_TRUE);
    if (f->nverts > f->ndecal_verts) glDrawArrays(GL_TRIANGLES, f->ndecal_verts, f->nverts - f->ndecal_verts);
    glDisable(GL_BLEND);
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0); glUseProgram(0); glActiveTexture(GL_TEXTURE0);
}
