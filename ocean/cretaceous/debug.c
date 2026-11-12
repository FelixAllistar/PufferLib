// Developer console and debug overlays (debug.h). Included by cretaceous.c
// (build.sh and build_windows.sh compile that one file); cheats.c and
// scenario.c hold everything that runs without a window.
#include "debug.h"
#include "render/gprof.h"
#include "rlgl.h"
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char* const DBG_BOT_STATE[] = {"wander", "graze", "flee", "hunt", "rest", "drink", "seek water", "feed", "defend", "nest", "raid"};
static const Color DBG_INK = {235, 235, 235, 255}, DBG_DIM = {170, 170, 170, 255}, DBG_ERR = {255, 120, 110, 255}, DBG_OK = {150, 230, 150, 255};
static bool g_dbg_swallow;   // cret_debug_pass: the console owns this frame's keys

static float dbg_wrap(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

void cret_debug_print(CretDebug* d, Color c, const char* fmt, ...) {
    char b[CRET_CON_LEN]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    printf("console: %s\n", b);
    int k = d->nlog % CRET_CON_LINES; snprintf(d->log[k], CRET_CON_LEN, "%s", b); d->log_col[k] = c; d->nlog++; d->scroll = 0;
    d->toast_t = 4.0f;
}

void cret_debug_init(CretDebug* d, CretPop* pop, uint32_t seed, bool authority) {
    memset(d, 0, sizeof *d);
    cret_cheats_init(&d->cheats, pop, seed);
    d->authority = authority; d->target = -1;
    cret_debug_print(d, DBG_DIM, "Cretaceous console: help lists commands, Tab completes, Up/Down history, Esc or ` closes");
    d->toast_t = 0;
}

bool cret_debug_pass(void) { return !g_dbg_swallow; }
float cret_debug_speed(const CretDebug* d) { return d->authority ? d->cheats.speed : 1.0f; }
const char* cret_debug_help(void) { return "` console  F6 dummy  F7 passive  F8 labels  F9 colliders"; }

// ---------------------------------------------------------------- crosshair
// The ray through the screen centre: the ground it meets and the animal it passes closest to.
static void pick(CretDebug* d, const CretSim* s, const CretCamera* cam, int me) {
    float o[3] = {cam->pos[0], cam->pos[1], cam->pos[2]};
    float r[3] = {cosf(cam->pitch) * cosf(cam->yaw), sinf(cam->pitch), cosf(cam->pitch) * sinf(cam->yaw)};
    d->has_aim = false; d->target = -1;
    float hit_t = 400;
    for (float t = 1; t < 400; t += t < 60 ? 0.5f : 2.0f) {
        float x = o[0] + r[0] * t, y = o[1] + r[1] * t, z = o[2] + r[2] * t;
        if (x < 0 || z < 0 || x > s->world->size || z > s->world->size) break;
        if (y < cret_height(s->world, x, z)) { d->has_aim = true; d->aim[0] = x; d->aim[1] = cret_height(s->world, x, z); d->aim[2] = z; hit_t = t; break; }
    }
    float best = 1e9f;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j]; if (j == me || !b->alive) continue;
        float c[3] = {b->x - o[0], b->y + b->height * 0.5f - o[1], b->z - o[2]};
        float t = c[0] * r[0] + c[1] * r[1] + c[2] * r[2]; if (t < 0 || t > hit_t + 4) continue;
        float px = c[0] - r[0] * t, py = c[1] - r[1] * t, pz = c[2] - r[2] * t, miss = sqrtf(px * px + py * py + pz * pz);
        float reach = fmaxf(b->radius, b->height * 0.5f) + CRET_SPECIES_TABLE[b->species].length * cret_growth_scale(b->growth) * 0.3f;
        if (miss < reach && t < best) { best = t; d->target = j; }
    }
}

