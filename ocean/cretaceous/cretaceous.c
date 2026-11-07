// Cretaceous: player, listen host, client, dedicated server and capture tool.
//   ./cretaceous [play] [--seed N] [--agents N] [--species raptor|stego|trex] [--size W H]
//   ./cretaceous host [--port P]        listen server; you play slot 0
//   ./cretaceous join ADDRESS [--port P]
//   ./cretaceous server [--port P]      headless dedicated server
//   ./cretaceous capture DIR [--shot NAME] [--size W H]   review stills
// Run from the repository root (shaders and textures load from ocean/cretaceous).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "raylib.h"
#include "sim.h"
#include "net.h"
#include "render/view.h"
#include "render/gprof.h"
#include "hud.h"

typedef enum { MODE_PLAY, MODE_HOST, MODE_JOIN, MODE_SERVER, MODE_CAPTURE } Mode;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float wrap_pi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

// ---------------------------------------------------------------- camera control
typedef struct {
    bool free_cam; float yaw, pitch, dist; float pos[3]; bool autorun;
    float boom;              // current camera distance after terrain collision (smoothed)
    float target[3]; bool target_ok;   // smoothed look-at point
} Control;

// Third-person controls: WASD relative to the camera (the body turns toward
// the travel direction), Shift sprint, Ctrl walk, Space jump, R autorun,
// E eat or drink (hold), G call, left mouse attack.
static CretInput player_input(Control* c, const CretAgent* a, const CretWorld* w) {
    CretInput in = {0};
    float f = 0, s = 0;
    if (IsKeyDown(KEY_W)) f += 1;
    if (IsKeyDown(KEY_S)) f -= 1;
    if (IsKeyDown(KEY_D)) s += 1;
    if (IsKeyDown(KEY_A)) s -= 1;
    if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_NUM_LOCK)) c->autorun = !c->autorun;
    if (IsKeyDown(KEY_S)) c->autorun = false;
    if (c->autorun && f == 0) f = 1;
    if (f != 0 || s != 0) {
        float want = c->yaw + atan2f(s, f);
        float diff = wrap_pi(want - a->yaw);
        in.turn = clampf(diff * 2.5f, -1, 1);
        float speed = IsKeyDown(KEY_LEFT_CONTROL) ? 0.5f : 1.0f;
        // Slow down while turning hard so the body swings round instead of circling.
        in.forward = speed * clampf(1.2f - fabsf(diff) / 1.6f, 0.15f, 1.0f);
        if (f < 0 && s == 0) { in.forward = -0.5f; in.turn = 0; }
    }
    in.sprint = IsKeyDown(KEY_LEFT_SHIFT);
    in.jump = IsKeyPressed(KEY_SPACE);
    if (IsKeyDown(KEY_E)) {
        float ax = a->x + cosf(a->yaw) * (a->radius + 1.5f), az = a->z + sinf(a->yaw) * (a->radius + 1.5f);
        in.action = cret_water(w, ax, az) > cret_height(w, ax, az) ? CRET_ACT_DRINK : CRET_ACT_EAT;
    }
    if (IsKeyPressed(KEY_G)) in.action = CRET_ACT_CALL;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && IsCursorHidden()) in.action = CRET_ACT_ATTACK;
    return in;
}

static void update_camera(Control* c, const CretSim* s, int me, CretCamera* cam, float dt) {
    Vector2 md = GetMouseDelta();
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsCursorHidden()) { c->yaw += md.x * 0.004f; c->pitch = clampf(c->pitch - md.y * 0.004f, -1.45f, 1.2f); }
    c->dist = clampf(c->dist * (1 - GetMouseWheelMove() * 0.1f), 2.5f, 400.0f);
    cam->yaw = c->yaw; cam->pitch = c->pitch; cam->fov = 60;
    if (c->free_cam) {
        float sp = (IsKeyDown(KEY_LEFT_SHIFT) ? 400.0f : 60.0f) * dt;
        float fx = cosf(c->pitch) * cosf(c->yaw), fy = sinf(c->pitch), fz = cosf(c->pitch) * sinf(c->yaw);
        float rx = -sinf(c->yaw), rz = cosf(c->yaw);
        if (IsKeyDown(KEY_W)) { c->pos[0] += fx * sp; c->pos[1] += fy * sp; c->pos[2] += fz * sp; }
        if (IsKeyDown(KEY_S)) { c->pos[0] -= fx * sp; c->pos[1] -= fy * sp; c->pos[2] -= fz * sp; }
        if (IsKeyDown(KEY_D)) { c->pos[0] += rx * sp; c->pos[2] += rz * sp; }
        if (IsKeyDown(KEY_A)) { c->pos[0] -= rx * sp; c->pos[2] -= rz * sp; }
        if (IsKeyDown(KEY_E)) c->pos[1] += sp;
        if (IsKeyDown(KEY_Q)) c->pos[1] -= sp;
        float g = cret_height(s->world, c->pos[0], c->pos[2]) + 1.0f;
        if (c->pos[1] < g) c->pos[1] = g;
        memcpy(cam->pos, c->pos, sizeof c->pos);
        return;
    }
    // Orbit a point above the shoulders; the boom shortens where terrain or
    // water would come between camera and animal, and eases back out.
    const CretAgent* a = &s->agents[me];
    const CretSpecies* sp = &CRET_SPECIES_TABLE[a->species];
    float want[3] = {a->x, a->y + sp->height * 0.8f, a->z};
    if (!c->target_ok) { memcpy(c->target, want, sizeof want); c->target_ok = true; c->boom = c->dist; }
    float k = 1 - expf(-dt * 14.0f), ky = 1 - expf(-dt * 6.0f);
    c->target[0] += (want[0] - c->target[0]) * k; c->target[2] += (want[2] - c->target[2]) * k; c->target[1] += (want[1] - c->target[1]) * ky;
    float tx = c->target[0], ty = c->target[1], tz = c->target[2];
    float dx = -cosf(c->pitch) * cosf(c->yaw), dy = -sinf(c->pitch), dz = -cosf(c->pitch) * sinf(c->yaw);
    float L = c->dist * fmaxf(sp->length / 3.0f, 1.0f);   // bigger animals: longer boom
    float clear = L;
    for (int i = 1; i <= 24; i++) {
        float t = L * i / 24.0f, px = tx + dx * t, py = ty + dy * t, pz = tz + dz * t;
        float g = fmaxf(cret_height(s->world, px, pz), cret_water(s->world, px, pz)) + 0.5f;
        if (py < g) { clear = fmaxf(t - L / 24.0f, 1.0f); break; }
    }
    c->boom = clear < c->boom ? clear : c->boom + (clear - c->boom) * (1 - expf(-dt * 2.0f));
    float cx = tx + dx * c->boom, cy = ty + dy * c->boom, cz = tz + dz * c->boom;
    float g = fmaxf(cret_height(s->world, cx, cz), cret_water(s->world, cx, cz)) + 0.4f; if (cy < g) cy = g;
    cam->pos[0] = cx; cam->pos[1] = cy; cam->pos[2] = cz;
    float lx = tx - cx, ly = ty - cy, lz = tz - cz;
    cam->yaw = atan2f(lz, lx); cam->pitch = atan2f(ly, sqrtf(lx * lx + lz * lz));
    c->pos[0] = cx; c->pos[1] = cy; c->pos[2] = cz;
}

