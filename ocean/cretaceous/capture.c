// Camera-path captures (see capture.h): scripted camera moves rendered as
// frames and piped to ffmpeg. The sim runs with bots during every path, so
// animals move; time of day and cloud cover follow the path's script.
#include "capture.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "raylib.h"
#include "render/view.h"
#include "render/gprof.h"

static float cap_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float cap_smooth(float t) { t = cap_clamp(t, 0, 1); return t * t * (3 - 2 * t); }
static float cap_wrap(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static float ground(const CretWorld* w, float x, float z) { return fmaxf(cret_height(w, x, z), cret_water(w, x, z)); }

// Landmarks shared by the paths, found once from the baked map.
typedef struct {
    float peak[3], shore[3], hill[3], forest[3], forest_dir;
} Marks;

static int walk_obstacles(const CretView* v, float x0, float z0, float a, float len);
static void find_marks(const CretWorld* w, const CretView* view, Marks* mk) {
    const TerraMap* m = &w->map; int n = m->n; float cell = m->cell, S = w->size, cx = S * 0.5f, cz = S * 0.5f;
    float bp = -1e9f; int ip = 0;
    for (int z = n / 8; z < n - n / 8; z += 2) for (int x = n / 8; x < n - n / 8; x += 2) {
        size_t i = (size_t)z * n + x; if (m->height[i] > bp) { bp = m->height[i]; ip = (int)i; }
    }
    float px = (ip % n) * cell, pz = (ip / n) * cell;
    mk->peak[0] = px; mk->peak[1] = bp; mk->peak[2] = pz;
    // Shore: march out from the peak, away from the island centre (or any
    // direction that reaches the sea first).
    float best = 1e30f; mk->shore[0] = px; mk->shore[2] = pz; mk->shore[1] = 0;
    for (int d = 0; d < 16; d++) {
        float a = atan2f(pz - cz, px - cx) + d * (2 * PI / 16), dx = cosf(a), dz = sinf(a);
        for (float r = 100; r < S; r += 20) {
            float x = px + dx * r, z = pz + dz * r;
            if (x < 0 || z < 0 || x > S || z > S) break;
            if (cret_height(w, x, z) < 0.5f) {
                float score = r * (d == 0 ? 0.7f : 1.0f);
                if (score < best) { best = score; mk->shore[0] = x; mk->shore[2] = z; }
                break;
            }
        }
    }
    // Sunset hill: highest point within 600 m of the centre (as the stills use).
    float bh = -1e9f;
    for (int t = 0; t < 400; t++) {
        float x = cx + (t % 20 - 10) * 60.0f, z = cz + (t / 20 - 10) * 60.0f, h = cret_height(w, x, z);
        if (h > bh) { bh = h; mk->hill[0] = x; mk->hill[2] = z; }
    }
    mk->hill[1] = bh;
    // Forest: dense canopy on gentle dry ground near the centre, with a
    // walking direction that stays in forest and on gentle slopes for 60 m.
    float bd = 1e30f; mk->forest[0] = cx; mk->forest[2] = cz; mk->forest_dir = 0;
    for (int z = n / 4; z < 3 * n / 4; z += 4) for (int x = n / 4; x < 3 * n / 4; x += 4) {
        size_t i = (size_t)z * n + x; const uint8_t* vg = m->veg + i * TERRA_VEG_COUNT;
        if (vg[TERRA_VEG_CONIFER] + vg[TERRA_VEG_BROADLEAF] < 200 || m->height[i] < 20 || m->water_kind[i] != TERRA_WATER_NONE) continue;
        float fx = x * cell, fz = z * cell, nn[3]; terra_normal(m, fx, fz, nn);
        if (nn[1] < 0.93f) continue;
        for (int d = 0; d < 8; d++) {
            float a = d * (2 * PI / 8), ok = 1, h0 = cret_height(w, fx, fz);
            for (float r = 10; r <= 60 && ok; r += 10) {
                float qx = fx + cosf(a) * r, qz = fz + sinf(a) * r;
                float qn[3]; terra_normal(m, qx, qz, qn);   // gentle ground along the way, not a side slope
                if (cret_water(w, qx, qz) > cret_height(w, qx, qz) || fabsf(cret_height(w, qx, qz) - h0) > r * 0.2f || qn[1] < 0.95f) ok = 0;
            }
            if (!ok) continue;
            float score = fabsf(x - n * 0.45f) + fabsf(z - n * 0.6f);
            if (score > bd) continue;
            score += 400.0f * walk_obstacles(view, fx, fz, a, 40.0f);   // a path clear of trunks and low branches
            if (score < bd) { bd = score; mk->forest[0] = fx; mk->forest[2] = fz; mk->forest_dir = a; }
        }
    }
    mk->forest[1] = cret_height(w, mk->forest[0], mk->forest[2]);
}

// Tall plants whose trunk or low crown comes within reach of a walk segment
// at eye height (crowns above ~4 m pass overhead).
static int walk_obstacles(const CretView* v, float x0, float z0, float a, float len) {
    if (!v->veg_ok) return 0;
    const CretVeg* vg = &v->veg; int T = vg->tiles_n, hits = 0;
    float dx = cosf(a), dz = sinf(a), R = len + 20;
    int tx0 = (int)floorf((fminf(x0, x0 + dx * len) - 20) / VEG_TILE), tx1 = (int)floorf((fmaxf(x0, x0 + dx * len) + 20) / VEG_TILE);
    int tz0 = (int)floorf((fminf(z0, z0 + dz * len) - 20) / VEG_TILE), tz1 = (int)floorf((fmaxf(z0, z0 + dz * len) + 20) / VEG_TILE);
    (void)R;
    for (int tz = tz0; tz <= tz1; tz++) for (int tx = tx0; tx <= tx1; tx++) {
        if (tx < 0 || tz < 0 || tx >= T || tz >= T) continue;
        const VegTile* tl = &vg->tiles[tz * T + tx];
        for (int i = 0; i < tl->ntrees; i++) {
            const VegInstance* in = &tl->trees[i];
            float px = in->x - x0, pz = in->z - z0, along = px * dx + pz * dz;
            if (along < -2 || along > len + 2) continue;
            float side = fabsf(-px * dz + pz * dx);
            const VegMesh* m = &vg->mesh[in->kind][in->variant];
            // Conifers and palms carry branches low; broadleaf crowns sit high.
            float reach = in->kind == VEG_BROADLEAF ? 1.6f : fminf(m->radius * in->scale * 0.75f, 6.0f) + 1.0f;
            if (side < reach) hits++;
        }
    }
    return hits;
}

typedef struct {
    const char* name; int frames;
} PathDef;
static const PathDef PATHS[] = {
    {"flyover", 450}, {"forest_walk", 450}, {"tp_run", 450}, {"dusk_night", 600}, {"rain_in", 750}, {"turn", 180},
};
enum { P_FLYOVER, P_FOREST, P_TPRUN, P_DUSK, P_RAIN, P_TURN, P_COUNT };

typedef struct {
    float y_smooth; bool y_ok;          // flyover / walk height smoothing
    float cam_yaw, boom; bool follow_ok; float tgt[3];   // third-person follow
    float goal[2];                      // tp_run steering goal
} PathState;

// Sets camera, time of day and cloud cover for frame f of n; returns the
// player's input for tp_run (slot 0), otherwise unused.
static CretInput path_frame(int p, const Marks* mk, CretSim* sim, CretView* view, PathState* st, int f, int n, float dt, CretCamera* cam) {
    const CretWorld* w = sim->world; float t = n > 1 ? (float)f / (n - 1) : 0; CretInput in = {0};
    // CRET_PATH_T0 / CRET_PATH_T1: render only part of a path (debugging a moment).
    static float t0 = -1, t1 = 1; if (t0 < 0) { t0 = getenv("CRET_PATH_T0") ? (float)atof(getenv("CRET_PATH_T0")) : 0; t1 = getenv("CRET_PATH_T1") ? (float)atof(getenv("CRET_PATH_T1")) : 1; }
    t = t0 + (t1 - t0) * t;
    cam->fov = 60;
    switch (p) {
    case P_FLYOVER: {
        // From over the sea, across the shore, up over the peak and on toward
        // the island's centre.
        float S = w->size, cx = S * 0.5f, cz = S * 0.5f;
        float dx = mk->peak[0] - mk->shore[0], dz = mk->peak[2] - mk->shore[2], L = sqrtf(dx * dx + dz * dz); dx /= L; dz /= L;
        float ex = mk->peak[0] + (cx - mk->peak[0]) * 0.5f, ez = mk->peak[2] + (cz - mk->peak[2]) * 0.5f;
        float x0 = mk->shore[0] - dx * 700, z0 = mk->shore[2] - dz * 700;
        float s = cap_smooth(t) * 0.7f + t * 0.3f;
        // Two legs: sea -> peak (first 65%), peak -> toward the centre.
        float x, z, hx, hz;
        if (s < 0.65f) { float u = s / 0.65f; x = x0 + (mk->peak[0] - x0) * u; z = z0 + (mk->peak[2] - z0) * u; hx = dx; hz = dz; }
        else { float u = (s - 0.65f) / 0.35f; x = mk->peak[0] + (ex - mk->peak[0]) * u; z = mk->peak[2] + (ez - mk->peak[2]) * u;
               float ux = ex - mk->peak[0], uz = ez - mk->peak[2], ul = fmaxf(sqrtf(ux * ux + uz * uz), 1); hx = ux / ul; hz = uz / ul; }
        float clear = 0;   // look ahead so the camera rises before ridges, not into them
        for (int k = 0; k <= 12; k++) for (int j = -1; j <= 1; j++)
            clear = fmaxf(clear, ground(w, x + hx * k * 50 - hz * j * 60, z + hz * k * 50 + hx * j * 60) + 90);
        float want = fmaxf(80, clear);
        if (!st->y_ok) { st->y_smooth = want; st->y_ok = true; st->cam_yaw = atan2f(hz, hx); }
        st->y_smooth += (want - st->y_smooth) * (want > st->y_smooth ? 0.08f : 0.025f);
        st->cam_yaw += cap_wrap(atan2f(hz, hx) - st->cam_yaw) * 0.03f;
        cam->pos[0] = x; cam->pos[1] = st->y_smooth; cam->pos[2] = z;
        cam->yaw = st->cam_yaw + 0.2f * sinf(t * PI * 1.5f); cam->pitch = -0.12f - 0.08f * sinf(t * PI);
        sim->time_of_day = 0.37f; view->settings.cloud_coverage = 0.45f;
    } break;
    case P_FOREST: {
        // Walk through forest at a large animal's eye height, glancing around.
        float a = mk->forest_dir, d = 2.4f * t * (n - 1) * dt;
        float x = mk->forest[0] + cosf(a) * d, z = mk->forest[2] + sinf(a) * d;
        float want = cret_height(w, x, z) + 2.2f + 0.04f * sinf(f * dt * 2 * PI * 1.6f);
        if (!st->y_ok) { st->y_smooth = want; st->y_ok = true; }
        st->y_smooth += (want - st->y_smooth) * 0.2f;
        cam->pos[0] = x; cam->pos[1] = st->y_smooth; cam->pos[2] = z;
        cam->yaw = a + 0.45f * sinf(t * 2 * PI * 0.9f); cam->pitch = -0.04f + 0.08f * sinf(t * 2 * PI * 0.6f);
        sim->time_of_day = 0.4f; view->settings.cloud_coverage = 0.4f;
    } break;
    case P_TPRUN: {
        // The player raptor runs toward the herd, then curves around it.
        const CretAgent* a = &sim->agents[0];
        if (f == 0) {
            float hx = 0, hz = 0; int k = 0;
            for (int i = 1; i < sim->cfg.num_agents; i++) if (sim->agents[i].alive && sim->agents[i].species == CRET_SP_STEGO) { hx += sim->agents[i].x; hz += sim->agents[i].z; k++; }
            if (k) { st->goal[0] = hx / k; st->goal[1] = hz / k; } else { st->goal[0] = a->x + cosf(a->yaw) * 100; st->goal[1] = a->z + sinf(a->yaw) * 100; }
        }
        float gx = st->goal[0] - a->x, gz = st->goal[1] - a->z, gd = sqrtf(gx * gx + gz * gz);
        float want = atan2f(gz, gx) + 1.45f * (1 - cap_smooth((gd - 40) / 25)) + 0.2f * sinf(t * 2 * PI * 1.3f);   // circle the herd ~40 m out
        in.turn = cap_clamp(cap_wrap(want - a->yaw) * 2.5f, -1, 1); in.forward = 1; in.sprint = t < 0.65f;
        // Camera: boom behind the animal with a lagging yaw (as the game camera).
        const CretSpecies* sp = &CRET_SPECIES_TABLE[a->species];
        float gs = cret_growth_scale(a->growth), want_t[3] = {a->x, a->y + sp->height * gs * 0.8f, a->z};
        if (!st->follow_ok) { memcpy(st->tgt, want_t, sizeof want_t); st->cam_yaw = a->yaw; st->follow_ok = true; }
        float k = 1 - expf(-dt * 14.0f), ky = 1 - expf(-dt * 6.0f);
        st->tgt[0] += (want_t[0] - st->tgt[0]) * k; st->tgt[2] += (want_t[2] - st->tgt[2]) * k; st->tgt[1] += (want_t[1] - st->tgt[1]) * ky;
        st->cam_yaw += cap_wrap(a->yaw - st->cam_yaw) * (1 - expf(-dt * 2.5f));
        float pitch = -0.16f, L = 2.2f + 1.3f * sp->length * cret_growth_scale(a->growth);   // a little closer than the game default
        float ddx = -cosf(pitch) * cosf(st->cam_yaw), ddy = -sinf(pitch), ddz = -cosf(pitch) * sinf(st->cam_yaw);
        float cx = st->tgt[0] + ddx * L, cy = st->tgt[1] + ddy * L, cz = st->tgt[2] + ddz * L;
        cy = fmaxf(cy, ground(w, cx, cz) + 0.5f);
        cam->pos[0] = cx; cam->pos[1] = cy; cam->pos[2] = cz;
        float lx = st->tgt[0] - cx, ly = st->tgt[1] - cy, lz = st->tgt[2] - cz;
        cam->yaw = atan2f(lz, lx); cam->pitch = atan2f(ly, sqrtf(lx * lx + lz * lz));
        sim->time_of_day = 0.42f; view->settings.cloud_coverage = 0.4f;
    } break;
    case P_DUSK: {
        // Sunset to moonlit night from the hill, slowly panning off the sun.
        cam->pos[0] = mk->hill[0]; cam->pos[1] = mk->hill[1] + 20; cam->pos[2] = mk->hill[2];
        cam->yaw = PI - 0.6f * cap_smooth(t); cam->pitch = -0.05f;
        sim->time_of_day = 0.715f + 0.165f * t; view->settings.cloud_coverage = 0.45f;
    } break;
    case P_RAIN: {
        // Clear midday clouding over into rain, panning across the valley.
        cam->pos[0] = mk->hill[0]; cam->pos[1] = mk->hill[1] + 20; cam->pos[2] = mk->hill[2];
        cam->yaw = PI - 0.6f + 0.35f * sinf(t * PI); cam->pitch = -0.08f;
        sim->time_of_day = 0.45f; view->settings.cloud_coverage = 0.4f + 0.6f * cap_smooth((t - 0.08f) / 0.5f);
    } break;
    case P_TURN: {
        // Fast pans on the ridge: temporal clouds and shadow cascades under motion.
        float dx = mk->hill[0] - mk->peak[0], dz = mk->hill[2] - mk->peak[2];
        float x = mk->peak[0], z = mk->peak[2];   // above the summit: a clear view all round
        cam->pos[0] = x; cam->pos[1] = mk->peak[1] + 45; cam->pos[2] = z;
        cam->yaw = atan2f(dz, dx) + 2 * PI * cap_smooth(t * 1.25f) - 1.5f * cap_smooth((t - 0.8f) / 0.2f);
        cam->pitch = -0.1f + 0.15f * sinf(t * 2 * PI);
        sim->time_of_day = 0.4f; view->settings.cloud_coverage = 0.5f;
    } break;
    }
    return in;
}

int cret_capture_paths(CretSim* sim, const char* dir, const char* names, int W, int H, int frames, int fps) {
    mkdir(dir, 0755);
    if (fps <= 0) fps = 30;
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(W, H, "cretaceous capture"); SetTraceLogLevel(LOG_WARNING);
    CretView view; if (!cret_view_init(&view, sim->world, W, H)) return 1;
    Marks mk; find_marks(sim->world, &view, &mk);
    fprintf(stderr, "paths: peak %.0f %.0f %.0f  shore %.0f %.0f  hill %.0f %.0f  forest %.0f %.0f dir %.2f\n",
            mk.peak[0], mk.peak[1], mk.peak[2], mk.shore[0], mk.shore[2], mk.hill[0], mk.hill[2], mk.forest[0], mk.forest[2], mk.forest_dir);
    int stills = getenv("CRET_PATH_STILLS") ? atoi(getenv("CRET_PATH_STILLS")) : 30;
    const char* ff = getenv("CRET_FFMPEG") ? getenv("CRET_FFMPEG") : "ffmpeg";
    float dt = 1.0f / fps; int rc = 0;
    for (int p = 0; p < P_COUNT; p++) {
        if (strcmp(names, "all")) {   // comma-separated list
            const char* s = names; size_t L = strlen(PATHS[p].name); bool hit = false;
            while (*s) { const char* e = strchr(s, ','); size_t k = e ? (size_t)(e - s) : strlen(s); if (k == L && !strncmp(s, PATHS[p].name, L)) hit = true; s += k + (e ? 1 : 0); }
            if (!hit) continue;
        }
        int n = frames > 0 ? frames : PATHS[p].frames;
        cret_sim_reset(sim);
        if (p == P_TPRUN) {   // an adult raptor, so it fills the game camera's frame
            cret_sim_set_species(sim, 0, CRET_SP_RAPTOR);
            CretAgent* a = &sim->agents[0]; a->growth = 1; cret_apply_growth(a); a->health = a->max_health;
        }
        char out[1024], sd[1024], cmd[2048];
        snprintf(out, sizeof out, "%s/%s.mp4", dir, PATHS[p].name); snprintf(sd, sizeof sd, "%s/%s", dir, PATHS[p].name); mkdir(sd, 0755);
        snprintf(cmd, sizeof cmd, "%s -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -movflags +faststart '%s'",
                 ff, W, H, fps, out);
        FILE* pipe = popen(cmd, "w");
        if (!pipe) { fprintf(stderr, "paths: cannot run %s\n", ff); rc = 1; break; }
        PathState st = {0}; CretInput inputs[CRET_MAX_AGENTS]; CretCamera cam = {0};
        double acc = 0, t0 = 0; float tod_keep = 0;
        // Warm-up at the first pose: exposure, clouds history and wetness settle.
        for (int f = -45; f < n; f++) {
            int fi = f < 0 ? 0 : f;
            if (f == 0) { t0 = GetTime(); gprof_reset(); }
            CretInput mine = path_frame(p, &mk, sim, &view, &st, fi, n, dt, &cam);
            tod_keep = sim->time_of_day;
            if (f >= 0) {
                acc += dt;
                while (acc >= CRET_DT - 1e-6) {   // 30 Hz sim steps (one per frame at 30 fps)
                    acc -= CRET_DT;
                    for (int i = 0; i < sim->cfg.num_agents; i++) inputs[i] = cret_bot_input(sim, i);
                    if (p == P_TPRUN) inputs[0] = mine;
                    cret_sim_step(sim, inputs);
                }
                sim->time_of_day = tod_keep;
            }
            BeginDrawing(); cret_view_render(&view, sim, &cam, dt); EndDrawing();
            if (f < 0) continue;
            Image im = LoadImageFromScreen();
            if (im.width != W || im.height != H) { ImageResize(&im, W, H); }
            fwrite(im.data, 4, (size_t)W * H, pipe);
            if (stills > 0 && f % stills == 0) { char path[1100]; snprintf(path, sizeof path, "%s/f%04d.png", sd, f); ExportImage(im, path); }
            UnloadImage(im);
        }
        double ms = (GetTime() - t0) * 1000.0 / n;
        if (pclose(pipe) != 0) { fprintf(stderr, "paths: ffmpeg failed for %s\n", out); rc = 1; }
        printf("path %s: %d frames, %.1f ms/frame incl. readback -> %s\n", PATHS[p].name, n, ms, out);
        gprof_report(PATHS[p].name);
    }
    cret_view_close(&view); CloseWindow();
    return rc;
}