// ---------------------------------------------------------------- commands
bool cret_debug_view_cmd(CretDebug* d, CretView* v, const CretCmd* c, char* out, int nout) {
    #define TOG(field) ((c)->toggle < 0 ? !(field) : (c)->toggle)
    const char* A0 = c->argc ? c->arg[0] : "";
    switch (c->id) {
    case CMD_PRESET: {
        int p = !strcmp(A0, "low") ? CRET_PRESET_LOW : !strcmp(A0, "medium") ? CRET_PRESET_MEDIUM : !strcmp(A0, "ultra") ? CRET_PRESET_ULTRA : CRET_PRESET_HIGH;
        cret_view_set_preset(v, p); cret_view_resize(v, v->width, v->height);
        snprintf(out, nout, "preset %s (shadows %d, render scale %.2f)", A0, v->settings.shadow_quality, v->settings.render_scale); return true;
    }
    case CMD_SHADOWS:
        v->settings.shadow_quality = c->argc ? (!strcmp(A0, "off") ? 0 : !strcmp(A0, "low") ? 1 : 2) : (v->settings.shadow_quality + 1) % 3;
        snprintf(out, nout, "shadows %s", (const char*[]){"off", "low", "high"}[v->settings.shadow_quality % 3]); return true;
    case CMD_CLOUDS: v->settings.clouds = TOG(v->settings.clouds); snprintf(out, nout, "clouds %s", v->settings.clouds ? "on" : "off"); return true;
    case CMD_VEG: v->settings.vegetation = TOG(v->settings.vegetation); snprintf(out, nout, "vegetation %s", v->settings.vegetation ? "on" : "off"); return true;
    case CMD_GRASS:
        if (!v->veg_ok) { snprintf(out, nout, "grass: no vegetation loaded"); return true; }
        v->veg.grass_on = TOG(v->veg.grass_on); v->veg.grass_valid = false;
        snprintf(out, nout, "grass %s", v->veg.grass_on ? "on" : "off"); return true;
    case CMD_WATER: v->settings.water = TOG(v->settings.water); snprintf(out, nout, "water %s", v->settings.water ? "on" : "off"); return true;
    case CMD_WIREFRAME: v->settings.wireframe = TOG(v->settings.wireframe); snprintf(out, nout, "wireframe %s", v->settings.wireframe ? "on" : "off"); return true;
    case CMD_FOG: {
        bool on = c->toggle < 0 ? d->fog_off : c->toggle;   // toggle: off -> on
        d->fog_off = !on;
        gfx_set_defines(d->fog_off ? "#define CRET_NO_FOG\n" : "");
        // Rebuild every program that includes common.glsl (the modules reload only on a changed file).
        if (v->dinos_ok) { v->dinos.prog.vs_mtime = 0; v->dinos.shadow_prog.vs_mtime = 0; }
        if (v->props_ok) { v->props.prog.vs_mtime = 0; v->props.shadow_prog.vs_mtime = 0; }
        cret_view_reload_shaders(v);
        snprintf(out, nout, "fog %s", on ? "on" : "off"); return true;
    }
    default: return false;
    }
    #undef TOG
}

static void run_line(CretDebug* d, CretSim* s, CretView* v, const CretCamera* cam, int me, const char* line, int force_actor) {
    CretCmd c; char err[160];
    if (!cret_cmd_parse(line, &c, err, sizeof err)) { cret_debug_print(d, DBG_ERR, "%s", err); return; }
    if (force_actor) { c.actor = force_actor; }
    const CretCmdInfo* I = &CRET_CMDS[c.id]; char out[CRET_CON_LEN] = {0};
    if (c.id == CMD_HELP) {
        if (c.argc) { int k = 0; for (; k < CMD_COUNT && strcmp(CRET_CMDS[k].name, c.arg[0]); k++) {}
                      cret_debug_print(d, DBG_INK, "%s  -  %s", CRET_CMDS[k].usage, CRET_CMDS[k].help); return; }
        cret_debug_print(d, DBG_DIM, "cheats (solo/host): spawn kill heal god tp time weather speed freeze grow species   info: pop");
        cret_debug_print(d, DBG_DIM, "overlays: colliders labels navdebug fps profile near   graphics: preset shadows clouds veg grass water fog wireframe");
        cret_debug_print(d, DBG_DIM, "help <command> for its usage; keys: F6 dummy, F7 passive, F8 labels, F9 colliders");
        return;
    }
    if (c.id == CMD_CLEAR) { d->nlog = 0; d->scroll = 0; return; }
    bool reveal = c.id == CMD_LABELS || c.id == CMD_NAVDEBUG || c.id == CMD_NEAR;
    if (!d->authority && (I->cheat || reveal)) { cret_debug_print(d, DBG_ERR, "not allowed: only the host or a solo player can use '%s'", I->name); return; }
    if (I->view) {
        #define TOGGLE(f) do { d->f = c.toggle < 0 ? !d->f : c.toggle; cret_debug_print(d, DBG_OK, "%s %s", I->name, d->f ? "on" : "off"); } while (0)
        switch (c.id) {
        case CMD_COLLIDERS: TOGGLE(colliders); return;
        case CMD_LABELS: TOGGLE(labels); return;
        case CMD_NAVDEBUG: TOGGLE(navdebug); return;
        case CMD_FPS: TOGGLE(fps); return;
        case CMD_NEAR: TOGGLE(nearest); return;
        case CMD_PROFILE: TOGGLE(profile); gprof_set_enabled(d->profile); d->prof_t = 0; return;
        default: break;
        }
        #undef TOGGLE
        if (cret_debug_view_cmd(d, v, &c, out, sizeof out)) cret_debug_print(d, DBG_OK, "%s", out);
        return;
    }
    CretCheatEnv env = {.me = me, .authority = d->authority, .has_aim = d->has_aim, .target = d->target, .cam_yaw = cam ? cam->yaw : s->agents[me].yaw};
    memcpy(env.aim, d->aim, sizeof env.aim);
    if (force_actor) env.has_aim = false;   // hotkeys: 8 m ahead
    bool ok = cret_cheat_exec(&d->cheats, s, &env, &c, out, sizeof out);
    if (out[0]) cret_debug_print(d, ok ? DBG_OK : DBG_ERR, "%s", out);
    if (env.cut) d->cut = true;
}