// Copy of the sim for drawing, with agents placed between the previous and
// current tick (the sim runs at 30 Hz, the screen faster).
static void interpolate_agents(CretSim* out, const CretSim* cur, const CretAgent* prev, float alpha) {
    memcpy(out, cur, sizeof *cur);
    for (int i = 0; i < cur->cfg.num_agents; i++) {
        CretAgent* a = &out->agents[i]; const CretAgent* p = &prev[i];
        if (!a->alive || !p->alive || fabsf(a->x - p->x) + fabsf(a->z - p->z) > 20) continue;   // respawned: snap
        a->x = p->x + (a->x - p->x) * alpha; a->y = p->y + (a->y - p->y) * alpha; a->z = p->z + (a->z - p->z) * alpha;
        a->yaw = p->yaw + wrap_pi(a->yaw - p->yaw) * alpha;
    }
}

// ---------------------------------------------------------------- capture shots
typedef struct { const char* name; float pos[3]; float yaw, pitch, fov, tod, clouds; } Shot;

static float look_yaw(float fx, float fz, float tx, float tz) { return atan2f(tz - fz, tx - fx); }

// Picks review viewpoints from the world data: coast, river, lake, ridge, aerial.
static int pick_shots(const CretWorld* w, Shot* out) {
    const TerraMap* m = &w->map; int n = m->n; float cell = m->cell, S = w->size; int k = 0;
    int best_peak = 0, best_river = -1, best_lake = -1, best_beach = -1; float bp = -1e9f, br = 0, bb = 1e9f;
    for (int z = n / 8; z < n - n / 8; z += 2) for (int x = n / 8; x < n - n / 8; x += 2) {
        size_t i = (size_t)z * n + x; float h = m->height[i];
        if (h > bp) { bp = h; best_peak = (int)i; }
        if (m->water_kind[i] == TERRA_WATER_RIVER && h > 15 && h < 300 && m->flow[i] > br) { br = m->flow[i]; best_river = (int)i; }
        if (m->water_kind[i] == TERRA_WATER_LAKE && best_lake < 0) {
            // Lake shore: a dry neighbour 20 m away.
            float dx = 0;
            for (int q = 5; q < 40; q++) if (x + q < n && m->water_kind[i + q] == TERRA_WATER_NONE) { dx = q; break; }
            if (dx > 0) best_lake = (int)(i + (size_t)dx + 2);
        }
        if (h > 0.5f && h < 2.0f) {
            float d = fabsf(x - n * 0.35f) + fabsf(z - n * 0.6f);
            int sea_near = 0; for (int q = 1; q < 12; q++) if (x - q > 0 && m->water_kind[i - q] == TERRA_WATER_SEA) { sea_near = 1; break; }
            if (sea_near && d < bb) { bb = d; best_beach = (int)i; }
        }
    }
    float cx = S * 0.5f, cz = S * 0.5f;
    if (best_beach >= 0) {
        float x = (best_beach % n) * cell, z = (best_beach / n) * cell, h = m->height[best_beach];
        out[k++] = (Shot){"coast", {x, h + 2.5f, z}, look_yaw(x, z, x - 400, z + 300), -0.04f, 65, 0.36f, 0.45f};
    }
    if (best_river >= 0) {
        int i = best_river; float x = (i % n) * cell, z = (i / n) * cell;
        float fx = m->flow_dir[2 * i] / 127.0f, fz = m->flow_dir[2 * i + 1] / 127.0f;
        float px = x - fz * 14 - fx * 30, pz = z + fx * 14 - fz * 30; float h = cret_height(w, px, pz);
        out[k++] = (Shot){"river", {px, h + 3.5f, pz}, atan2f(fz, fx), -0.12f, 65, 0.33f, 0.4f};
    }
    if (best_lake >= 0) {
        float x = (best_lake % n) * cell, z = (best_lake / n) * cell, h = cret_height(w, x, z);
        out[k++] = (Shot){"lake", {x, h + 3.0f, z}, PI, -0.05f, 65, 0.42f, 0.5f};
    }
    {
        float x = (best_peak % n) * cell, z = (best_peak / n) * cell;
        float px = x + (cx - x) * 0.08f, pz = z + (cz - z) * 0.08f;
        out[k++] = (Shot){"ridge", {px, cret_height(w, px, pz) + 25, pz}, look_yaw(px, pz, cx, cz), -0.15f, 65, 0.38f, 0.5f};
    }
    out[k++] = (Shot){"aerial", {S * 0.05f, 1500, S * 0.05f}, look_yaw(S * 0.05f, S * 0.05f, cx, cz), -0.3f, 55, 0.4f, 0.45f};
    out[k++] = (Shot){"sunset", {S * 0.5f, 0, S * 0.5f}, 0, -0.06f, 65, 0.735f, 0.55f};
    // Sunset from a hill near the centre, facing west (sun sets toward -x).
    {
        float bx = cx, bz = cz, bh = -1e9f;
        for (int t = 0; t < 400; t++) { float x = cx + (t % 20 - 10) * 60.0f, z = cz + (t / 20 - 10) * 60.0f, h = cret_height(w, x, z); if (h > bh) { bh = h; bx = x; bz = z; } }
        out[k - 1].pos[0] = bx; out[k - 1].pos[1] = bh + 20; out[k - 1].pos[2] = bz; out[k - 1].yaw = PI;
        // Same hill a few minutes later, facing away from the sun: front-lit clouds at golden hour.
        out[k] = out[k - 1]; out[k].name = "dusk"; out[k].yaw = 0.35f; out[k].pitch = 0.02f; out[k].tod = 0.741f; k++;
    }
    return k;
}


// ---------------------------------------------------------------- dino capture scenes
// A staged scene: animals (agent slots 0..n-1), carcasses (age in seconds),
// and an overlay drawn over the frame (1: the game HUD for agent 0, 2: credits).
typedef struct {
    int n; struct { int species, act; float x, z, yaw, speed; } a[8];
    int nc; struct { int species; float x, z, yaw, age; } c[3];
    int overlay;
    int frames;            // warm-up frames before the still (0: 24)
} DinoScene;

