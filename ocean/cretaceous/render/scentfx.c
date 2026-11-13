// The scent view (render/scentfx.h). Included by render/view.c.
#include "scentfx.h"

#define SC_SHADER_DIR "ocean/cretaceous/shaders/"
#define SC_MAX_PUFFS 1600
#define SC_RANGE 60.0f       // m from the animal: trails further out are not drawn
#define SC_TRACK_RANGE 70.0f // m: footprints further out are not shown in the scent view
#define SC_SENSE_RANGE 160.0f // m: animals further out are not marked even if sensed
#define SC_WASH 0.36f         // how far the view is pulled toward grey while the sense is held
#define SC_BLOOD_RANGE 90.0f // m: blood marks further out are not drawn
#define SC_SPACING 0.7f      // m between wisps along a trail
#define SC_VSTRIDE 13        // centre 3, corner 2, colour 4, (size, kind, seed, phase)

static struct {
    bool ready, failed;
    GfxProgram prog;
    GLuint vao, vbo;
    float* verts; int nverts, cap;
} g_sc;

static bool sc_init(void) {
    if (g_sc.ready || g_sc.failed) return g_sc.ready;
    if (!gfx_program_load(&g_sc.prog, SC_SHADER_DIR "scent.vs", SC_SHADER_DIR "scent.fs")) {
        fprintf(stderr, "cretaceous: scent shaders failed, the scent view is not drawn\n");
        g_sc.failed = true; return false;
    }
    glGenVertexArrays(1, &g_sc.vao); glGenBuffers(1, &g_sc.vbo);
    glBindVertexArray(g_sc.vao); glBindBuffer(GL_ARRAY_BUFFER, g_sc.vbo);
    GLsizei st = SC_VSTRIDE * sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, st, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, st, (void*)(9 * sizeof(float)));
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    g_sc.cap = SC_MAX_PUFFS * 6; g_sc.verts = malloc(sizeof(float) * SC_VSTRIDE * g_sc.cap);
    g_sc.ready = g_sc.verts != NULL; g_sc.failed = !g_sc.ready;
    return g_sc.ready;
}

void cret_scent_fx_close(void) {
    if (g_sc.ready) { glDeleteBuffers(1, &g_sc.vbo); glDeleteVertexArrays(1, &g_sc.vao); gfx_program_free(&g_sc.prog); }
    free(g_sc.verts); memset(&g_sc, 0, sizeof g_sc);
}

// One camera-facing quad (two triangles; the vertex shader turns the corners).
static void sc_puff(float x, float y, float z, float size, const float col[3], float alpha, float kind, float seed, float phase) {
    if (g_sc.nverts + 6 > g_sc.cap || alpha < 0.004f) return;
    static const float C[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (int k = 0; k < 6; k++) {
        float* v = g_sc.verts + (size_t)SC_VSTRIDE * g_sc.nverts++;
        float d[SC_VSTRIDE] = {x, y, z, C[k][0], C[k][1], col[0], col[1], col[2], alpha, size, kind, seed, phase};
        memcpy(v, d, sizeof d);
    }
}
static float sc_hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return (x & 0xffff) / 65535.0f; }