// ---------------------------------------------------------------- per frame
static void insert_text(CretDebug* d, const char* t) {
    size_t L = strlen(d->line), n = strlen(t);
    if (L + n >= CRET_CON_LEN - 1) return;
    memmove(d->line + d->cursor + n, d->line + d->cursor, L - d->cursor + 1);
    memcpy(d->line + d->cursor, t, n); d->cursor += (int)n;
}
static bool key_rep(int k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); }

bool cret_debug_frame(CretDebug* d, CretSim* s, CretView* v, const CretCamera* cam, int me) {
    d->frame++; d->cut = false;
    if (d->toast_t > 0) d->toast_t -= GetFrameTime();
    if (cam && s) pick(d, s, cam, me);
    // CRET_CONSOLE="spawn stego dummy 3; labels": commands run once at the start (scripted checks, shortcuts).
    if (!d->ran_env && s && d->frame > 2) {
        d->ran_env = true; const char* env = getenv("CRET_CONSOLE");
        if (env) {
            char buf[512]; snprintf(buf, sizeof buf, "%s", env);
            for (char* tok = strtok(buf, ";"); tok; tok = strtok(NULL, ";")) { cret_debug_print(d, DBG_DIM, "> %s", tok); run_line(d, s, v, cam, me, tok, 0); }
        }
    }
    bool was = d->open;
    if (IsKeyPressed(KEY_GRAVE) || IsKeyPressed(KEY_F12)) { d->open = !d->open; d->complete[0] = 0; }
    if (d->open && IsKeyPressed(KEY_ESCAPE) && was) d->open = false;
    g_dbg_swallow = d->open || was;   // the frame it closes still belongs to it (Esc must not open the pause menu)
    if (d->open && !was) { while (GetCharPressed()) {} return true; }   // the toggle key is not typed
    if (!d->open) {
        while (was && GetCharPressed()) {}
        if (!s) return false;
        // Hotkeys.
        if (IsKeyPressed(KEY_F6)) run_line(d, s, v, cam, me, "spawn random dummy", CRET_ACTOR_DUMMY);
        if (IsKeyPressed(KEY_F7)) run_line(d, s, v, cam, me, "spawn random passive", CRET_ACTOR_PASSIVE);
        if (IsKeyPressed(KEY_F8)) run_line(d, s, v, cam, me, "labels", 0);
        if (IsKeyPressed(KEY_F9)) run_line(d, s, v, cam, me, "colliders", 0);
        return false;
    }
    for (int ch; (ch = GetCharPressed());) {
        if (ch < 32 || ch > 126 || ch == '`' || ch == '~') continue;
        char t[2] = {(char)ch, 0}; insert_text(d, t); d->complete[0] = 0;
    }
    int L = (int)strlen(d->line);
    if (key_rep(KEY_BACKSPACE) && d->cursor > 0) { memmove(d->line + d->cursor - 1, d->line + d->cursor, L - d->cursor + 1); d->cursor--; d->complete[0] = 0; }
    if (key_rep(KEY_DELETE) && d->cursor < L) memmove(d->line + d->cursor, d->line + d->cursor + 1, L - d->cursor);
    if (key_rep(KEY_LEFT) && d->cursor > 0) d->cursor--;
    if (key_rep(KEY_RIGHT) && d->cursor < (int)strlen(d->line)) d->cursor++;
    if (IsKeyPressed(KEY_HOME)) d->cursor = 0;
    if (IsKeyPressed(KEY_END)) d->cursor = (int)strlen(d->line);
    if (key_rep(KEY_PAGE_UP)) d->scroll += 5;
    if (key_rep(KEY_PAGE_DOWN)) d->scroll = d->scroll > 5 ? d->scroll - 5 : 0;
    int nh = d->nhist < CRET_CON_HIST ? d->nhist : CRET_CON_HIST;
    if (key_rep(KEY_UP) && d->hist_at < nh) { d->hist_at++; snprintf(d->line, CRET_CON_LEN, "%s", d->hist[(d->nhist - d->hist_at) % CRET_CON_HIST]); d->cursor = (int)strlen(d->line); }
    if (key_rep(KEY_DOWN) && d->hist_at > 0) {
        d->hist_at--;
        if (d->hist_at) snprintf(d->line, CRET_CON_LEN, "%s", d->hist[(d->nhist - d->hist_at) % CRET_CON_HIST]); else d->line[0] = 0;
        d->cursor = (int)strlen(d->line);
    }
    if (IsKeyPressed(KEY_TAB)) {
        char out[CRET_CON_LEN], list[16][24];
        int n = cret_cmd_complete(d->line, out, sizeof out, list, 16);
        snprintf(d->line, CRET_CON_LEN, "%s", out); d->cursor = (int)strlen(d->line);
        d->complete[0] = 0;
        if (n > 1) for (int k = 0; k < n && k < 16; k++) { strncat(d->complete, list[k], sizeof d->complete - strlen(d->complete) - 3); strcat(d->complete, "  "); }
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        char line[CRET_CON_LEN]; snprintf(line, sizeof line, "%s", d->line);
        d->line[0] = 0; d->cursor = 0; d->hist_at = 0; d->complete[0] = 0;
        bool blank = true; for (const char* p = line; *p; p++) if (*p != ' ') blank = false;
        if (!blank) {
            if (!d->nhist || strcmp(d->hist[(d->nhist - 1) % CRET_CON_HIST], line)) { snprintf(d->hist[d->nhist % CRET_CON_HIST], CRET_CON_LEN, "%s", line); d->nhist++; }
            cret_debug_print(d, DBG_DIM, "> %s", line);
            if (s) run_line(d, s, v, cam, me, line, 0);
        }
    }
    return true;
}