static void place_scene(CretSim* sim, const DinoScene* sc) {
    for (int k = 0; k < sc->n && k < sim->cfg.num_agents; k++) {
        CretAgent* a = &sim->agents[k];
        a->alive = true; a->species = (uint8_t)sc->a[k].species; a->act = (uint8_t)sc->a[k].act;
        a->x = sc->a[k].x; a->z = sc->a[k].z; a->y = cret_height(sim->world, a->x, a->z); a->yaw = sc->a[k].yaw;
        a->speed = sc->a[k].speed; a->on_ground = true; a->swimming = false; a->hurt_ticks = 0; a->bleed = 0;
        a->growth = 1; a->max_health = 0; cret_apply_growth(a); a->health = a->max_health; a->food = a->water = a->stamina = 1;
    }
    memset(sim->carcasses, 0, sizeof sim->carcasses);
    for (int k = 0; k < sc->nc; k++) {
        float m = CRET_SPECIES_TABLE[sc->c[k].species].mass * 0.5f;
        sim->carcasses[k] = (CretCarcass){.active = true, .species = (uint8_t)sc->c[k].species, .x = sc->c[k].x, .z = sc->c[k].z,
                                          .y = cret_height(sim->world, sc->c[k].x, sc->c[k].z), .yaw = sc->c[k].yaw, .growth = 1,
                                          .meat = m * 0.8f, .max_meat = m, .age = (int)(sc->c[k].age * CRET_TICK_HZ), .agent = 40 + k};
    }
}
// Running animals move along their heading during the warm-up frames.
static void advance_scene(CretSim* sim, float dt) {
    for (int k = 0; k < sim->cfg.num_agents; k++) {
        CretAgent* a = &sim->agents[k]; if (!a->alive || a->speed <= 0) continue;
        a->x += cosf(a->yaw) * a->speed * dt; a->z += sinf(a->yaw) * a->speed * dt; a->y = cret_height(sim->world, a->x, a->z);
    }
}

