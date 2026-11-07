// Cretaceous: player, listen host, client, dedicated server and capture tool.
//   ./cretaceous [play] [--seed N] [--agents N] [--size W H]
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

typedef enum { MODE_PLAY, MODE_HOST, MODE_JOIN, MODE_SERVER, MODE_CAPTURE } Mode;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float wrap_pi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

// ---------------------------------------------------------------- camera control
typedef struct {
    bool free_cam; float yaw, pitch, dist; float pos[3]; bool autorun;
} Control;

static CretInput player_input(Control* c, const CretAgent* a) {
    CretInput in = {0};
    float f = 0, s = 0;
    if (IsKeyDown(KEY_W)) f += 1;
    if (IsKeyDown(KEY_S)) f -= 1;
    if (IsKeyDown(KEY_D)) s += 1;
    if (IsKeyDown(KEY_A)) s -= 1;
    if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_NUM_LOCK)) c->autorun = !c->autorun;
    if (f != 0 || s < 0 || s > 0) { if (IsKeyDown(KEY_S)) c->autorun = false; }
    if (c->autorun && f == 0) f = 1;
    if (f != 0 || s != 0) {
        // Move relative to the camera; the body turns toward the travel direction.
        float want = c->yaw + atan2f(s, f);
        float diff = wrap_pi(want - a->yaw);
        in.turn = clampf(diff * 2.5f, -1, 1);
        float speed = IsKeyDown(KEY_LEFT_CONTROL) ? 0.5f : 1.0f;
        in.forward = speed * clampf(1.2f - fabsf(diff) / 1.6f, 0.15f, 1.0f);
        if (f < 0 && s == 0) { in.forward = -0.5f; in.turn = 0; }
    }
    in.sprint = IsKeyDown(KEY_LEFT_SHIFT);
    in.jump = IsKeyPressed(KEY_SPACE);
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
    const CretAgent* a = &s->agents[me];
    float tx = a->x, ty = a->y + a->height * 0.85f, tz = a->z;
    float cx = tx - cosf(c->pitch) * cosf(c->yaw) * c->dist, cy = ty - sinf(c->pitch) * c->dist, cz = tz - cosf(c->pitch) * sinf(c->yaw) * c->dist;
    float g = fmaxf(cret_height(s->world, cx, cz), cret_water(s->world, cx, cz)) + 0.6f; if (cy < g) cy = g;
    cam->pos[0] = cx; cam->pos[1] = cy; cam->pos[2] = cz;
    // Look at the target from the (possibly raised) position.
    float dx = tx - cx, dy = ty - cy, dz = tz - cz;
    cam->yaw = atan2f(dz, dx); cam->pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
    c->pos[0] = cx; c->pos[1] = cy; c->pos[2] = cz;
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
    }
    return k;
}