void cret_debug_inputs(CretDebug* d, CretSim* s, CretInput* inputs, int me) { if (d->authority) cret_cheats_inputs(&d->cheats, s, inputs, me); }
void cret_debug_after_step(CretDebug* d, CretSim* s, int me) { if (d->authority) cret_cheats_after_step(&d->cheats, s, me); }

// ---------------------------------------------------------------- drawing
// World to screen through the frame's view-projection; segments are clipped at the near plane.
typedef struct { float x, y, w; } Clip;
static Clip to_clip(const float* vp, float x, float y, float z) {
    return (Clip){vp[0] * x + vp[4] * y + vp[8] * z + vp[12], vp[1] * x + vp[5] * y + vp[9] * z + vp[13], vp[3] * x + vp[7] * y + vp[11] * z + vp[15]};
}
static Vector2 to_screen(Clip c) { return (Vector2){(c.x / c.w * 0.5f + 0.5f) * GetScreenWidth(), (0.5f - c.y / c.w * 0.5f) * GetScreenHeight()}; }
static bool dbg_project(const float* vp, float x, float y, float z, Vector2* out) {
    Clip c = to_clip(vp, x, y, z); if (c.w < 0.1f) return false;
    *out = to_screen(c);
    return out->x > -200 && out->x < GetScreenWidth() + 200 && out->y > -200 && out->y < GetScreenHeight() + 200;
}
static void line3(const float* vp, const float a[3], const float b[3], float thick, Color col) {
    Clip ca = to_clip(vp, a[0], a[1], a[2]), cb = to_clip(vp, b[0], b[1], b[2]); const float N = 0.1f;
    if (ca.w < N && cb.w < N) return;
    if (ca.w < N) { float t = (N - ca.w) / (cb.w - ca.w); ca = (Clip){ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, N}; }
    if (cb.w < N) { float t = (N - cb.w) / (ca.w - cb.w); cb = (Clip){cb.x + (ca.x - cb.x) * t, cb.y + (ca.y - cb.y) * t, N}; }
    DrawLineEx(to_screen(ca), to_screen(cb), thick, col);
}
static void ring3(const float* vp, const float c[3], const float u[3], const float w[3], float r, Color col) {
    float prev[3];
    for (int k = 0; k <= 16; k++) {
        float a = k * (2 * PI / 16), p[3];
        for (int q = 0; q < 3; q++) p[q] = c[q] + (u[q] * cosf(a) + w[q] * sinf(a)) * r;
        if (k) line3(vp, prev, p, 1.2f, col);
        memcpy(prev, p, sizeof p);
    }
}

typedef struct { b3ShapeId id[768]; int n; } DbgShapes;
static bool dbg_shape_fn(b3ShapeId id, void* ctx) { DbgShapes* q = ctx; if (q->n < 768) q->id[q->n++] = id; return q->n < 768; }