static int dino_shots(CretSim* sim, Shot* shots, int* ns, DinoScene* sc, int* setup) {
    const CretWorld* w = sim->world; const TerraMap* m = &w->map; int n = m->n; float cell = m->cell;
    // Open, nearly flat, dry ground with low canopy, closest to the centre.
    int best = -1; float bd = 1e30f;
    for (int z = n / 4; z < 3 * n / 4; z += 3) for (int x = n / 4; x < 3 * n / 4; x += 3) {
        size_t i = (size_t)z * n + x; const uint8_t* vg = m->veg + i * TERRA_VEG_COUNT;
        float px = x * cell, pz = z * cell; float nn[3]; terra_normal(m, px, pz, nn);
        if (m->height[i] < 8 || m->water_kind[i] != TERRA_WATER_NONE || nn[1] < 0.95f) continue;

        bool open = true;
        for (int q = 0; q < 16 && open; q++) {
            float a = q * 0.3927f, r = 25; int gx = (int)((px + cosf(a) * r) / cell + 0.5f), gz = (int)((pz + sinf(a) * r) / cell + 0.5f);
            if (gx < 0 || gz < 0 || gx >= n || gz >= n || m->water_kind[(size_t)gz * n + gx] != TERRA_WATER_NONE || fabsf(m->height[(size_t)gz * n + gx] - m->height[i]) > 4) open = false;
        }
        if (!open) continue;
        // The island is mostly forest: take the thinnest canopy, near the centre.
        float d = (vg[TERRA_VEG_CONIFER] + vg[TERRA_VEG_BROADLEAF] + 0.6f * vg[TERRA_VEG_TREEFERN]) + 0.08f * (fabsf(x - n * 0.5f) + fabsf(z - n * 0.52f));
        if (d < bd) { bd = d; best = (int)i; }
    }
    if (best < 0) { fprintf(stderr, "capture: no open ground for dino scenes\n"); return 0; }
    float X = (best % n) * cell, Z = (best / n) * cell;
    float sun[3]; cret_sun_direction(0.4f, sun);
    float sa = atan2f(sun[2], sun[0]);          // azimuth toward the sun
    float side = sa + 1.9f;                       // camera roughly across the light: sun from front-side
    int k = 0;
    // 1: raptor idle close-up, three-quarter front.
    sc[k] = (DinoScene){.n = 1, .a = {{CRET_SP_RAPTOR, 0, X, Z, side + 0.75f, 0}}}; k++;
    float cx = X + cosf(side) * 5.0f, cz = Z + sinf(side) * 5.0f, cy = cret_height(w, cx, cz) + 1.5f;
    setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_raptor_idle", {cx, cy, cz}, look_yaw(cx, cz, X, Z), atan2f(cret_height(w, X, Z) + 0.8f - cy, 5.0f), 50, 0.4f, 0.4f};
    // 2: raptor sprinting across the frame, low camera.
    float ry = side + 3.14159f * 0.5f;
    sc[k] = (DinoScene){.n = 1, .a = {{CRET_SP_RAPTOR, 0, X - cosf(ry) * 0.8f * 9.0f, Z - sinf(ry) * 0.8f * 9.0f, ry, 9.0f}}}; k++;
    cx = X + cosf(side) * 6.5f; cz = Z + sinf(side) * 6.5f; cy = cret_height(w, cx, cz) + 1.2f;
    setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_raptor_run", {cx, cy, cz}, look_yaw(cx, cz, X, Z), atan2f(cret_height(w, X, Z) + 0.7f - cy, 6.5f), 55, 0.4f, 0.4f};
    // 3: stegosaurus grazing, human eye height.
    sc[k] = (DinoScene){.n = 1, .a = {{CRET_SP_STEGO, CRET_ACT_EAT, X, Z, side - 1.1f, 0}}}; k++;
    cx = X + cosf(side) * 13.0f; cz = Z + sinf(side) * 13.0f; cy = cret_height(w, cx, cz) + 1.7f;
    setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_stego_idle", {cx, cy, cz}, look_yaw(cx, cz, X, Z), atan2f(cret_height(w, X, Z) + 2.0f - cy, 13.0f), 55, 0.4f, 0.4f};
    // 4: stegosaurus walking past.
    sc[k] = (DinoScene){.n = 1, .a = {{CRET_SP_STEGO, 0, X - cosf(ry) * 2.2f * 0.8f, Z - sinf(ry) * 2.2f * 0.8f, ry, 2.2f}}}; k++;
    cx = X + cosf(side) * 15.0f; cz = Z + sinf(side) * 15.0f; cy = cret_height(w, cx, cz) + 1.7f;
    setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_stego_walk", {cx, cy, cz}, look_yaw(cx, cz, X, Z), atan2f(cret_height(w, X, Z) + 2.0f - cy, 15.0f), 55, 0.4f, 0.4f};
    // 5: a herd with raptors watching, wide, from above the ferns.
    DinoScene* h = &sc[k]; h->n = 0;
    float hx[6] = {0, 10, -8, 17, 4, -13}, hz[6] = {0, 6, 8, -5, -10, -3};
    for (int q = 0; q < 6; q++) {   // offsets in the camera frame: x across, z away
        float ax = -sinf(side) * hx[q] - cosf(side) * hz[q], az = cosf(side) * hx[q] - sinf(side) * hz[q];
        h->a[h->n++] = (typeof(h->a[0])){CRET_SP_STEGO, q % 2 ? CRET_ACT_EAT : 0, X + ax, Z + az, side + 1.6f + q * 0.5f, q == 3 ? 2.0f : 0};
    }
    h->a[h->n++] = (typeof(h->a[0])){CRET_SP_RAPTOR, 0, X + cosf(side) * 12 - sinf(side) * 8, Z + sinf(side) * 12 + cosf(side) * 8, side + 3.14159f + 0.6f, 0};
    h->a[h->n++] = (typeof(h->a[0])){CRET_SP_RAPTOR, CRET_ACT_CALL, X + cosf(side) * 14 - sinf(side) * 11, Z + sinf(side) * 14 + cosf(side) * 11, side + 3.14159f + 0.2f, 0};
    k++;
    cx = X + cosf(side) * 30.0f; cz = Z + sinf(side) * 30.0f; cy = cret_height(w, cx, cz) + 5.0f;
    setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_herd", {cx, cy, cz}, look_yaw(cx, cz, X, Z), atan2f(cret_height(w, X, Z) + 1.5f - cy, 30.0f), 60, 0.4f, 0.4f};
    // 6: the game's third-person camera (update_camera defaults) behind the
    // player raptor, trotting away from the camera, slightly off axis.
    float py = side + 3.14159f + 0.35f, boom = 5.5f, cp = -0.22f;
    float ex = X + cosf(py) * 4.6f * 0.8f, ez = Z + sinf(py) * 4.6f * 0.8f;   // where it ends after the warm-up frames
    sc[k] = (DinoScene){.n = 1, .a = {{CRET_SP_RAPTOR, 0, X, Z, py, 4.6f}}}; k++;
    float tyy = cret_height(w, ex, ez) + CRET_SPECIES_TABLE[CRET_SP_RAPTOR].height * 0.8f, ca = py - 0.25f;
    float bx = ex - cosf(cp) * cosf(ca) * boom, bz = ez - cosf(cp) * sinf(ca) * boom, by = tyy - sinf(cp) * boom;
    setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_tp", {bx, by, bz}, look_yaw(bx, bz, ex, ez), atan2f(tyy - by, boom * cosf(cp)), 60, 0.4f, 0.4f};
    // 7: tyrannosaurus walking toward a low camera, three-quarter front.
    float tside = side - 0.35f, ty = tside + 0.6f;
    sc[k] = (DinoScene){.n = 1, .a = {{CRET_SP_TREX, 0, X - cosf(ty) * 2.4f * 0.8f, Z - sinf(ty) * 2.4f * 0.8f, ty, 2.4f}}}; k++;
    cx = X + cosf(tside) * 20.0f; cz = Z + sinf(tside) * 20.0f; cy = cret_height(w, cx, cz) + 2.2f;
    setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_trex", {cx, cy, cz}, look_yaw(cx, cz, X, Z), atan2f(cret_height(w, X, Z) + 2.8f - cy, 20.0f), 55, 0.4f, 0.4f};
    // 8: a tyrannosaurus running down two fleeing stegosaurs, wide.
    float cy2 = side + 3.14159f * 0.5f;            // the chase runs across the frame
    DinoScene* c = &sc[k]; c->n = 0;
    c->a[c->n++] = (typeof(c->a[0])){CRET_SP_TREX, 0, X - cosf(cy2) * 16, Z - sinf(cy2) * 16, cy2, 7.5f};
    c->a[c->n++] = (typeof(c->a[0])){CRET_SP_STEGO, 0, X + cosf(cy2) * 2 + cosf(side) * 3, Z + sinf(cy2) * 2 + sinf(side) * 3, cy2 + 0.15f, 5.2f};
    c->a[c->n++] = (typeof(c->a[0])){CRET_SP_STEGO, 0, X + cosf(cy2) * 8 - cosf(side) * 4, Z + sinf(cy2) * 8 - sinf(side) * 4, cy2 - 0.1f, 5.2f};
    k++;
    cx = X + cosf(side) * 34.0f; cz = Z + sinf(side) * 34.0f; cy = cret_height(w, cx, cz) + 4.0f;
    setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_chase", {cx, cy, cz}, look_yaw(cx, cz, X, Z), atan2f(cret_height(w, X, Z) + 2.0f - cy, 34.0f), 55, 0.4f, 0.4f};
    // 9: combat, a raptor biting a grazing stegosaur's flank.
    {
        float sy = side + 1.5708f, ax = cosf(sy), az = sinf(sy), tx = cosf(side), tz = sinf(side);   // stego axis across the frame
        float rx = X - ax * 2.6f + tx * 2.9f, rz = Z - az * 2.6f + tz * 2.9f;   // at the hip, on the camera side
        sc[k] = (DinoScene){.n = 2, .a = {{CRET_SP_STEGO, CRET_ACT_EAT, X, Z, sy, 0}, {CRET_SP_RAPTOR, CRET_ACT_ATTACK, rx, rz, atan2f(Z - az * 2.2f - rz, X - ax * 2.2f - rx), 0}},
                            .frames = 31}; k++;   // the bite lands ~1.0 s into the clip
        float fx = X - ax * 2.4f + tx * 1.6f, fz = Z - az * 2.4f + tz * 1.6f;   // frame on the bite
        cx = fx + cosf(side - 0.75f) * 8.5f; cz = fz + sinf(side - 0.75f) * 8.5f; cy = cret_height(w, cx, cz) + 1.6f;
        setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_combat", {cx, cy, cz}, look_yaw(cx, cz, fx, fz), atan2f(cret_height(w, fx, fz) + 1.3f - cy, 8.5f), 50, 0.4f, 0.4f};
    }
    // 10: a toppled tyrannosaur carcass with two raptors feeding.
    {
        float cyaw = side + 1.2f, ccx = X + cosf(side) * 2, ccz = Z + sinf(side) * 2;
        DinoScene* f = &sc[k]; memset(f, 0, sizeof *f);
        f->nc = 1; f->c[0].species = CRET_SP_TREX; f->c[0].x = ccx; f->c[0].z = ccz; f->c[0].yaw = cyaw; f->c[0].age = 40;
        f->n = 2;
        // Raptors on the camera side, heads toward the carcass.
        // A toppled tyrannosaur lies about 4 m wide: stand clear of the body.
        float r0x = ccx + cosf(cyaw) * 0.5f + cosf(side) * 6.0f, r0z = ccz + sinf(cyaw) * 0.5f + sinf(side) * 6.0f;
        float r1x = ccx - cosf(cyaw) * 3.5f + cosf(side) * 7.5f, r1z = ccz - sinf(cyaw) * 3.5f + sinf(side) * 7.5f;
        f->a[0] = (typeof(f->a[0])){CRET_SP_RAPTOR, CRET_ACT_EAT, r0x, r0z, atan2f(ccz - r0z, ccx - r0x) + 0.5f, 0};
        f->a[1] = (typeof(f->a[0])){CRET_SP_RAPTOR, CRET_ACT_CALL, r1x, r1z, atan2f(ccz - r1z, ccx - r1x) - 0.9f, 0};
        k++;
        cx = X + cosf(side + 0.3f) * 21.0f; cz = Z + sinf(side + 0.3f) * 21.0f; cy = cret_height(w, cx, cz) + 3.6f;
        setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_feed", {cx, cy, cz}, look_yaw(cx, cz, ccx, ccz), atan2f(cret_height(w, ccx, ccz) + 0.8f - cy, 15.0f), 55, 0.4f, 0.4f};
    }
    // 11: the game HUD, third-person camera behind a young raptor near the herd.
    {
        float py2 = side + 3.14159f + 0.2f, boom2 = 5.5f, cp2 = -0.22f;
        DinoScene* hs = &sc[k]; memset(hs, 0, sizeof *hs); hs->overlay = 1; hs->n = 4;
        hs->a[0] = (typeof(hs->a[0])){CRET_SP_RAPTOR, 0, X, Z, py2, 0};
        for (int q = 1; q < 4; q++) {
            float dd = 16 + 7 * q, aa = py2 + (q - 2) * 0.35f;
            hs->a[q] = (typeof(hs->a[0])){CRET_SP_STEGO, q == 2 ? CRET_ACT_EAT : 0, X + cosf(aa) * dd, Z + sinf(aa) * dd, py2 + 1.3f * q, 0};
        }
        k++;
        float tyy = cret_height(w, X, Z) + CRET_SPECIES_TABLE[CRET_SP_RAPTOR].height * 0.8f, ca = py2 - 0.1f;
        float bx = X - cosf(cp2) * cosf(ca) * boom2, bz = Z - cosf(cp2) * sinf(ca) * boom2, by = tyy - sinf(cp2) * boom2;
        setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_hud", {bx, by, bz}, look_yaw(bx, bz, X, Z), atan2f(tyy - by, boom2 * cosf(cp2)), 60, 0.4f, 0.4f};
    }
    // 12: the credits page over the herd shot.
    {
        sc[k] = sc[4]; sc[k].overlay = 2; k++;
        Shot herd = shots[*ns - 7]; herd.name = "dino_credits"; setup[*ns] = k; shots[(*ns)++] = herd;
    }
    fprintf(stderr, "capture: dino scenes at %.0f %.0f (canopy score %.0f)\n", X, Z, bd);
    return k;
}