static int run_capture(CretSim* sim, const char* dir, const char* only, int W, int H) {
    mkdir(dir, 0755);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(W, H, "cretaceous capture"); SetTraceLogLevel(LOG_WARNING);
    CretView view; if (!cret_view_init(&view, sim->world, W, H)) return 1;
    Shot shots[40]; int ns = pick_shots(sim->world, shots);
    // Close-ups of each tall plant kind: first instance found near the centre.
    if (view.veg_ok) {
        // kind, variant (-1: any), name; per-variant entries review the sub-species.
        static const struct { int kind, variant; const char* name; } SP[] = {
            {VEG_CONIFER, -1, "sp_conifer"}, {VEG_BROADLEAF, -1, "sp_broadleaf"}, {VEG_CYCAD, -1, "sp_cycad"},
            {VEG_TREEFERN, -1, "sp_treefern"}, {VEG_PALM, -1, "sp_palm"},
            {VEG_CONIFER, 1, "sv_podocarp"}, {VEG_CONIFER, 2, "sv_umbrella"}, {VEG_CONIFER, 3, "sv_metasequoia"},
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
    for (int i = 0; i < sim->cfg.num_agents; i++) sim->agents[i].alive = false;
    for (int s = 0; s < ns; s++) {
        if (only && strcmp(only, shots[s].name)) continue;
        sim->time_of_day = shots[s].tod; view.settings.cloud_coverage = shots[s].clouds;
        CretCamera cam = {{shots[s].pos[0], shots[s].pos[1], shots[s].pos[2]}, shots[s].yaw, shots[s].pitch, shots[s].fov};
        // CRET_CAPTURE_FRAMES=N renders N extra frames and prints the mean frame time (vsync off in hidden windows).
        int extra = getenv("CRET_CAPTURE_FRAMES") ? atoi(getenv("CRET_CAPTURE_FRAMES")) : 0; double t0 = 0;
        for (int f = 0; f < 24 + extra; f++) {
            if (f == 24) { glFinish(); t0 = GetTime(); gprof_reset(); }
            BeginDrawing(); cret_view_render(&view, sim, &cam, 1.0f / 30); EndDrawing();
        }
        if (extra > 0) { glFinish(); printf("frame %s: %.2f ms\n", shots[s].name, (GetTime() - t0) * 1000.0 / extra); gprof_report(shots[s].name); }
        Image im = LoadImageFromScreen(); char path[1024]; snprintf(path, sizeof path, "%s/%s.png", dir, shots[s].name);
        ExportImage(im, path); UnloadImage(im);
        printf("wrote %s  (cam %.0f %.0f %.0f)\n", path, cam.pos[0], cam.pos[1], cam.pos[2]);
    }
    cret_view_close(&view); CloseWindow(); return 0;
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
    CretView view; if (!cret_view_init(&view, sim->world, GetRenderWidth(), GetRenderHeight())) return 1;
    Control ctl = {.yaw = sim->agents[me].yaw, .pitch = -0.25f, .dist = 9};
    CretCamera cam = {0};
    double acc = 0; bool show_help = true; float time_scale = 1;
    while (!WindowShouldClose()) {
        float dt = GetFrameTime(); if (dt > 0.1f) dt = 0.1f;
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
        CretInput mine = ctl.free_cam ? (CretInput){0} : player_input(&ctl, &sim->agents[me]);
        acc += dt;
        while (acc >= CRET_DT) {
            acc -= CRET_DT;
            if (mode == MODE_JOIN) { cret_net_client_send(net, &mine, sim->tick); continue; }
            for (int i = 0; i < sim->cfg.num_agents; i++) inputs[i] = cret_bot_input(sim, i);
            inputs[me] = mine;
            if (net) cret_net_server_poll(net, sim, inputs, remote);
            cret_sim_step(sim, inputs);
            if (time_scale > 1) sim->time_of_day = fmodf(sim->time_of_day + CRET_DT * (time_scale - 1) / sim->cfg.day_seconds, 1.0f);
            if (net && sim->tick % 2 == 0) cret_net_server_broadcast(net, sim);
            mine.jump = 0;
        }
        if (mode == MODE_JOIN && !cret_net_client_poll(net, sim)) { fprintf(stderr, "cretaceous: disconnected\n"); break; }
        update_camera(&ctl, sim, me, &cam, dt);
        BeginDrawing();
        cret_view_render(&view, sim, &cam, dt);
        if (show_help) {
            const CretAgent* a = &sim->agents[me];
            DrawText(TextFormat("%d fps   time %05.2f   pos %.0f %.0f  alt %.0f m  %s%s", GetFPS(), sim->time_of_day * 24, a->x, a->z, a->y,
                                a->swimming ? "swimming " : "", ctl.autorun ? "autorun" : ""), 12, 10, 18, RAYWHITE);
            DrawText("WASD move  Shift sprint  Ctrl walk  Space jump  R autorun  RMB/Tab look  F free cam (E/Q up/down)  T time x60  C clouds  -/= cover  F2 wire  F5 shaders  F1 hide",
                     12, GetScreenHeight() - 26, 16, (Color){230, 230, 230, 200});
        }
        EndDrawing();
    }
    cret_view_close(&view); CloseWindow();
    cret_net_close(net); cret_sim_close(sim); free(sim);
    return 0;
}