static void draw_colliders(const CretSim* s, const CretView* v, const CretCamera* cam) {
    if (!b3World_IsValid(s->phys)) return;
    const float* vp = v->viewproj; float R = 45;
    b3AABB box = {{cam->pos[0] - R, cam->pos[1] - R, cam->pos[2] - R}, {cam->pos[0] + R, cam->pos[1] + R, cam->pos[2] + R}};
    b3QueryFilter f = b3DefaultQueryFilter(); f.categoryBits = ~0ull; f.maskBits = CRET_CAT_AGENT | CRET_CAT_PROP;
    static DbgShapes q; q.n = 0;
    b3World_OverlapAABB(s->phys, box, f, dbg_shape_fn, &q);
    for (int k = 0; k < q.n; k++) {
        b3ShapeId id = q.id[k]; b3BodyId body = b3Shape_GetBody(id); b3WorldTransform T = b3Body_GetTransform(body);
        b3Filter sf = b3Shape_GetFilter(id);
        Color col = (sf.categoryBits & CRET_CAT_AGENT) ? (Color){255, 220, 60, 230} : b3Body_GetType(body) == b3_dynamicBody ? (Color){255, 140, 40, 230} : (Color){70, 220, 255, 200};
        #define WP(v3, out) do { b3Pos P_ = b3TransformWorldPoint(T, (v3)); (out)[0] = (float)P_.x; (out)[1] = (float)P_.y; (out)[2] = (float)P_.z; } while (0)
        switch (b3Shape_GetType(id)) {
        case b3_capsuleShape: {
            b3Capsule c = b3Shape_GetCapsule(id); float a[3], b[3]; WP(c.center1, a); WP(c.center2, b);
            float ax[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, L = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
            if (L < 1e-4f) { ax[0] = 0; ax[1] = 1; ax[2] = 0; } else for (int i = 0; i < 3; i++) ax[i] /= L;
            float u[3] = {ax[1] * 0 - ax[2] * 1, ax[2] * 0 - ax[0] * 0, ax[0] * 1 - ax[1] * 0};   // ax x up
            float un = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); if (un < 1e-3f) { u[0] = 1; u[1] = u[2] = 0; } else for (int i = 0; i < 3; i++) u[i] /= un;
            float w[3] = {ax[1] * u[2] - ax[2] * u[1], ax[2] * u[0] - ax[0] * u[2], ax[0] * u[1] - ax[1] * u[0]};
            ring3(vp, a, u, w, c.radius, col); ring3(vp, b, u, w, c.radius, col);
            ring3(vp, a, ax, u, c.radius, col); ring3(vp, b, ax, u, c.radius, col);
            for (int s4 = 0; s4 < 4; s4++) {
                float ang = s4 * PI / 2, o[3], p0[3], p1[3];
                for (int i = 0; i < 3; i++) { o[i] = (u[i] * cosf(ang) + w[i] * sinf(ang)) * c.radius; p0[i] = a[i] + o[i]; p1[i] = b[i] + o[i]; }
                line3(vp, p0, p1, 1.2f, col);
            }
            break;
        }
        case b3_sphereShape: {
            b3Sphere sp = b3Shape_GetSphere(id); float c[3]; WP(sp.center, c);
            float X[3] = {1, 0, 0}, Y[3] = {0, 1, 0}, Z[3] = {0, 0, 1};
            ring3(vp, c, X, Z, sp.radius, col); ring3(vp, c, X, Y, sp.radius, col); ring3(vp, c, Y, Z, sp.radius, col);
            break;
        }
        case b3_hullShape: {
            const b3HullData* h = b3Shape_GetHull(id); if (!h) break;
            const b3Vec3* pts = b3GetHullPoints(h); const b3HullHalfEdge* e = b3GetHullEdges(h);
            for (int i = 0; i < h->edgeCount; i++) {
                if (e[i].twin < i) continue;   // each full edge once
                float a[3], b[3]; WP(pts[e[i].origin], a); WP(pts[e[e[i].next].origin], b);
                line3(vp, a, b, 1.2f, col);
            }
            break;
        }
        case b3_meshShape: case b3_compoundShape: {
            b3AABB bb = b3Shape_GetAABB(id); float lo[3] = {bb.lowerBound.x, bb.lowerBound.y, bb.lowerBound.z}, hi[3] = {bb.upperBound.x, bb.upperBound.y, bb.upperBound.z};
            for (int i = 0; i < 12; i++) {   // the box's edges
                static const int E[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                float a[3], b[3];
                for (int q3 = 0; q3 < 3; q3++) { a[q3] = (E[i][0] >> q3) & 1 ? hi[q3] : lo[q3]; b[q3] = (E[i][1] >> q3) & 1 ? hi[q3] : lo[q3]; }
                line3(vp, a, b, 1.0f, (Color){col.r, col.g, col.b, 140});
            }
            break;
        }
        default: break;   // the terrain height field
        }
        #undef WP
    }
}

static void text_shadow(const char* t, float x, float y, int fs, Color c) {
    DrawText(t, (int)x + 1, (int)y + 1, fs, (Color){0, 0, 0, 170}); DrawText(t, (int)x, (int)y, fs, c);
}

static void draw_labels(CretDebug* d, const CretSim* s, const CretView* v, const CretCamera* cam, int me, float ui) {
    int fs = (int)(13 * ui);
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* a = &s->agents[j]; if (!a->alive) continue;
        float dist = hypotf(a->x - cam->pos[0], a->z - cam->pos[2]); if (dist > 150) continue;
        Vector2 p; if (!dbg_project(v->viewproj, a->x, a->y + a->height + 0.6f, a->z, &p)) continue;
        bool carn = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE;
        char line1[96], line2[96];
        const char* who = j == me ? "you" : cret_is_person(s, j) ? "player" : d->cheats.actor[j] ? CRET_ACTOR_NAMES[d->cheats.actor[j]] : "bot";
        snprintf(line1, sizeof line1, "%s #%d %s%s", CRET_SPECIES_TABLE[a->species].asset, j, who, a->growth < 0.95f ? TextFormat(" g%.2f", a->growth) : "");
        const char* st = d->cheats.actor[j] ? "" : a->bot_state < (int)(sizeof DBG_BOT_STATE / sizeof *DBG_BOT_STATE) ? DBG_BOT_STATE[a->bot_state] : "?";
        int t = a->bot_target;
        snprintf(line2, sizeof line2, "%s%s  %.0f%%  %.0f m%s", cret_is_person(s, j) ? "" : st,
                 !cret_is_person(s, j) && !d->cheats.actor[j] && t >= 0 && t < s->cfg.num_agents && (a->bot_state == 2 || a->bot_state == 3 || a->bot_state == 8) ? TextFormat(" -> #%d %s", t, CRET_SPECIES_TABLE[s->agents[t].species].asset) : "",
                 100 * a->health / fmaxf(a->max_health, 1), dist, j == d->target ? "  [crosshair]" : "");
        Color c = carn ? (Color){255, 170, 150, 255} : (Color){170, 235, 170, 255};
        text_shadow(line1, p.x - MeasureText(line1, fs) * 0.5f, p.y - fs * 2.2f, fs, c);
        text_shadow(line2, p.x - MeasureText(line2, fs) * 0.5f, p.y - fs * 1.1f, fs, DBG_INK);
    }
}