static int run_capture(CretSim* sim, const char* dir, const char* only, int W, int H) {
    mkdir(dir, 0755);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(W, H, "cretaceous capture"); SetTraceLogLevel(LOG_WARNING);
    CretView view; if (!cret_view_init(&view, sim->world, W, H)) return 1;
    CretHud hud; cret_hud_init(&hud);
    Shot shots[64]; int ns = pick_shots(sim->world, shots);
    // Close-ups of each tall plant kind: first instance found near the centre.
    if (view.veg_ok) {
        // kind, variant (-1: any), name; per-variant entries review the sub-species.
        static const struct { int kind, variant; const char* name; } SP[] = {
            {VEG_CONIFER, -1, "sp_conifer"}, {VEG_BROADLEAF, -1, "sp_broadleaf"}, {VEG_CYCAD, -1, "sp_cycad"},
            {VEG_TREEFERN, -1, "sp_treefern"}, {VEG_PALM, -1, "sp_palm"},
            {VEG_CONIFER, 0, "sv_araucaria"}, {VEG_CONIFER, 1, "sv_podocarp"}, {VEG_CONIFER, 2, "sv_umbrella"}, {VEG_CONIFER, 3, "sv_metasequoia"},
            {VEG_BROADLEAF, 1, "sv_laurel"}, {VEG_BROADLEAF, 2, "sv_katsura"}, {VEG_CYCAD, 3, "sv_williamsonia"}};
        const CretVeg* vg = &view.veg; int T = vg->tiles_n;
        for (int e = 0; e < (int)(sizeof SP / sizeof SP[0]) && ns < 38; e++) {
            int k = SP[e].kind;
            bool done = false;
            for (int ring = 0; ring < T / 2 && !done; ring++)
                for (int tz = T / 2 - ring; tz <= T / 2 + ring && !done; tz++) for (int tx = T / 2 - ring; tx <= T / 2 + ring && !done; tx++) {
                    if (tx < 0 || tz < 0 || tx >= T || tz >= T) continue;
                    const VegTile* tl = &vg->tiles[tz * T + tx];
                    for (int i = 0; i < tl->ntrees && !done; i++) {
                        const VegInstance* in = &tl->trees[i]; if (in->kind != k || (SP[e].variant >= 0 && in->variant != SP[e].variant)) continue;
                        float h = vg->mesh[k][in->variant].height * in->scale, d = fmaxf(h * 1.3f, k == VEG_CYCAD ? 4.5f : 6.0f);
                        // Camera on the downhill side so slopes do not hide the subject.
                        float cxp = in->x - d, czp = in->z - d * 0.4f, lo = 1e30f;
                        for (int q = 0; q < 8; q++) {
                            float ax = in->x + cosf(q * 0.785f) * d, az = in->z + sinf(q * 0.785f) * d, gh = cret_height(sim->world, ax, az);
                            if (gh < lo && cret_water(sim->world, ax, az) < gh) { lo = gh; cxp = ax; czp = az; }
                        }
                        float cy = cret_height(sim->world, cxp, czp) + 1.8f;
                        // Skip if another tall plant stands between camera and subject.
                        bool blocked = false;
                        for (int j = 0; j < tl->ntrees && !blocked; j++) {
                            const VegInstance* o2 = &tl->trees[j]; if (o2 == in || o2->kind == VEG_CYCAD) continue;
                            float sx = in->x - cxp, sz = in->z - czp, L2 = sx * sx + sz * sz;
                            float t = fmaxf(0, fminf(1.15f, ((o2->x - cxp) * sx + (o2->z - czp) * sz) / L2));
                            float ex = cxp + sx * t - o2->x, ez = czp + sz * t - o2->z;
                            if (ex * ex + ez * ez < 3.5f * 3.5f) blocked = true;
                        }
                        if (blocked) continue;
                        float tyy = in->y + h * 0.45f;
                        shots[ns++] = (Shot){SP[e].name, {cxp, fmaxf(cy, in->y + 1.5f), czp}, look_yaw(cxp, czp, in->x, in->z),
                                             atan2f(tyy - fmaxf(cy, in->y + 1.5f), sqrtf((in->x - cxp) * (in->x - cxp) + (in->z - czp) * (in->z - czp))), 60, 0.4f, 0.4f};
                        // Look up into the first broadleaf crown from beside its trunk.
                        if (k == VEG_BROADLEAF && SP[e].variant < 0) {
                            float ux = in->x + 1.6f, uz = in->z + 0.8f;
                            shots[ns++] = (Shot){"sp_under", {ux, cret_height(sim->world, ux, uz) + 1.6f, uz}, look_yaw(ux, uz, in->x - 3, in->z - 1.5f), 0.75f, 70, 0.4f, 0.4f};
                        }
                        done = true;
                    }
                }
        }
    }
    // Eye-level shot of open fern ground on a gentle slope near the centre.
    {
        const TerraMap* m = &sim->world->map; int n = m->n; float cell = m->cell; int best = -1; float bd = 1e30f;
        for (int z = n / 4; z < 3 * n / 4; z += 3) for (int x = n / 4; x < 3 * n / 4; x += 3) {
            size_t i = (size_t)z * n + x; const uint8_t* vg = m->veg + i * TERRA_VEG_COUNT;
            if (vg[TERRA_VEG_FERN] < 180 || vg[TERRA_VEG_CONIFER] + vg[TERRA_VEG_BROADLEAF] > 200 || m->height[i] < 20) continue;
            float d = fabsf(x - n * 0.5f) + fabsf(z - n * 0.55f); if (d < bd) { bd = d; best = (int)i; }
        }
        if (best >= 0) {
            float x = (best % n) * cell, z = (best / n) * cell;
            shots[ns++] = (Shot){"ground", {x, cret_height(sim->world, x, z) + 1.7f, z}, 0.6f, -0.12f, 65, 0.4f, 0.4f};
        }
    }
    // Close-ups of ground cover (scattered around the camera, so aim at the
    // densest cell of each kind) and a mid-distance look over forest.
    {
        const TerraMap* m = &sim->world->map; int n = m->n; float cell = m->cell;
        static const struct { int kind; const char* name; } GC[3] = {{TERRA_VEG_FERN, "sp_fern"}, {TERRA_VEG_HORSETAIL, "sp_horsetail"}, {TERRA_VEG_SHRUB, "sp_shrub"}};
        for (int g = 0; g < 3; g++) {
            int best = -1; float bs = -1e30f;
            for (int z = n / 4; z < 3 * n / 4; z += 4) for (int x = n / 4; x < 3 * n / 4; x += 4) {
                size_t i = (size_t)z * n + x; const uint8_t* vg = m->veg + i * TERRA_VEG_COUNT;
                if (m->height[i] < 2 || m->water_kind[i] != TERRA_WATER_NONE) continue;
                float sc = vg[GC[g].kind] - 0.4f * (vg[TERRA_VEG_CONIFER] + vg[TERRA_VEG_BROADLEAF]) - 0.02f * (fabsf(x - n * 0.5f) + fabsf(z - n * 0.5f));
                if (sc > bs) { bs = sc; best = (int)i; }
            }
            if (best < 0) continue;
            float x = (best % n) * cell, z = (best / n) * cell, p[3];
            if (view.veg_ok && cret_veg_find(&view.veg, GC[g].kind, x, z, 80, p)) {
                float back = getenv("CRET_SHOT_BACK") ? (float)atof(getenv("CRET_SHOT_BACK")) : 2.6f;
                float cxp = p[0] - back, czp = p[2] - back * 0.4f, cy = fmaxf(cret_height(sim->world, cxp, czp), cret_water(sim->world, cxp, czp)) + 1.4f + back * 0.3f;
                shots[ns++] = (Shot){GC[g].name, {cxp, cy, czp}, look_yaw(cxp, czp, p[0], p[2]), atan2f(p[1] + 0.4f - cy, back * 1.08f), 60, 0.4f, 0.35f};
            } else shots[ns++] = (Shot){GC[g].name, {x, cret_height(sim->world, x, z) + 1.5f, z}, 0.9f, -0.32f, 60, 0.4f, 0.35f};
        }
        int best = -1; float bd = 1e30f;
        for (int z = n / 4; z < 3 * n / 4; z += 4) for (int x = n / 4; x < 3 * n / 4; x += 4) {
            size_t i = (size_t)z * n + x; const uint8_t* vg = m->veg + i * TERRA_VEG_COUNT;
            if (vg[TERRA_VEG_CONIFER] + vg[TERRA_VEG_BROADLEAF] < 220 || m->height[i] < 30) continue;
            float d = fabsf(x - n * 0.45f) + fabsf(z - n * 0.6f); if (d < bd) { bd = d; best = (int)i; }
        }
        if (best >= 0) {
            float x = (best % n) * cell, z = (best / n) * cell;
            shots[ns++] = (Shot){"canopy", {x, cret_height(sim->world, x, z) + 90.0f, z}, 0.3f, -0.18f, 60, 0.38f, 0.35f};
        }
    }
    // Props (props.c): nearest boulder / outcrop / log / driftwood / coastal rock to the centre.
    if (view.props_ok) {
        static const struct { int kind; const char* name; float back; } PS[6] = {
            {PROP_BOULDER, "pr_boulder", 2.3f}, {PROP_OUTCROP, "pr_outcrop", 2.3f}, {PROP_LOG, "pr_log", 2.3f},
            {PROP_DRIFTWOOD, "pr_drift", 2.3f}, {PROP_COAST, "pr_coast", 1.3f}, {PROP_CLIFF, "pr_cliff", 2.3f}};
        const CretWorld* cw = sim->world; float S = cw->size;
        for (int k = 0; k < 6 && ns < 40; k++) {
            float o[4]; if (!cret_props_find(&view.props, PS[k].kind, S * 0.5f, S * 0.55f, o)) continue;
            float d = fmaxf(o[3] * PS[k].back, 4.0f), best = -1e9f, bx = o[0], bz = o[2];
            for (int a = 0; a < 12; a++) {   // pick the side with the lowest ground (open view)
                float ang = a * PI / 6, x = o[0] + cosf(ang) * d, z = o[2] + sinf(ang) * d, g = cret_height(cw, x, z);
                float sc = -fabsf(g - o[1]) - 50.0f * (cret_water(cw, x, z) > g);
                if (view.veg_ok) {   // avoid tree trunks near the line of sight
                    const CretVeg* vg = &view.veg; int T = vg->tiles_n;
                    int tx0 = (int)floorf((fminf(x, o[0]) - 4) / VEG_TILE), tx1 = (int)floorf((fmaxf(x, o[0]) + 4) / VEG_TILE);
                    int tz0 = (int)floorf((fminf(z, o[2]) - 4) / VEG_TILE), tz1 = (int)floorf((fmaxf(z, o[2]) + 4) / VEG_TILE);
                    for (int tz = tz0; tz <= tz1; tz++) for (int tx = tx0; tx <= tx1; tx++) {
                        if (tx < 0 || tz < 0 || tx >= T || tz >= T) continue;
                        const VegTile* tl = &vg->tiles[tz * T + tx];
                        for (int i = 0; i < tl->ntrees; i++) {
                            float ex = x - o[0], ez = z - o[2], L2 = ex * ex + ez * ez;
                            float t = fmaxf(0, fminf(1, ((tl->trees[i].x - o[0]) * ex + (tl->trees[i].z - o[2]) * ez) / L2));
                            float qx = o[0] + ex * t - tl->trees[i].x, qz = o[2] + ez * t - tl->trees[i].z;
                            if (qx * qx + qz * qz < 9.0f) sc -= 30.0f;
                        }
                    }
                }
                if (sc > best) { best = sc; bx = x; bz = z; }
            }
            float cy = fmaxf(fmaxf(cret_height(cw, bx, bz), cret_water(cw, bx, bz)) + 1.8f, o[1] + fminf(o[3] * 0.8f, 4.0f));
            float dist = sqrtf((o[0] - bx) * (o[0] - bx) + (o[2] - bz) * (o[2] - bz));
            fprintf(stderr, "capture: %s at %.0f %.0f %.0f r %.1f\n", PS[k].name, o[0], o[1], o[2], o[3]);
            shots[ns++] = (Shot){PS[k].name, {bx, cy, bz}, look_yaw(bx, bz, o[0], o[2]), atan2f(o[1] + o[3] * 0.3f - cy, dist), 60, 0.36f, 0.4f};
        }
    }
    // Dinosaurs (dino.c) on open, gently sloping ground near the centre:
    // close-ups idle and running, a herd, and the game's third-person view.
    DinoScene scenes[14]; int nscene = 0, setup[64] = {0};
    if (ns <= 52) nscene = dino_shots(sim, shots, &ns, scenes, setup);
    else fprintf(stderr, "capture: no room for dino shots (%d shots)\n", ns);
    for (int i = 0; i < sim->cfg.num_agents; i++) sim->agents[i].alive = false;
    for (int s = 0; s < ns; s++) {
        // --shot NAME, or a prefix ending in '*' (e.g. 'dino_*').
        if (only && (only[strlen(only) - 1] == '*' ? strncmp(only, shots[s].name, strlen(only) - 1) : strcmp(only, shots[s].name))) continue;
        sim->time_of_day = shots[s].tod; view.settings.cloud_coverage = shots[s].clouds;
        for (int i = 0; i < sim->cfg.num_agents; i++) sim->agents[i].alive = false;
        if (setup[s] > 0 && setup[s] <= nscene) place_scene(sim, &scenes[setup[s] - 1]);
        else memset(sim->carcasses, 0, sizeof sim->carcasses);
        if (!strcmp(shots[s].name, "dino_hud")) {   // a young raptor partway through a hard day
            CretAgent* a = &sim->agents[0]; a->growth = 0.64f; cret_apply_growth(a);
            a->health = a->max_health * 0.72f; a->bleed = a->max_health * 0.01f; a->stamina = 0.58f; a->food = 0.46f; a->water = 0.81f;
            sim->agents[2].health = sim->agents[2].max_health * 0.55f;
        }
        view.focus[3] = 0;
        if (!strcmp(shots[s].name, "dino_tp")) {   // the game camera's see-through, aimed where the raptor ends up
            const CretAgent* a = &sim->agents[0]; float t = 24.0f / 30.0f;
            view.focus[0] = a->x + cosf(a->yaw) * a->speed * t; view.focus[2] = a->z + sinf(a->yaw) * a->speed * t;
            view.focus[1] = cret_height(sim->world, view.focus[0], view.focus[2]) + CRET_SPECIES_TABLE[a->species].height * 0.5f;
            view.focus[3] = fmaxf(1.2f, CRET_SPECIES_TABLE[a->species].height * 0.8f);
        }
        CretCamera cam = {{shots[s].pos[0], shots[s].pos[1], shots[s].pos[2]}, shots[s].yaw, shots[s].pitch, shots[s].fov};
        // CRET_CAPTURE_FRAMES=N renders N extra frames and prints the mean frame time (vsync off in hidden windows).
        int extra = getenv("CRET_CAPTURE_FRAMES") ? atoi(getenv("CRET_CAPTURE_FRAMES")) : 0; double t0 = 0;
        int warm = setup[s] > 0 && setup[s] <= nscene && scenes[setup[s] - 1].frames > 0 ? scenes[setup[s] - 1].frames : 24;
        for (int f = 0; f < warm + extra; f++) {
            if (f == warm) { glFinish(); t0 = GetTime(); gprof_reset(); }
            if (setup[s]) advance_scene(sim, 1.0f / 30);
            BeginDrawing(); cret_view_render(&view, sim, &cam, 1.0f / 30);
            int ov = setup[s] > 0 && setup[s] <= nscene ? scenes[setup[s] - 1].overlay : 0;
            if (ov == 1) cret_hud_draw(&hud, sim, 0, view.viewproj, cam.yaw);
            else if (ov == 2) cret_hud_draw_credits(&hud);
            EndDrawing();
        }
        if (extra > 0) { glFinish(); printf("frame %s: %.2f ms\n", shots[s].name, (GetTime() - t0) * 1000.0 / extra); gprof_report(shots[s].name); }
        Image im = LoadImageFromScreen(); char path[1024]; snprintf(path, sizeof path, "%s/%s.png", dir, shots[s].name);
        ExportImage(im, path); UnloadImage(im);
        printf("wrote %s  (cam %.0f %.0f %.0f)\n", path, cam.pos[0], cam.pos[1], cam.pos[2]);
    }
    cret_hud_close(&hud); cret_view_close(&view); CloseWindow(); return 0;
}