// Wisps along one trail: `n` points, newest first, each with its age (s).
static void sc_trail(const CretSim* sim, int me, int what, const float* px, const float* pz, const float* age, int n, const float col[3],
                     float size0, float amount, float time, uint32_t id) {
    float wx, wz, w; cret_wind(sim, &wx, &wz, &w);
    for (int q = 0; q + 1 < n; q++) {
        float dx = px[q + 1] - px[q], dz = pz[q + 1] - pz[q], L = sqrtf(dx * dx + dz * dz);
        if (L > 8.0f) continue;   // a gap in the record, not a path
        int m = (int)ceilf(L / SC_SPACING); if (m < 1) m = 1;
        for (int j = 0; j < m; j++) {
            float f = (j + 0.5f) / m, x = px[q] + dx * f, z = pz[q] + dz * f, a = age[q] + (age[q + 1] - age[q]) * f;
            float st = cret_scent_strength(sim, me, true, what, x, z, a, 1);
            if (st <= 0.01f) continue;
            float seed = sc_hash(id * 7919u + (uint32_t)(q * 64 + j)), drift = CRET_SCENT_DRIFT * w * a, sw = 0.25f * sinf(time * 0.6f + seed * 9.0f);
            float cx = x + wx * drift - wz * sw, cz = z + wz * drift + wx * sw;
            float lift = 0.12f + 0.35f * fminf(a / 25.0f, 1) + 0.06f * sinf(time * 1.3f + seed * 6.0f);
            float cy = cret_height(sim->world, cx, cz) + lift;
            sc_puff(cx, cy, cz, size0 * (0.7f + 0.8f * fminf(a / 25.0f, 1)), col, amount * (0.25f + 0.45f * st), 0, seed, time * 0.25f + seed * 13.0f);
        }
    }
}