static void draw_nav(const CretSim* s, const CretView* v, const CretCamera* cam, int me) {
    const float* vp = v->viewproj;
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* a = &s->agents[j]; if (!a->alive || cret_is_person(s, j)) continue;
        if (hypotf(a->x - cam->pos[0], a->z - cam->pos[2]) > 120) continue;
        float p0[3] = {a->x, a->y + 0.4f, a->z};
        if (a->goal_x != 0 || a->goal_z != 0) {
            float g[3] = {a->goal_x, cret_height(s->world, a->goal_x, a->goal_z) + 0.4f, a->goal_z};
            line3(vp, p0, g, 1.5f, (Color){120, 255, 120, 200});
            float up[3] = {g[0], g[1] + 3, g[2]}; line3(vp, g, up, 2.5f, (Color){120, 255, 120, 230});
        }
        float L = 8 + a->radius * 4, f[3] = {a->x + cosf(a->yaw) * L, 0, a->z + sinf(a->yaw) * L};
        f[1] = cret_height(s->world, f[0], f[2]) + 0.4f;
        bool ok = cret_walkable(s->world, f[0], f[2]);
        line3(vp, p0, f, 1.5f, ok ? (Color){200, 200, 255, 180} : (Color){255, 80, 80, 230});
        if (a->escape_ticks > 0 || a->stuck_ticks > 30) {
            Vector2 sp; if (dbg_project(vp, a->x, a->y + a->height + 1.6f, a->z, &sp)) text_shadow(a->escape_ticks > 0 ? "escaping" : "stuck", sp.x - 20, sp.y, 14, DBG_ERR);
        }
    }
    // Unwalkable ground near the player (water, cliffs): red dots on a 3 m grid.
    const CretAgent* m = &s->agents[me];
    for (float dz = -36; dz <= 36; dz += 3) for (float dx = -36; dx <= 36; dx += 3) {
        float x = m->x + dx, z = m->z + dz; if (cret_walkable(s->world, x, z)) continue;
        Vector2 sp; if (dbg_project(vp, x, fmaxf(cret_height(s->world, x, z), cret_water(s->world, x, z)) + 0.2f, z, &sp)) DrawCircleV(sp, 2.5f, (Color){255, 60, 60, 200});
    }
}