// ---------------------------------------------------------------- main
int main(int argc, char** argv) {
    // WSL: Mesa defaults to llvmpipe (CPU). /dev/dxg means the d3d12 GPU path works.
    struct stat st; if (stat("/dev/dxg", &st) == 0) setenv("GALLIUM_DRIVER", "d3d12", 0);
    Mode mode = MODE_PLAY; const char* addr = NULL; const char* dir = "."; const char* only = NULL;
    int port = CRET_DEFAULT_PORT, W = 1600, H = 900;
    CretConfig cfg = cret_default_config(); cfg.seed = (uint32_t)time(NULL);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "play")) mode = MODE_PLAY;
        else if (!strcmp(argv[i], "host")) mode = MODE_HOST;
        else if (!strcmp(argv[i], "server")) mode = MODE_SERVER;
        else if (!strcmp(argv[i], "join") && i + 1 < argc) { mode = MODE_JOIN; addr = argv[++i]; }
        else if (!strcmp(argv[i], "capture") && i + 1 < argc) { mode = MODE_CAPTURE; dir = argv[++i]; }
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) cfg.world_seed = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) cfg.world_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--agents") && i + 1 < argc) cfg.num_agents = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--species") && i + 1 < argc) {
            const char* sp = argv[++i]; cfg.player_species = -1;
            for (int k = 0; k < CRET_SPECIES; k++) if (!strcmp(sp, CRET_SPECIES_TABLE[k].asset)) cfg.player_species = k;
            if (cfg.player_species < 0) { fprintf(stderr, "unknown species %s (raptor, stego, trex)\n", sp); return 2; }
        }
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) { W = atoi(argv[++i]); H = atoi(argv[++i]); }
        else { fprintf(stderr, "usage: see the header of ocean/cretaceous/cretaceous.c\n"); return 2; }
    }
    CretNet* net = NULL; int me = 0;
    if (mode == MODE_JOIN) {
        net = cret_net_join(addr, port, 5.0f); if (!net) return 1;
        cret_net_client_welcomed(net, &cfg, &me);
    }
    CretSim* sim = calloc(1, sizeof(CretSim));
    if (cret_sim_init(sim, cfg) != 0) { fprintf(stderr, "cretaceous: world failed\n"); return 1; }
    if (mode == MODE_CAPTURE) { int r = run_capture(sim, dir, only, W, H); cret_sim_close(sim); free(sim); return r; }
    if (mode == MODE_HOST || mode == MODE_SERVER) net = cret_net_host(port, mode == MODE_HOST ? 1 : 0);
    if ((mode == MODE_HOST || mode == MODE_SERVER) && !net) return 1;
    CretInput inputs[CRET_MAX_AGENTS] = {0}; bool remote[CRET_MAX_AGENTS] = {0};
    if (mode == MODE_SERVER) {           // headless: no window, audio or GPU
        double t0 = 0;
        for (;;) {
            struct timespec ts = {0, 2000000}; nanosleep(&ts, NULL);
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now); double t = now.tv_sec + now.tv_nsec * 1e-9;
            if (t0 == 0) t0 = t;
            if (t - t0 < CRET_DT) continue;
            t0 += CRET_DT;
            for (int i = 0; i < sim->cfg.num_agents; i++) inputs[i] = cret_bot_input(sim, i);
            cret_net_server_poll(net, sim, inputs, remote);
            cret_sim_step(sim, inputs);
            if (sim->tick % 2 == 0) cret_net_server_broadcast(net, sim);
        }
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(W, H, "Cretaceous");
    SetTraceLogLevel(LOG_WARNING);
    SetExitKey(KEY_NULL);   // Esc opens the menu
    CretView view; if (!cret_view_init(&view, sim->world, GetRenderWidth(), GetRenderHeight())) return 1;
    CretHud hud; cret_hud_init(&hud); hud.can_respawn = mode != MODE_JOIN;
    Control ctl = {.yaw = sim->agents[me].yaw, .pitch = -0.22f, .dist = 5.5f};
    CretCamera cam = {0};
    double acc = 0; bool show_help = true; float time_scale = 1;
    CretSim* draw = calloc(1, sizeof(CretSim)); CretAgent prev[CRET_MAX_AGENTS]; memcpy(prev, sim->agents, sizeof prev);
    DisableCursor();   // mouse look; Tab releases the cursor
    bool quit = false;
    while (!WindowShouldClose() && !quit) {
        float dt = GetFrameTime(); if (dt > 0.1f) dt = 0.1f;
        bool menu_was = hud.menu;
        if (!hud.menu && IsKeyPressed(KEY_ESCAPE)) { hud.menu = true; hud.sel = 0; hud.species = sim->agents[me].species; EnableCursor(); }
        // Dead: Enter respawns at once (the sim respawns by itself after a delay).
        if (!hud.menu && hud.can_respawn && !sim->agents[me].alive && IsKeyPressed(KEY_ENTER)) cret_sim_respawn(sim, me);
        if (IsKeyPressed(KEY_F)) { ctl.free_cam = !ctl.free_cam; if (ctl.free_cam) { ctl.pitch = cam.pitch; ctl.yaw = cam.yaw; } }
        if (IsKeyPressed(KEY_F5)) cret_view_reload_shaders(&view);
        if (IsKeyPressed(KEY_F1)) show_help = !show_help;
        if (IsKeyPressed(KEY_F2)) view.settings.wireframe = !view.settings.wireframe;
        if (IsKeyPressed(KEY_C)) view.settings.clouds = !view.settings.clouds;
        if (IsKeyPressed(KEY_TAB)) { if (IsCursorHidden()) EnableCursor(); else DisableCursor(); }
        if (IsKeyDown(KEY_T)) time_scale = 60; else time_scale = 1;
        if (IsKeyPressed(KEY_EQUAL)) view.settings.cloud_coverage = clampf(view.settings.cloud_coverage + 0.05f, 0, 1);
        if (IsKeyPressed(KEY_MINUS)) view.settings.cloud_coverage = clampf(view.settings.cloud_coverage - 0.05f, 0, 1);
        if (IsWindowResized()) cret_view_resize(&view, GetRenderWidth(), GetRenderHeight());
        CretInput mine = ctl.free_cam || hud.menu ? (CretInput){0} : player_input(&ctl, &sim->agents[me], sim->world);
        acc += dt;
        if (hud.menu && mode == MODE_PLAY) acc = 0;   // solo play pauses under the menu
        while (acc >= CRET_DT) {
            acc -= CRET_DT;
            if (mode == MODE_JOIN) { cret_net_client_send(net, &mine, sim->tick); continue; }
            for (int i = 0; i < sim->cfg.num_agents; i++) inputs[i] = cret_bot_input(sim, i);
            inputs[me] = mine;
            if (net) cret_net_server_poll(net, sim, inputs, remote);
            memcpy(prev, sim->agents, sizeof prev);
            cret_sim_step(sim, inputs);
            if (time_scale > 1) sim->time_of_day = fmodf(sim->time_of_day + CRET_DT * (time_scale - 1) / sim->cfg.day_seconds, 1.0f);
            if (net && sim->tick % 2 == 0) cret_net_server_broadcast(net, sim);
            mine.jump = 0;
            if (mine.action == CRET_ACT_CALL || mine.action == CRET_ACT_ATTACK) mine.action = 0;   // presses act once
        }
        if (mode == MODE_JOIN && !cret_net_client_poll(net, sim)) { fprintf(stderr, "cretaceous: disconnected\n"); break; }
        interpolate_agents(draw, sim, prev, mode == MODE_JOIN ? 1.0f : (float)(acc / CRET_DT));
        update_camera(&ctl, draw, me, &cam, dt);
        // Plants between the camera and the player dither out.
        if (ctl.free_cam || !draw->agents[me].alive) view.focus[3] = 0;
        else {
            const CretAgent* a = &draw->agents[me]; float h = CRET_SPECIES_TABLE[a->species].height;
            view.focus[0] = a->x; view.focus[1] = a->y + h * 0.5f; view.focus[2] = a->z; view.focus[3] = fmaxf(1.2f, h * 0.8f);
        }
        BeginDrawing();
        cret_view_render(&view, draw, &cam, dt);
        // Overlay from the interpolated copy (positions) with the live stats.
        cret_hud_draw(&hud, draw, me, view.viewproj, cam.yaw);
        if (hud.menu) {
            CretHudAction act = cret_hud_menu(&hud, sim, me);
            if (!menu_was) act = HUD_NONE;   // the Esc press that opened it
            if (act == HUD_RESUME) { hud.menu = false; DisableCursor(); }
            else if (act == HUD_RESPAWN) { cret_sim_set_species(sim, me, hud.species); ctl.target_ok = false; hud.menu = false; DisableCursor(); }
            else if (act == HUD_QUIT) quit = true;
        }
        if (show_help && !hud.menu) {
            const CretAgent* a = &sim->agents[me];
            DrawText(TextFormat("%d fps   time %05.2f   pos %.0f %.0f  alt %.0f m  %.1f m/s  %s%s", GetFPS(), sim->time_of_day * 24, a->x, a->z, a->y, a->speed,
                                a->swimming ? "swimming " : "", ctl.autorun ? "autorun" : ""), 12, 10, 14, (Color){230, 230, 230, 170});
            const char* help = "WASD move  Shift sprint  Ctrl walk  Space jump  R autorun  E eat/drink  G call  LMB attack  Esc menu  Tab cursor  F free cam  T time x60  F1 hide";
            DrawText(help, GetScreenWidth() - MeasureText(help, 14) - 16, GetScreenHeight() - 24, 14, (Color){230, 230, 230, 160});
        }
        EndDrawing();
    }
    cret_hud_close(&hud); cret_view_close(&view); CloseWindow();
    cret_net_close(net); cret_sim_close(sim); free(sim); free(draw);
    return 0;
}