void cret_scent_draw(CretView* v, const CretSim* sim, int me, float amount, const CretCamera* cam) {
    if (amount <= 0.01f || me < 0 || !sim->agents[me].alive || !sc_init()) return;
    const CretAgent* a = &sim->agents[me]; float time = v->time;
    CretScent sc; cret_scent_query(sim, me, true, &sc);
    bool nose = sc.range[CRET_SCENT_CARCASS] > 0;   // herbivores have no nose for prey: they get the animals they sense and the marks on the ground
    g_sc.nverts = 0;
    // The world calms while the sense is held: a grey-green wash under everything else (less colour, less contrast).
    sc_puff(0, 0, 0, 1, (const float[3]){0.16f, 0.19f, 0.18f}, amount * SC_WASH, 2, 1, 0);
    // Animals it senses now, through the plants: seen (in its view, by how they move and stand: eco.h) or heard
    // (by their speed and weight). A halo by what they are to it: a threat red, prey amber, its own group green,
    // the rest pale. Heard but not seen is fainter and wider (a direction more than a place).
    {
        static const float THREAT[3] = {1.0f, 0.28f, 0.2f}, FOOD[3] = {1.0f, 0.8f, 0.4f}, KIN[3] = {0.5f, 0.95f, 0.6f}, OTHER[3] = {0.8f, 0.86f, 0.9f};
        bool carn = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE; float ph = fmodf(time / 2.4f, 1.0f);
        for (int j = 0; j < sim->cfg.num_agents; j++) {
            const CretAgent* b = &sim->agents[j]; if (j == me || !b->alive) continue;
            float d = hypotf(b->x - a->x, b->z - a->z); if (d > SC_SENSE_RANGE) continue;
            bool seen = cret_eco_sees(sim, a, b, d), heard = d < cret_eco_hear_m(sim, b->speed, cret_mass(b));
            if (!seen && !heard) continue;
            bool bcarn = CRET_SPECIES_TABLE[b->species].diet == CRET_DIET_CARNIVORE;
            const float* col = a->group && b->group == a->group ? KIN : cret_eco_threat_of(sim, a, b) > 0 ? THREAT : carn && !bcarn ? FOOD : !carn && bcarn ? THREAT : OTHER;
            float L = CRET_SPECIES_TABLE[b->species].length * cret_growth_scale(b->growth), near = 1 - 0.5f * d / SC_SENSE_RANGE;
            sc_puff(b->x, b->y + b->height * 0.5f, b->z, (seen ? 0.62f : 0.95f) * L, col, amount * (seen ? 0.85f : 0.4f) * near, 3, 0, ph);
        }
    }
    if (!nose) goto marks;
    static const float PREY[3] = {1.0f, 0.84f, 0.58f}, ANIMAL[3] = {0.72f, 0.86f, 0.78f}, CARRION[3] = {0.86f, 0.9f, 0.52f};
    float px[CRET_TRAIL_POINTS + 1], pz[CRET_TRAIL_POINTS + 1], ag[CRET_TRAIL_POINTS + 1];
    // Critters: the trail behind each, from where it is now.
    for (int k = 0; k < CRET_MAX_CRITTERS && sim->ncrit; k++) {
        const CretCritter* c = &sim->critters[k]; const CretTrail* t = &sim->crit_trail[k];
        if (!c->active || c->alt > 0.5f || fabsf(c->x - a->x) + fabsf(c->z - a->z) > SC_RANGE * 1.4f) continue;
        int n = 0; px[n] = c->x; pz[n] = c->z; ag[n++] = 0;
        for (int q = 0; q < t->n; q++) {
            int p = (t->head + CRET_TRAIL_POINTS - 1 - q) % CRET_TRAIL_POINTS;
            px[n] = t->x[p]; pz[n] = t->z[p]; ag[n++] = (sim->tick - t->tick[p]) * CRET_DT;
        }
        sc_trail(sim, me, CRET_SCENT_CRITTER, px, pz, ag, n, PREY, 0.3f, amount, time, (uint32_t)k + 1000u);
    }
    // Other animals: only their trail, from CRET_SCENT_MIN_AGE back (never where they stand).
    for (int j = 0; j < sim->cfg.num_agents; j++) {
        const CretTrail* t = &sim->agent_trail[j];
        if (j == me || !sim->agents[j].alive || t->n < 2) continue;
        int n = 0;
        for (int q = 0; q < t->n; q++) {
            int p = (t->head + CRET_TRAIL_POINTS - 1 - q) % CRET_TRAIL_POINTS; float age = (sim->tick - t->tick[p]) * CRET_DT;
            if (age < CRET_SCENT_MIN_AGE) continue;
            px[n] = t->x[p]; pz[n] = t->z[p]; ag[n++] = age;
        }
        sc_trail(sim, me, CRET_SCENT_ANIMAL, px, pz, ag, n, ANIMAL, 0.55f, amount, time, (uint32_t)j);
    }
    // Carcasses: a plume rising and leaning downwind.
    for (int k = 0; k < CRET_MAX_CARCASSES; k++) {
        const CretCarcass* c = &sim->carcasses[k]; if (!c->active || c->meat <= 0 || c->carrier) continue;
        float st = cret_scent_strength(sim, me, true, CRET_SCENT_CARCASS, c->x, c->z, 0, fminf(fmaxf(0.5f + 0.25f * log2f(1 + c->meat / 10), 0.5f), 1.5f));
        if (st <= 0.01f) continue;
        float g = cret_height(sim->world, c->x, c->z);
        for (int i = 0; i < 8; i++) {
            float seed = sc_hash((uint32_t)k * 131u + i), ph = fmodf(time * 0.12f + i / 8.0f + seed * 0.1f, 1.0f);
            float x = c->x + sc.wind_x * ph * 4.0f, z = c->z + sc.wind_z * ph * 4.0f;
            sc_puff(x, g + 0.3f + ph * 2.8f, z, 0.5f + 1.1f * ph, CARRION, amount * (0.25f + 0.5f * st) * sinf(3.14159f * ph), 0, seed, time * 0.2f + seed * 7);
        }
    }
marks:;
    // Tracks: every footprint within range glows through the plants (they hide in grass otherwise), brighter the
    // fresher, so a trail reads as a line of dots that fades toward where the animal came from. Dung: a dull mark.
    static const float TRACK[3] = {1.0f, 0.82f, 0.42f}, DUNG[3] = {0.62f, 0.52f, 0.3f};
    for (int k = 0; k < CRET_SIGNS; k++) {
        float fresh = cret_sign_fresh(sim, k); if (fresh <= 0) continue;
        const CretSign* g = &sim->signs[k]; float dx = g->x - a->x, dz = g->z - a->z, d = sqrtf(dx * dx + dz * dz);
        if (g->agent == me || d > SC_TRACK_RANGE) continue;
        bool dung = g->kind == CRET_SIGN_DUNG; float seed = sc_hash((uint32_t)k * 40503u + 17u), near = 1 - 0.7f * d / SC_TRACK_RANGE;
        float size = 0.1f + 0.012f * CRET_SPECIES_TABLE[g->species < CRET_SPECIES ? g->species : 0].length * (0.4f + 0.6f * g->growth / 255.0f) * 4.0f;
        sc_puff(g->x, cret_height(sim->world, g->x, g->z) + 0.1f, g->z, dung ? size * 1.3f : size, dung ? DUNG : TRACK, amount * (0.15f + 0.6f * fresh * fresh) * near, 0, seed, seed * 13.0f);
    }
    // Blood: red marks on the ground along a wounded animal's way, fading as they dry.
    static const float BLOOD[3] = {0.95f, 0.16f, 0.12f};
    for (int k = 0; k < CRET_BLOOD_MARKS; k++) {
        float fresh = cret_blood_fresh(sim, k); if (fresh <= 0) continue;
        const CretBlood* b = &sim->blood[k]; float dx = b->x - a->x, dz = b->z - a->z, d = sqrtf(dx * dx + dz * dz);
        if (b->agent == me || d > SC_BLOOD_RANGE) continue;
        float seed = sc_hash((uint32_t)k * 2654435761u), near = 1 - 0.6f * d / SC_BLOOD_RANGE;
        sc_puff(b->x, cret_height(sim->world, b->x, b->z) + 0.12f, b->z, 0.22f + 0.1f * seed, BLOOD, amount * (0.35f + 0.55f * fresh) * near, 0, seed, seed * 13.0f);
    }
    // The pulse: the nearest prey and the nearest carcass the nose finds.
    bool prey_done = false, carc_done = false;
    for (int q = 0; q < sc.n; q++) {
        const CretScentHit* h = &sc.hit[q]; float ph = fmodf(time / 1.4f, 1.0f);
        if (h->what == CRET_SCENT_CRITTER && !prey_done) {
            const CretCritter* c = &sim->critters[h->index]; float L = cret_critter_length(c->kind, c->kg);
            sc_puff(c->x, c->y + L * 0.3f, c->z, 0.45f + 1.2f * L, PREY, amount * (0.45f + 0.4f * h->strength), 1, 0, ph);
            prey_done = true;
        } else if (h->what == CRET_SCENT_CARCASS && !carc_done) {
            const CretCarcass* c = &sim->carcasses[h->index];
            sc_puff(c->x, cret_height(sim->world, c->x, c->z) + 0.5f, c->z, 1.6f, CARRION, amount * (0.35f + 0.4f * h->strength), 1, 0, ph);
            carc_done = true;
        }
    }
    // The edges darken a little while the sense is held.
    sc_puff(0, 0, 0, 1, (const float[3]){0, 0, 0}, amount * 0.35f, 2, 0, 0);
    if (!g_sc.nverts) return;
    // Camera basis for the billboards.
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float right[3] = {-sy, 0, cy}, fwd[3] = {cy * cp, sp, sy * cp};
    float up[3] = {right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]};
    if (up[1] < 0) for (int i = 0; i < 3; i++) up[i] = -up[i];
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, GetRenderWidth(), GetRenderHeight());
    const GfxProgram* p = &g_sc.prog; glUseProgram(p->id);
    gfx_setmat4(p, "u_viewproj", v->viewproj);
    gfx_set3f(p, "u_right", right[0], right[1], right[2]); gfx_set3f(p, "u_up", up[0], up[1], up[2]);
    gfx_set2f(p, "u_screen", (float)GetRenderWidth(), (float)GetRenderHeight()); gfx_set1f(p, "u_near", CRET_NEAR);
    gfx_bind_tex(p, "t_depth", 0, GL_TEXTURE_2D, v->hdr.depth);
    glBindVertexArray(g_sc.vao); glBindBuffer(GL_ARRAY_BUFFER, g_sc.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * SC_VSTRIDE * g_sc.nverts, g_sc.verts, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, g_sc.nverts);
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (!blend) glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE);
}