static void draw_near(const CretSim* s, const CretCamera* cam, int me, float ui) {
    const CretAgent* m = &s->agents[me]; int idx[8], n = 0; float dist[8];
    for (int j = 0; j < s->cfg.num_agents; j++) {
        const CretAgent* b = &s->agents[j]; if (j == me || !b->alive) continue;
        float d = hypotf(b->x - m->x, b->z - m->z); int k = n < 8 ? n++ : 8;
        if (k == 8) { if (d >= dist[7]) continue; k = 7; }
        while (k > 0 && dist[k - 1] > d) { dist[k] = dist[k - 1]; idx[k] = idx[k - 1]; k--; }
        dist[k] = d; idx[k] = j;
    }
    int fs = (int)(14 * ui); float x = GetScreenWidth() - 330 * ui, y = 60 * ui;
    DrawRectangle((int)(x - 8 * ui), (int)(y - 6 * ui), (int)(320 * ui), (int)((n + 1) * fs * 1.3f + 10 * ui), (Color){0, 0, 0, 120});
    text_shadow("nearest animals", x, y, fs, DBG_DIM); y += fs * 1.3f;
    for (int k = 0; k < n; k++) {
        const CretAgent* b = &s->agents[idx[k]];
        float rel = dbg_wrap(atan2f(b->z - m->z, b->x - m->x) - cam->yaw) * 180 / PI;
        const char* side = fabsf(rel) < 15 ? "ahead" : fabsf(rel) > 150 ? "behind" : rel > 0 ? "right" : "left";
        char t[96]; snprintf(t, sizeof t, "%-10s #%-3d %5.0f m  %4.0f deg %s", CRET_SPECIES_TABLE[b->species].asset, idx[k], dist[k], fabsf(rel), side);
        text_shadow(t, x, y, fs, CRET_SPECIES_TABLE[b->species].diet == CRET_DIET_CARNIVORE ? (Color){255, 180, 160, 255} : (Color){180, 240, 180, 255});
        y += fs * 1.3f;
    }
}

static void draw_fps(CretDebug* d, float ui) {
    float w = 2 * ui, h = 70 * ui, x0 = GetScreenWidth() * 0.5f - CRET_FPS_HIST * w * 0.5f, y0 = 150 * ui;
    DrawRectangle((int)x0 - 4, (int)(y0 - h - 18 * ui), (int)(CRET_FPS_HIST * w + 8), (int)(h + 24 * ui), (Color){0, 0, 0, 120});
    float worst = 0, sum = 0;
    for (int k = 0; k < CRET_FPS_HIST; k++) {
        float ms = d->frame_ms[(d->frame_i + k) % CRET_FPS_HIST]; worst = fmaxf(worst, ms); sum += ms;
        Color c = ms < 17.5f ? (Color){120, 220, 120, 220} : ms < 34 ? (Color){240, 210, 90, 220} : (Color){255, 90, 80, 230};
        float bh = fminf(ms / 50.0f, 1) * h; DrawRectangle((int)(x0 + k * w), (int)(y0 - bh), (int)fmaxf(w, 1), (int)bh, c);
    }
    DrawLine((int)x0, (int)(y0 - h * 16.7f / 50), (int)(x0 + CRET_FPS_HIST * w), (int)(y0 - h * 16.7f / 50), (Color){255, 255, 255, 90});
    text_shadow(TextFormat("frame %.1f ms avg, %.1f worst (line 60 fps, top 50 ms)", sum / CRET_FPS_HIST, worst), x0, y0 - h - 15 * ui, (int)(12 * ui), DBG_INK);
}

static void draw_profile(CretDebug* d, float ui) {
    if (GetTime() - d->prof_t > 0.5) {
        d->prof_t = GetTime();
        d->prof_n = gprof_snapshot(d->prof_name, d->prof_gpu, d->prof_cpu, 48, &d->prof_tot_gpu, &d->prof_tot_cpu);
    }
    int fs = (int)(13 * ui); float x = 12 * ui, y = 120 * ui;
    DrawRectangle((int)(x - 6), (int)(y - 4), (int)(270 * ui), (int)((d->prof_n + 2) * fs * 1.25f + 8), (Color){0, 0, 0, 130});
    text_shadow(TextFormat("pass            GPU ms  CPU ms   (total %.2f / %.2f)", d->prof_tot_gpu, d->prof_tot_cpu), x, y, fs, DBG_DIM); y += fs * 1.25f;
    if (!d->prof_n) text_shadow("collecting...", x, y, fs, DBG_DIM);
    for (int k = 0; k < d->prof_n; k++, y += fs * 1.25f) text_shadow(TextFormat("%-14s %7.2f %7.2f", d->prof_name[k], d->prof_gpu[k], d->prof_cpu[k]), x, y, fs, d->prof_gpu[k] > 3 ? DBG_ERR : DBG_INK);
}

static void draw_console(CretDebug* d, float ui) {
    int fs = (int)(15 * ui); float lh = fs * 1.3f; int rows = 18; float W = (float)GetScreenWidth(), H = rows * lh + lh * 2.6f;
    DrawRectangle(0, 0, (int)W, (int)H, (Color){10, 12, 14, 215});
    DrawLine(0, (int)H, (int)W, (int)H, (Color){120, 160, 120, 200});
    int n = d->nlog < CRET_CON_LINES ? d->nlog : CRET_CON_LINES;
    int maxscroll = n > rows ? n - rows : 0; if (d->scroll > maxscroll) d->scroll = maxscroll;
    for (int r = 0; r < rows; r++) {
        int k = d->nlog - d->scroll - rows + r; if (k < 0 || k < d->nlog - n) continue;
        DrawText(d->log[k % CRET_CON_LINES], (int)(10 * ui), (int)(6 * ui + r * lh), fs, d->log_col[k % CRET_CON_LINES]);
    }
    float py = 6 * ui + rows * lh + lh * 0.3f;
    if (d->complete[0]) DrawText(d->complete, (int)(10 * ui), (int)(py - lh * 0.15f + lh), (int)(fs * 0.9f), DBG_DIM);
    char shown[CRET_CON_LEN + 4]; snprintf(shown, sizeof shown, "> %s", d->line);
    DrawText(shown, (int)(10 * ui), (int)py, fs, DBG_INK);
    if ((int)(GetTime() * 2) % 2 == 0) {
        char pre[CRET_CON_LEN + 4]; snprintf(pre, sizeof pre, "> %.*s", d->cursor, d->line);
        int cx = (int)(10 * ui) + MeasureText(pre, fs); DrawRectangle(cx, (int)py, (int)fmaxf(2, ui * 2), fs, DBG_INK);
    }
    if (!d->authority) DrawText("client: cheats are the host's", (int)(W - MeasureText("client: cheats are the host's", fs) - 12 * ui), (int)py, fs, DBG_DIM);
}

void cret_debug_draw(CretDebug* d, const CretSim* draw, CretSim* sim, const CretView* view, const CretCamera* cam, int me, float ui) {
    d->frame_ms[d->frame_i] = fminf(GetFrameTime() * 1000, 1000); d->frame_i = (d->frame_i + 1) % CRET_FPS_HIST;
    if (ui <= 0) ui = 1;
    if (d->colliders) draw_colliders(sim, view, cam);
    if (d->navdebug && d->authority) draw_nav(draw, view, cam, me);
    if (d->labels && d->authority) draw_labels(d, draw, view, cam, me, ui);
    if (d->nearest && d->authority) draw_near(draw, cam, me, ui);
    if (d->fps) draw_fps(d, ui);
    if (d->profile) draw_profile(d, ui);
    if (d->open) {
        // The crosshair and what it points at, while the console is up (spawns go there).
        float cx = GetScreenWidth() * 0.5f, cy = GetScreenHeight() * 0.5f;
        DrawLineEx((Vector2){cx - 8, cy}, (Vector2){cx + 8, cy}, 1.5f, DBG_INK); DrawLineEx((Vector2){cx, cy - 8}, (Vector2){cx, cy + 8}, 1.5f, DBG_INK);
        if (d->target >= 0) text_shadow(TextFormat("#%d %s", d->target, CRET_SPECIES_TABLE[draw->agents[d->target].species].asset), cx + 12, cy + 6, (int)(13 * ui), DBG_INK);
        else if (d->has_aim) text_shadow(TextFormat("%.0f %.0f", d->aim[0], d->aim[2]), cx + 12, cy + 6, (int)(13 * ui), DBG_DIM);
        draw_console(d, ui);
    } else if (d->toast_t > 0 && d->nlog) {
        int fs = (int)(15 * ui); const char* t = d->log[(d->nlog - 1) % CRET_CON_LINES];
        text_shadow(t, GetScreenWidth() * 0.5f - MeasureText(t, fs) * 0.5f, 52 * ui, fs, (Color){235, 235, 235, (unsigned char)(255 * fminf(d->toast_t, 1))});
    }
    rlDrawRenderBatchActive();   // into the back buffer now (--screenshot reads it before EndDrawing)
}
