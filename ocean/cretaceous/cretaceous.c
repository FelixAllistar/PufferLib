// Cretaceous: player, listen host, client, dedicated server and capture tool.
//   ./cretaceous                        title screen (Play, Host, Join, Settings, Credits)
//   ./cretaceous play [--seed N] [--agents N] [--species raptor|stego|trex|pachy|sauropelta|galli] [--size W H]
//                [--character NAME (saved in ~/.config/cretaceous/characters)]
//                [--day-minutes M] [--clouds C (fixed cover, no weather)]
//   ./cretaceous host [--port P]        listen server; you play slot 0
//   ./cretaceous join ADDRESS [--port P] [--species S]   the server spawns you as S
//                [--character NAME (the server keeps it)] [--netsim "lat=60 jit=10 loss=1"]   (network conditions: netsim.h)
//   ./cretaceous server [--port P]      headless dedicated server
//   ./cretaceous capture DIR [--shot NAME] [--size W H]   review stills
//   ./cretaceous capture DIR --path NAME [--frames N] [--fps F]   camera-path video (capture.c)
// Run from the repository root (shaders and textures load from ocean/cretaceous).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
#include "raylib.h"
#include "sim.h"
#include "net.h"
#include "render/view.h"
#include "render/gprof.h"
#include "render/cache.h"
#include "hud.h"
#include "audio.h"
#include "menu.h"
#include "capture.h"
#include "character.h"
#include "abilities.h"
#include "progress.h"

#include "render/fx.c"      // combat feedback (presentation only)

typedef enum { MODE_PLAY, MODE_HOST, MODE_JOIN, MODE_SERVER, MODE_CAPTURE } Mode;
static CretFxSettings g_fxs = {1.0f, 1.0f};   // camera shake and hit-stop (settings)

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float wrap_pi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

// ---------------------------------------------------------------- mouse look
// raylib 5.5 builds GLFW 3.4 into libraylib.a; these are its entry points.
typedef struct GLFWwindow GLFWwindow;
GLFWwindow* glfwGetCurrentContext(void);
int glfwRawMouseMotionSupported(void);
void glfwSetInputMode(GLFWwindow* window, int mode, int value);
#define CRET_GLFW_RAW_MOUSE_MOTION 0x00033005

// Mouse look. Two schemes:
//  - captured (native Linux and Windows): the cursor is hidden and locked, and
//    every motion turns the camera. Raw (unaccelerated) motion where GLFW has
//    it. The first frames after a capture, a focus change or a resize are
//    dropped (the cursor is warped to the window centre then), and a jump of
//    more than a quarter of the window in one frame is a warp, not a hand.
//  - drag (the default under WSL): WSLg cannot lock the pointer. Its pointer is
//    the Windows one, an absolute device, so the warp-to-centre that a locked
//    cursor relies on happens only on the Linux side; the next real motion then
//    arrives as a jump back toward where the Windows pointer is (the view
//    spins, and left sometimes turns right), and the grab also swallows clicks
//    on the window's close button. So under WSL nothing is grabbed or warped:
//    holding the right button and dragging orbits the camera (from absolute
//    positions, which WSLg reports correctly), a quick right click is the
//    secondary attack, and the camera swings in behind the animal while it
//    runs forward.
// CRET_MOUSE=drag|capture picks the scheme; CRET_MOUSE_RAW=0/1 forces raw
// motion off or on (captured); CRET_MOUSE_DEBUG=1 prints every delta.
typedef struct MouseLook {
    bool raw, raw_forced, debug, focused;
    bool drag_mode, active;            // drag scheme; play has the mouse (not a menu)
    bool dragging, drag_moved, tap2;   // right button held; moved past the tap slop; a quick right click to deliver
    int skip, outliers, w, h; double outlier_t, drag_t; Vector2 last, press;
} MouseLook;

static bool running_on_wsl(void) {
#ifdef _WIN32
    return false;   // the native Windows build: real pointer capture
#endif
    if (getenv("WSL_DISTRO_NAME")) return true;
    char b[256] = {0}; FILE* f = fopen("/proc/sys/kernel/osrelease", "r");
    if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
    return strstr(b, "microsoft") || strstr(b, "Microsoft") || strstr(b, "WSL");
}
static void mouse_init(MouseLook* m) {
    const char* e = getenv("CRET_MOUSE_RAW");
    m->raw_forced = e && *e; m->raw = m->raw_forced ? atoi(e) != 0 : !running_on_wsl();
    const char* mode = getenv("CRET_MOUSE");
    m->drag_mode = mode && *mode ? strcmp(mode, "capture") != 0 : running_on_wsl();
    m->debug = getenv("CRET_MOUSE_DEBUG") && atoi(getenv("CRET_MOUSE_DEBUG"));
    m->focused = true; m->skip = 2;
    if (m->drag_mode) fprintf(stderr, "cretaceous: mouse: hold the right button and drag to turn the camera (CRET_MOUSE=capture locks the cursor instead)\n");
}
static void mouse_set_raw(const MouseLook* m) {
    GLFWwindow* w = glfwGetCurrentContext();   // raylib's only window
    if (w && glfwRawMouseMotionSupported()) glfwSetInputMode(w, CRET_GLFW_RAW_MOUSE_MOTION, m->raw);
}
static void mouse_capture(MouseLook* m, bool on) {
    m->active = on; m->skip = 2;
    if (m->drag_mode) { if (m->dragging) ShowCursor(); m->dragging = m->tap2 = false; return; }
    if (on) { DisableCursor(); mouse_set_raw(m); }   // DisableCursor turns raw motion on wherever it is supported
    else EnableCursor();
    if (m->debug) fprintf(stderr, "mouse: %s (raw %d, supported %d)\n", on ? "captured" : "released", m->raw, glfwRawMouseMotionSupported());
}
// Play has the mouse: clicks attack (captured: the cursor is locked; drag: no menu is open).
static bool mouse_playing(const MouseLook* m) { return m->drag_mode ? m->active : IsCursorHidden(); }
// The right button orbits in the drag scheme, so it attacks only on a quick tap.
static bool mouse_take_tap2(MouseLook* m) { bool t = m->tap2; m->tap2 = false; return t; }

static Vector2 mouse_drag_delta(MouseLook* m) {
    Vector2 zero = {0, 0}, p = GetMousePosition();
    if (m->debug && (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)))
        fprintf(stderr, "mouse: right %s at %.0f %.0f (active %d, focused %d)\n", IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ? "down" : "up", p.x, p.y, m->active, IsWindowFocused());
    if (!m->active || !IsWindowFocused()) { if (m->dragging) { m->dragging = false; ShowCursor(); } return zero; }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { m->dragging = true; m->drag_moved = false; m->drag_t = GetTime(); m->last = m->press = p; return zero; }
    if (!m->dragging) return zero;
    if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        m->dragging = false; ShowCursor();
        if (!m->drag_moved && GetTime() - m->drag_t < 0.3) m->tap2 = true;
        return zero;
    }
    Vector2 d = {p.x - m->last.x, p.y - m->last.y}; m->last = p;
    if (!m->drag_moved) {
        if (fabsf(p.x - m->press.x) + fabsf(p.y - m->press.y) < 6) return zero;   // tap slop
        m->drag_moved = true; HideCursor();   // hidden only: nothing is grabbed or warped
    }
    if (m->debug && (d.x || d.y)) fprintf(stderr, "mouse: drag %+.0f %+.0f\n", d.x, d.y);
    return d;
}

static Vector2 mouse_look_delta(MouseLook* m) {
    if (m->drag_mode) return mouse_drag_delta(m);
    Vector2 d = GetMouseDelta(), zero = {0, 0};
    bool f = IsWindowFocused(); int w = GetScreenWidth(), h = GetScreenHeight();
    if (f != m->focused || w != m->w || h != m->h) { m->focused = f; m->w = w; m->h = h; m->skip = 2; }
    if (!IsCursorHidden() || !f) return zero;
    if (m->skip > 0) { m->skip--; if (m->debug && (d.x || d.y)) fprintf(stderr, "mouse: dropped %+.0f %+.0f (settling)\n", d.x, d.y); return zero; }
    float lim = 0.25f * (float)(w > h ? w : h);
    if (fabsf(d.x) > lim || fabsf(d.y) > lim) {
        double now = GetTime();
        if (now - m->outlier_t > 2.0) m->outliers = 0;
        m->outlier_t = now; m->outliers++;
        if (m->debug) fprintf(stderr, "mouse: dropped %+.0f %+.0f (jump)\n", d.x, d.y);
        if (m->outliers >= 3 && m->raw && !m->raw_forced) {
            m->raw = false; mouse_set_raw(m); m->skip = 2;
            fprintf(stderr, "cretaceous: raw mouse motion jumped %d times in 2 s; turned it off (CRET_MOUSE_RAW=1 keeps it on)\n", m->outliers);
        }
        return zero;
    }
    if (m->debug && (d.x || d.y)) fprintf(stderr, "mouse: %+.0f %+.0f\n", d.x, d.y);
    return d;
}

// ---------------------------------------------------------------- camera control
// The third-person rig: the pivot sits just behind the shoulders a little
// above the back (1.05 x height), the boom is 1.5 body lengths at zoom 1, so
// a raptor and a tyrannosaur fill the frame alike. Zoom 0.6x to 2.5x; the
// look pitch stays between 0.55 rad down and 0.45 up (never top-down).
#define CAM_BOOM_LENGTHS 1.5f
#define CAM_PIVOT_UP 1.05f
#define CAM_PIVOT_FWD 0.05f
#define CAM_PITCH_MIN -0.55f
#define CAM_PITCH_MAX 0.45f
#define CAM_ZOOM_MIN 0.6f
#define CAM_ZOOM_MAX 2.5f
#define CAM_RADIUS 0.3f      // the boom's sphere: the lens stays this far from rocks, logs and trunks
#define CAM_CLEAR 0.45f      // and this far above ground and water
#define CAM_LOOK 0.002f      // radians per pixel at sensitivity 1 (1000 px: 115 degrees)

typedef struct {
    bool free_cam; float yaw, pitch, zoom, zoom_s; float pos[3]; bool autorun;
    float boom;              // current boom length after collision (pulled in at once, eased out)
    float target[3]; bool target_ok;   // smoothed pivot
    float sens; bool invert_y;         // settings
    MouseLook mouse;
} Control;

static void cam_pivot(const CretAgent* a, float out[3]) {
    float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth);
    out[0] = a->x + cosf(a->yaw) * CAM_PIVOT_FWD * L; out[1] = a->y + a->height * CAM_PIVOT_UP; out[2] = a->z + sinf(a->yaw) * CAM_PIVOT_FWD * L;
}
static float cam_boom(const CretAgent* a, float zoom) {
    return fmaxf(CAM_BOOM_LENGTHS * CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth) * zoom, 1.5f);
}

// Head aim (-1..1 across the neck arc) from the camera: where the camera
// looks, relative to the body. The stegosaur aims its tail behind it, so its
// aim fades to straight back while the camera looks forward.
static float player_aim(const Control* c, const CretAgent* a) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    float rel = wrap_pi(c->yaw - a->yaw);
    if (!S->tail) return clampf(rel / S->neck_arc, -1, 1);
    float t = clampf((fabsf(rel) - PI / 2) / 0.5f, 0, 1); t = t * t * (3 - 2 * t);
    return clampf(wrap_pi(rel - PI) / S->neck_arc, -1, 1) * t;
}

// Third-person controls: WASD relative to the camera (the body turns toward
// the camera's forward plus the keys, at the species' turn rate), Shift
// sprint, Ctrl walk, Space jump, R autorun, E eat or drink (hold), G call,
// left mouse attack, right mouse secondary attack (both aimed with the
// camera), 1-3 abilities, N (hold) build a nest.
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
    in.aim = player_aim(c, a);
    if (IsKeyDown(KEY_E)) {
        float ax = a->x + cosf(a->yaw) * (a->radius + 1.5f), az = a->z + sinf(a->yaw) * (a->radius + 1.5f);
        in.action = cret_water(w, ax, az) > cret_height(w, ax, az) ? CRET_ACT_DRINK : CRET_ACT_EAT;
    }
    if (IsKeyPressed(KEY_G)) in.action = CRET_ACT_CALL;
    if (mouse_playing(&c->mouse)) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) in.action = CRET_ACT_ATTACK;
        else if (c->mouse.drag_mode ? mouse_take_tap2(&c->mouse) : IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) in.action = CRET_ACT_ATTACK2;
    }
    if (IsKeyPressed(KEY_Q)) in.action = CRET_ACT_ATTACK2;
    if (IsKeyDown(KEY_N)) in.action = CRET_ACT_NEST;
    if (IsKeyPressed(KEY_ONE)) in.ability = 1;
    if (IsKeyPressed(KEY_TWO)) in.ability = 2;
    if (IsKeyPressed(KEY_THREE)) in.ability = 3;
    return in;
}

// Boom collision: a sphere swept out from the pivot stops at rocks, logs and
// cliff props (Box3D prop colliders) and at tree trunks; terrain and water
// lift the lens instead, unless a ridge hides the animal, then the boom
// shortens to it. Leaves and other animals never stop it, and nothing turns
// see-through.
typedef struct { float best; } CamCast;
static float cam_cast_fn(b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction, uint64_t mat, int tri, int child, void* ctx) {
    (void)shape; (void)point; (void)normal; (void)mat; (void)tri; (void)child;
    if (fraction <= 1e-4f) return -1;   // the pivot itself is inside this one: ignore it
    CamCast* c = ctx; if (fraction < c->best) c->best = fraction;
    return fraction;
}
static float cam_ground(const CretWorld* w, float x, float z) { return fmaxf(cret_height(w, x, z), cret_water(w, x, z)) + CAM_CLEAR; }
static bool cam_sees(const CretWorld* w, const float a[3], const float b[3]) {   // terrain between two points?
    for (int i = 1; i < 16; i++) {
        float t = i / 16.0f, x = a[0] + (b[0] - a[0]) * t, y = a[1] + (b[1] - a[1]) * t, z = a[2] + (b[2] - a[2]) * t;
        if (y < cam_ground(w, x, z) - CAM_CLEAR - 0.05f) return false;
    }
    return true;
}
static float cam_clear(const CretSim* s, const CretVeg* veg, const float p[3], const float d[3], float L) {
    float t = L;
    if (b3World_IsValid(s->phys)) {
        b3Vec3 pt = {0, 0, 0}; b3ShapeProxy proxy = {&pt, 1, CAM_RADIUS};
        b3QueryFilter f = {.categoryBits = CRET_CAT_AGENT, .maskBits = CRET_CAT_PROP, .id = 3, .name = "cret_camera"};
        CamCast cc = {1.0f};
        b3World_CastShape(s->phys, (b3Pos){p[0], p[1], p[2]}, &proxy, (b3Vec3){d[0] * L, d[1] * L, d[2] * L}, f, cam_cast_fn, &cc);
        t = fminf(t, cc.best * L);
    }
    if (veg) t = fminf(t, cret_veg_cast_trunks(veg, p, d, t, CAM_RADIUS));
    // Terrain: the full boom lifted clear of the ground, if the animal stays in sight from there; else stop at the ridge.
    float e[3] = {p[0] + d[0] * t, p[1] + d[1] * t, p[2] + d[2] * t};
    e[1] = fmaxf(e[1], cam_ground(s->world, e[0], e[2]));
    if (!cam_sees(s->world, e, p)) {
        for (int i = 1; i <= 32; i++) {
            float u = t * i / 32.0f, x = p[0] + d[0] * u, y = p[1] + d[1] * u, z = p[2] + d[2] * u;
            if (y < cam_ground(s->world, x, z) - CAM_CLEAR) { t = fmaxf(u - t / 32.0f, 0.5f); break; }
        }
    }
    return t;
}

static void update_camera(Control* c, const CretSim* s, const CretVeg* veg, int me, CretCamera* cam, bool look, float dt) {
    Vector2 md = mouse_look_delta(&c->mouse);
    float ms = CAM_LOOK * (c->sens > 0 ? c->sens : 1.0f);
    if (look) {
        c->zoom = clampf(c->zoom * (1 - GetMouseWheelMove() * 0.1f), CAM_ZOOM_MIN, CAM_ZOOM_MAX);
        c->yaw = wrap_pi(c->yaw + md.x * ms);
        c->pitch -= md.y * ms * (c->invert_y ? -1 : 1);
    }
    // Drag scheme: while the animal runs roughly away from the camera and the
    // right button is up, the camera swings in behind it (strafing sideways
    // leaves it alone, so A or D alone doesn't spin you in circles).
    if (look && c->mouse.drag_mode && !c->mouse.dragging && !c->free_cam) {
        const CretAgent* a = &s->agents[me];
        float diff = wrap_pi(a->yaw - c->yaw), go = clampf(a->speed / 3.0f, 0, 1);
        if (a->alive && a->speed > 0.5f && fabsf(diff) < 1.2f) c->yaw = wrap_pi(c->yaw + diff * (1 - expf(-dt * 1.8f * go)));
    }
    c->pitch = c->free_cam ? clampf(c->pitch, -1.45f, 1.2f) : clampf(c->pitch, CAM_PITCH_MIN, CAM_PITCH_MAX);
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
    // The pivot follows the animal with a little lag (more vertically, to
    // smooth the gait); the camera looks along the control yaw and pitch.
    const CretAgent* a = &s->agents[me];
    float want[3]; cam_pivot(a, want);
    if (c->zoom_s <= 0) c->zoom_s = c->zoom;
    c->zoom_s += (c->zoom - c->zoom_s) * (1 - expf(-dt * 10.0f));
    float L = cam_boom(a, c->zoom_s);
    if (!c->target_ok) { memcpy(c->target, want, sizeof want); c->target_ok = true; c->boom = L; }
    float k = 1 - expf(-dt * 12.0f), ky = 1 - expf(-dt * 6.0f);
    c->target[0] += (want[0] - c->target[0]) * k; c->target[2] += (want[2] - c->target[2]) * k; c->target[1] += (want[1] - c->target[1]) * ky;
    float d[3] = {-cosf(c->pitch) * cosf(c->yaw), -sinf(c->pitch), -cosf(c->pitch) * sinf(c->yaw)};
    float clear = cam_clear(s, veg, c->target, d, L);
    c->boom = clear < c->boom ? clear : c->boom + (clear - c->boom) * (1 - expf(-dt * 2.5f));   // in at once, out slowly
    float cx = c->target[0] + d[0] * c->boom, cy = c->target[1] + d[1] * c->boom, cz = c->target[2] + d[2] * c->boom;
    cy = fmaxf(cy, cam_ground(s->world, cx, cz));
    cam->pos[0] = cx; cam->pos[1] = cy; cam->pos[2] = cz;
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
    cret_dyn_lerp(out, alpha);   // movable props (dynprops.c)
}

// ---------------------------------------------------------------- capture shots
typedef struct { const char* name; float pos[3]; float yaw, pitch, fov, tod, clouds; } Shot;

// --shot list: comma-separated names, each optionally a prefix ending in '*'.
static bool shot_selected(const char* list, const char* name) {
    for (const char* p = list; *p;) {
        const char* e = strchr(p, ','); size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n > 0 && (p[n - 1] == '*' ? strncmp(p, name, n - 1) == 0 : (strlen(name) == n && strncmp(p, name, n) == 0))) return true;
        if (!e) break; p = e + 1;
    }
    return false;
}

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
    // River mouth: the largest river cell that touches the sea or a lake,
    // seen from the bank (low) and from above (where the flows meet).
    {
        int bm = -1; float bf = 0;
        for (int z = 4; z < n - 4; z += 2) for (int x = 4; x < n - 4; x += 2) {
            size_t i = (size_t)z * n + x; if (m->water_kind[i] != TERRA_WATER_RIVER || m->flow[i] <= bf) continue;
            int still = 0; for (int q = 0; q < 8 && !still; q++) { int kx = x + (q % 3 - 1) * 3, kz = z + (q / 3 - 1) * 3; uint8_t wk = m->water_kind[(size_t)kz * n + kx]; still = wk == TERRA_WATER_SEA || wk == TERRA_WATER_LAKE; }
            if (still) { bf = m->flow[i]; bm = (int)i; }
        }
        if (bm >= 0) {
            float x = (bm % n) * cell, z = (bm / n) * cell, fx = m->flow_dir[2 * bm] / 127.0f, fz = m->flow_dir[2 * bm + 1] / 127.0f;
            // From out over the open water, looking back up the river (no trees in the way).
            float px = x + fx * 45 + fz * 12, pz = z + fz * 45 - fx * 12, h = fmaxf(cret_height(w, px, pz), cret_water(w, px, pz));
            out[k++] = (Shot){"mouth", {px, h + 6.0f, pz}, look_yaw(px, pz, x - fx * 15, z - fz * 15), -0.12f, 65, 0.42f, 0.4f};
            px = x + fx * 40; pz = z + fz * 40; h = fmaxf(cret_height(w, px, pz), cret_water(w, px, pz));
            out[k++] = (Shot){"mouth_high", {px, h + 40.0f, pz}, look_yaw(px, pz, x, z), -0.62f, 65, 0.42f, 0.4f};
        }
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
        // Same hill: early morning facing the low sun (east, +x), and a
        // moonlit night facing away from the moon, then toward it.
        out[k] = out[k - 2]; out[k].name = "morning"; out[k].yaw = 0.25f; out[k].pitch = -0.04f; out[k].tod = 0.285f; out[k].clouds = 0.45f; k++;
        out[k] = out[k - 3]; out[k].name = "night"; out[k].yaw = PI - 0.3f; out[k].pitch = -0.08f; out[k].tod = 0.86f; out[k].clouds = 0.4f; k++;
        out[k] = out[k - 3]; out[k].name = "rain"; out[k].yaw = PI - 0.6f; out[k].pitch = -0.1f; out[k].tod = 0.45f; out[k].clouds = 0.97f; k++;
        out[k] = out[k - 5]; out[k].name = "night_moon"; out[k].yaw = 0.3f; out[k].pitch = 0.18f; out[k].tod = 0.86f; out[k].clouds = 0.4f; k++;
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
    // Combat feedback (render/fx.c): blows logged at given warm-up frames,
    // seen by agent `me` (-1: a spectator; the HUD overlay is agent 0's).
    int nh, me; struct { int frame, att, vic, kind; float dmg; } hit[3];
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
    // 6: the game's third-person camera (update_camera's rig at zoom 1)
    // behind the player raptor, trotting away from the camera, slightly off axis.
    const CretSpecies* R = &CRET_SPECIES_TABLE[CRET_SP_RAPTOR];
    float py = side + 3.14159f + 0.35f, boom = CAM_BOOM_LENGTHS * R->length, cp = -0.22f;
    float ex = X + cosf(py) * 4.6f * 0.8f, ez = Z + sinf(py) * 4.6f * 0.8f;   // where it ends after the warm-up frames
    sc[k] = (DinoScene){.n = 1, .a = {{CRET_SP_RAPTOR, 0, X, Z, py, 4.6f}}}; k++;
    float pvx = ex + cosf(py) * CAM_PIVOT_FWD * R->length, pvz = ez + sinf(py) * CAM_PIVOT_FWD * R->length;
    float tyy = cret_height(w, ex, ez) + R->height * CAM_PIVOT_UP, ca = py - 0.25f;
    float bx = pvx - cosf(cp) * cosf(ca) * boom, bz = pvz - cosf(cp) * sinf(ca) * boom, by = fmaxf(tyy - sinf(cp) * boom, cam_ground(w, bx, bz));
    setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_tp", {bx, by, bz}, ca, cp, 60, 0.4f, 0.4f};
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
    // 9b: combat feedback. The player's raptor (third-person camera, game
    // HUD) bites a stegosaur's flank: blood, decals, the stegosaur's flinch,
    // the hit marker, its health bar and the attack cooldown. Then the raptor
    // bitten by a tyrannosaur from its right (damage arc, vignette, flinch),
    // and a stegosaur's tail swipe throwing a raptor (trail), from the side.
    {
        const CretSpecies* RS = &CRET_SPECIES_TABLE[CRET_SP_RAPTOR];
        float py2 = side + 3.14159f + 0.25f, sy = py2 + 1.5708f;   // raptor faces away from the camera; the stegosaur crosses its path
        float sx = X + cosf(py2) * 3.4f, sz = Z + sinf(py2) * 3.4f;
        int hf = (int)lroundf(RS->hit_time * 30);
        sc[k] = (DinoScene){.n = 2, .a = {{CRET_SP_RAPTOR, CRET_ACT_ATTACK, X, Z, py2, 0}, {CRET_SP_STEGO, CRET_ACT_EAT, sx, sz, sy, 0}}, .overlay = 1,
                            .frames = hf + 4, .nh = 2, .me = 0, .hit = {{3, 0, 1, CRET_HIT_BITE, 0.05f}, {hf, 0, 1, CRET_HIT_BITE, 0.09f}}}; k++;
        float boom2 = 5.0f, cp2 = -0.2f, tyy = cret_height(w, X, Z) + RS->height * 0.85f, ca = py2 - 0.15f;
        float bx = X - cosf(cp2) * cosf(ca) * boom2, bz = Z - cosf(cp2) * sinf(ca) * boom2, by = tyy - sinf(cp2) * boom2;
        setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_feel_bite", {bx, by, bz}, ca, cp2, 60, 0.4f, 0.4f};
        const CretSpecies* TS = &CRET_SPECIES_TABLE[CRET_SP_TREX];
        float tyaw = py2 + 1.5708f + 3.14159f, tx = X + cosf(py2 - 1.2f) * 6.5f, tz = Z + sinf(py2 - 1.2f) * 6.5f;   // on the raptor's right, facing it
        tyaw = atan2f(Z - tz, X - tx);
        int tf = (int)lroundf(TS->hit_time * 30);
        sc[k] = (DinoScene){.n = 2, .a = {{CRET_SP_RAPTOR, 0, X, Z, py2, 0}, {CRET_SP_TREX, CRET_ACT_ATTACK, tx, tz, tyaw, 0}}, .overlay = 1,
                            .frames = tf + 4, .nh = 1, .me = 0, .hit = {{tf, 1, 0, CRET_HIT_BITE, 0.35f}}}; k++;
        setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_feel_hurt", {bx, by, bz}, ca, cp2, 60, 0.4f, 0.4f};
        const CretSpecies* GS = &CRET_SPECIES_TABLE[CRET_SP_STEGO];
        float gy = side + 1.5708f, rx2 = X - cosf(gy) * GS->length * 0.55f - sinf(gy) * 1.6f, rz2 = Z - sinf(gy) * GS->length * 0.55f + cosf(gy) * 1.6f;
        int gf = (int)lroundf(GS->hit_time * 30);
        sc[k] = (DinoScene){.n = 2, .a = {{CRET_SP_STEGO, CRET_ACT_ATTACK, X, Z, gy, 0}, {CRET_SP_RAPTOR, 0, rx2, rz2, gy, 0}},
                            .frames = gf + 1, .nh = 1, .me = -1, .hit = {{gf, 0, 1, CRET_HIT_TAIL, 0.3f}}}; k++;
        float mx = X - cosf(gy) * GS->length * 0.4f, mz = Z - sinf(gy) * GS->length * 0.4f;
        cx = mx + cosf(side) * 9.0f; cz = mz + sinf(side) * 9.0f; cy = cret_height(w, cx, cz) + 2.2f;
        setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_feel_tail", {cx, cy, cz}, look_yaw(cx, cz, mx, mz), atan2f(cret_height(w, mx, mz) + 1.2f - cy, 9.0f), 55, 0.4f, 0.4f};
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
        // Two packmates: one alongside, one further off.
        hs->a[4] = (typeof(hs->a[0])){CRET_SP_RAPTOR, 0, X + cosf(py2 - 0.6f) * 6, Z + sinf(py2 - 0.6f) * 6, py2 + 0.2f, 0};
        hs->a[5] = (typeof(hs->a[0])){CRET_SP_RAPTOR, 0, X + cosf(py2 + 0.9f) * 34, Z + sinf(py2 + 0.9f) * 34, py2 - 0.4f, 0};
        hs->n = 6;
        k++;
        float tyy = cret_height(w, X, Z) + CRET_SPECIES_TABLE[CRET_SP_RAPTOR].height * 0.8f, ca = py2 - 0.1f;
        float bx = X - cosf(cp2) * cosf(ca) * boom2, bz = Z - cosf(cp2) * sinf(ca) * boom2, by = tyy - sinf(cp2) * boom2;
        setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_hud", {bx, by, bz}, look_yaw(bx, bz, X, Z), atan2f(tyy - by, boom2 * cosf(cp2)), 60, 0.4f, 0.4f};
        setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_map", {bx, by, bz}, look_yaw(bx, bz, X, Z), atan2f(tyy - by, boom2 * cosf(cp2)), 60, 0.4f, 0.4f};
    }
    // 12: the credits page over the herd shot.
    {
        sc[k] = sc[4]; sc[k].overlay = 2; k++;
        Shot herd = shots[*ns - 7]; herd.name = "dino_credits"; setup[*ns] = k; shots[(*ns)++] = herd;
    }
    // 13..: every species standing, running and attacking (dino_<asset>_idle/_run/_attack),
    // then a raptor pack closing on a pachycephalosaur from three sides.
    for (int sp = 0; sp < CRET_SPECIES && k + 3 < 40; sp++) {
        const CretSpecies* S = &CRET_SPECIES_TABLE[sp]; float L = S->length;
        float d = L * 1.5f + 3.0f, eye = 1.4f + 0.12f * L, yw = side + 0.9f;
        static const char* MODE[3] = {"idle", "run", "attack"};
        for (int mo = 0; mo < 3; mo++) {
            float spd = mo == 1 ? S->trot : 0, ry2 = mo == 1 ? side + 1.5708f : yw;
            float sx = X - cosf(ry2) * spd * 0.8f, sz = Z - sinf(ry2) * spd * 0.8f;   // ends at X, Z after the warm-up
            sc[k] = (DinoScene){.n = 1, .a = {{sp, mo == 2 ? CRET_ACT_ATTACK : 0, sx, sz, ry2, spd}}, .frames = mo == 2 ? (int)(S->hit_time * 30) : 24};
            k++;
            cx = X + cosf(side) * d; cz = Z + sinf(side) * d; cy = cret_height(w, cx, cz) + eye;
            char* nm = malloc(48); snprintf(nm, 48, "dino_sp_%s_%s", S->asset, MODE[mo]);
            setup[*ns] = k; shots[(*ns)++] = (Shot){nm, {cx, cy, cz}, look_yaw(cx, cz, X, Z), atan2f(cret_height(w, X, Z) + S->height * 0.5f - cy, d), 50, 0.4f, 0.4f};
        }
    }
    {
        float run = side + 1.5708f;   // the pachycephalosaur flees across the frame
        DinoScene* ph = &sc[k]; memset(ph, 0, sizeof *ph); ph->n = 4;
        ph->a[0] = (typeof(ph->a[0])){CRET_SP_PACHY, 0, X - cosf(run) * 6, Z - sinf(run) * 6, run, 7.5f};
        float off[3] = {-0.55f, 0.0f, 0.6f};
        for (int q = 0; q < 3; q++) {
            float ang = run + 3.14159f + off[q], r = 9 + 3 * q;
            float rx = X - cosf(run) * 6 + cosf(ang) * r, rz = Z - sinf(run) * 6 + sinf(ang) * r;
            ph->a[q + 1] = (typeof(ph->a[0])){CRET_SP_RAPTOR, 0, rx, rz, run - off[q] * 0.4f, 9.0f};
        }
        k++;
        float fx = X - cosf(run) * 3, fz = Z - sinf(run) * 3;   // frame the gap between prey and pack
        cx = fx + cosf(side) * 14.0f; cz = fz + sinf(side) * 14.0f; cy = cret_height(w, cx, cz) + 2.2f;
        setup[*ns] = k; shots[(*ns)++] = (Shot){"dino_pack_hunt", {cx, cy, cz}, look_yaw(cx, cz, fx, fz), atan2f(cret_height(w, fx, fz) + 1.0f - cy, 14.0f), 60, 0.4f, 0.4f};
    }
    fprintf(stderr, "capture: dino scenes at %.0f %.0f (canopy score %.0f)\n", X, Z, bd);
    return k;
}

static int run_capture(CretSim* sim, const char* dir, const char* only, int W, int H) {
    mkdir(dir, 0755);
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(W, H, "cretaceous capture"); SetTraceLogLevel(LOG_WARNING);
    CRET_LOAD_MARK("window and GL context");
    CretView view; if (!cret_view_init(&view, sim->world, W, H)) return 1;
    CretHud hud; cret_hud_init(&hud); CRET_LOAD_MARK("hud fonts");
    CretFx* fx = calloc(1, sizeof(CretFx)); cret_fx_init(fx, sim->world, view.dinos_ok ? &view.dinos : NULL);
    view.fx_draw = cret_fx_draw; view.fx_ctx = fx;
    const CretFxSettings cap_fxs = {1, 1};
    Shot shots[96]; int ns = pick_shots(sim->world, shots);
    // Creek water: a steep creek reported in play (3783, 1849), aimed at its
    // steepest nearby cell and at the spot where it meets slower water, then
    // the biggest steep creek on the island seen from its bank.
    {
        const TerraMap* m = &sim->world->map; int n = m->n; float cell = m->cell;
        #define CREEK_SLOPE(i) (sqrtf(powf(m->height[(i) + 1] - m->height[i], 2) + powf(m->height[(i) + n] - m->height[i], 2)) / cell)
        float px = 3783, pz = 1849, py = fmaxf(cret_height(sim->world, px, pz), cret_water(sim->world, px, pz)) + 2.4f;
        int fast = -1, meet = -1; float bf = 0, bm = 0;
        for (int z = (int)(pz / cell) - 10; z <= (int)(pz / cell) + 10; z++) for (int x = (int)(px / cell) - 10; x <= (int)(px / cell) + 10; x++) {
            if (x < 3 || z < 3 || x >= n - 3 || z >= n - 3) continue;
            size_t i = (size_t)z * n + x; if (m->water_kind[i] == TERRA_WATER_NONE) continue;
            float s = CREEK_SLOPE(i), d = 0;
            for (int k = 0; k < 4; k++) { size_t j = i + (k == 0 ? 2 : k == 1 ? -2 : k == 2 ? 2 * (size_t)n : -2 * (size_t)n); if (m->water_kind[j] != TERRA_WATER_NONE) d = fmaxf(d, fabsf(CREEK_SLOPE(j) - s)); }
            if (s > bf) { bf = s; fast = (int)i; }
            if (d > bm) { bm = d; meet = (int)i; }
        }
        shots[ns++] = (Shot){"creek_felix", {px, py, pz}, 2.09f, -0.25f, 65, 0.666f, 0.35f};
        int aim[2] = {fast, meet}; static const char* AN[2] = {"creek_fast", "creek_meet"};
        for (int a = 0; a < 2; a++) if (aim[a] >= 0) {
            float tx = (aim[a] % n) * cell, tz = (aim[a] / n) * cell, ty = cret_water(sim->world, tx, tz), dd = fmaxf(hypotf(tx - px, tz - pz), 3.0f);
            fprintf(stderr, "capture: %s aims at %.0f %.0f (slope %.2f, jump %.2f)\n", AN[a], tx, tz, a ? bm : bf, bm);
            shots[ns++] = (Shot){AN[a], {px, py, pz}, look_yaw(px, pz, tx, tz), atan2f(ty - py, dd), 65, a ? 0.495f : 0.666f, 0.35f};
        }
        int st = -1; float bs = 0, fmx = 0;
        for (size_t i = 0; i < (size_t)n * n; i++) if (m->water_kind[i] == TERRA_WATER_RIVER) fmx = fmaxf(fmx, m->flow[i]);
        for (int z = n / 8; z < n - n / 8; z += 2) for (int x = n / 8; x < n - n / 8; x += 2) {
            size_t i = (size_t)z * n + x; if (m->water_kind[i] != TERRA_WATER_RIVER || m->flow[i] < 0.03f * fmx) continue;
            float s = CREEK_SLOPE(i); if (s > 0.12f && s < 0.4f && m->flow[i] > bs) { bs = m->flow[i]; st = (int)i; }   // the biggest steep creek (not a cliff fall)
        }
        if (st >= 0) {
            float x = (st % n) * cell, z = (st / n) * cell, fx = m->flow_dir[2 * st] / 127.0f, fz = m->flow_dir[2 * st + 1] / 127.0f;
            float cx0 = x - fz * 14 - fx * 10, cz0 = z + fx * 14 - fz * 10, cy0 = fmaxf(cret_height(sim->world, cx0, cz0), cret_water(sim->world, cx0, cz0)) + 2.5f;
            fprintf(stderr, "capture: creek_steep at %.0f %.0f (slope %.2f)\n", x, z, CREEK_SLOPE((size_t)st));
            shots[ns++] = (Shot){"creek_steep", {cx0, cy0, cz0}, look_yaw(cx0, cz0, x, z), atan2f(cret_water(sim->world, x, z) - cy0, 17.0f), 65, 0.6f, 0.35f};
        }
        #undef CREEK_SLOPE
        // A tall-tree stand reported in play (5627, 1972), facing 162 degrees.
        float tx = 5627, tz = 1972; shots[ns++] = (Shot){"tree_felix", {tx, cret_height(sim->world, tx, tz) + 3.0f, tz}, 1.257f, 0.12f, 65, 0.572f, 0.4f};
    }
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
                        if (k == VEG_CONIFER || k == VEG_PALM) {   // the crown from below, beside the trunk
                            static char up[16][24]; snprintf(up[e], sizeof up[e], "%s_up", SP[e].name);
                            float ux = in->x + (cxp - in->x) * 0.18f, uz = in->z + (czp - in->z) * 0.18f, uy = cret_height(sim->world, ux, uz) + 1.6f;
                            shots[ns++] = (Shot){up[e], {ux, uy, uz}, look_yaw(ux, uz, in->x, in->z), atan2f(in->y + h * 0.9f - uy, hypotf(in->x - ux, in->z - uz)), 70, 0.4f, 0.4f};
                        }
                        if (k == VEG_CONIFER && SP[e].variant == 2)   // the umbrella crown level from the side: full mesh, then its LOD
                            for (int q = 0; q < 2; q++) {
                                static const char* SN[2] = {"sv_umbrella_side", "sv_umbrella_far"};
                                float dd = h * (q ? 4.2f : 1.8f), sx = in->x + (cxp - in->x) / d * dd, sz = in->z + (czp - in->z) / d * dd, sy = in->y + h * 0.72f;
                                shots[ns++] = (Shot){SN[q], {sx, sy, sz}, look_yaw(sx, sz, in->x, in->z), atan2f(in->y + h * 0.78f - sy, dd), q ? 30 : 50, 0.4f, 0.4f};
                            }
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
    DinoScene scenes[40]; int nscene = 0, setup[96] = {0};
    if (ns <= 52) nscene = dino_shots(sim, shots, &ns, scenes, setup);
    else fprintf(stderr, "capture: no room for dino shots (%d shots)\n", ns);
    for (int i = 0; i < sim->cfg.num_agents; i++) sim->agents[i].alive = false;
    // CRET_SHOT_VARIANTS="noon:0.5:0.3,gold:0.71:0.35" renders every selected
    // shot once per tag:time_of_day:cloud_cover, written as NAME_TAG.png.
    const char* tag[96] = {0};
    if (getenv("CRET_SHOT_VARIANTS")) {
        Shot vs[96]; int vset[96], nv = 0; const char* vtag[96];
        for (int s = 0; s < ns; s++) {
            if (only && !shot_selected(only, shots[s].name)) continue;
            for (const char* p = getenv("CRET_SHOT_VARIANTS"); *p && nv < 96;) {
                char t[32] = {0}; float tod, cl; int len = 0;
                if (sscanf(p, "%31[^:]:%f:%f%n", t, &tod, &cl, &len) < 3) break;
                vs[nv] = shots[s]; vs[nv].tod = tod; vs[nv].clouds = cl; vset[nv] = setup[s]; vtag[nv++] = strdup(t);
                p += len; if (*p == ',') p++;
            }
        }
        memcpy(shots, vs, sizeof(Shot) * nv); memcpy(setup, vset, sizeof(int) * nv); memcpy(tag, vtag, sizeof(char*) * nv); ns = nv; only = NULL;
    }
    for (int s = 0; s < ns; s++) {
        // --shot NAME[,NAME...], each a name or a prefix ending in '*' (e.g. 'dino_*').
        if (only && !shot_selected(only, shots[s].name)) continue;
        sim->time_of_day = shots[s].tod; view.settings.cloud_coverage = shots[s].clouds;
        for (int i = 0; i < sim->cfg.num_agents; i++) sim->agents[i].alive = false;
        if (setup[s] > 0 && setup[s] <= nscene) place_scene(sim, &scenes[setup[s] - 1]);
        else memset(sim->carcasses, 0, sizeof sim->carcasses);
        hud.map_big = !strcmp(shots[s].name, "dino_map");
        if (!strcmp(shots[s].name, "dino_hud") || hud.map_big) {   // a young raptor partway through a hard day
            CretAgent* a = &sim->agents[0]; a->growth = 0.64f; cret_apply_growth(a);
            a->health = a->max_health * 0.72f; a->bleed = a->max_health * 0.01f; a->stamina = 0.58f; a->food = 0.46f; a->water = 0.81f;
            sim->agents[2].health = sim->agents[2].max_health * 0.55f; sim->agents[5].health = sim->agents[5].max_health * 0.4f;
            // Its pack: every raptor in the scene, one with a broken leg; a packmate called from up the valley.
            for (int i = 0; i < sim->cfg.num_agents; i++) if (sim->agents[i].alive && sim->agents[i].species == a->species) sim->agents[i].group = 1;
            a->broken = 40; a->rally_ticks = 600; a->rally_x = a->x + cosf(a->yaw + 0.5f) * 60; a->rally_z = a->z + sinf(a->yaw + 0.5f) * 60;
        }
        CretCamera cam = {{shots[s].pos[0], shots[s].pos[1], shots[s].pos[2]}, shots[s].yaw, shots[s].pitch, shots[s].fov};
        // Each still starts fresh: exposure, cloud history and wetness snap (view.c treats a jump as a teleport).
        view.adapt_valid = false; view.cam_pos[1] += 1e4f;
        // CRET_CAPTURE_FRAMES=N renders N extra frames and prints the mean frame time (vsync off in hidden windows).
        int extra = getenv("CRET_CAPTURE_FRAMES") ? atoi(getenv("CRET_CAPTURE_FRAMES")) : 0; double t0 = 0;
        int warm = setup[s] > 0 && setup[s] <= nscene && scenes[setup[s] - 1].frames > 0 ? scenes[setup[s] - 1].frames : 24;
        const DinoScene* scn = setup[s] > 0 && setup[s] <= nscene ? &scenes[setup[s] - 1] : NULL;
        int fx_me = scn && scn->nh ? scn->me : -1;
        cret_fx_clear(fx); cret_fx_update(fx, sim, sim, fx_me, &cap_fxs, NULL, 0);
        hud.fb = scn && scn->nh ? &fx->hud : NULL;
        for (int f = 0; f < warm + extra; f++) {
            if (f == warm) { glFinish(); t0 = GetTime(); gprof_reset(); }
            if (setup[s]) advance_scene(sim, 1.0f / 30);
            for (int q = 0; scn && q < scn->nh; q++) if (scn->hit[q].frame == f) {   // a blow lands (as the sim's damage() logs it)
                CretAgent* v = &sim->agents[scn->hit[q].vic];
                cret_log_hit(sim, scn->hit[q].att, scn->hit[q].vic, scn->hit[q].kind, scn->hit[q].dmg);
                v->health = fmaxf(v->health - scn->hit[q].dmg * v->max_health, 1); v->hurt_ticks = 12; v->bleed = 0.012f * v->max_health;
                sim->agents[scn->hit[q].att].attack_cd = (int)(CRET_SPECIES_TABLE[sim->agents[scn->hit[q].att].species].cooldown * CRET_TICK_HZ * 0.8f);
            }
            { CretCamera cc = cam; cret_fx_update(fx, sim, sim, fx_me, &cap_fxs, NULL, 1.0f / 30); cret_fx_camera(fx, &cc); }
            BeginDrawing(); cret_view_render(&view, sim, &cam, 1.0f / 30);
            int ov = setup[s] > 0 && setup[s] <= nscene ? scenes[setup[s] - 1].overlay : 0;
            if (ov == 1) cret_hud_draw(&hud, sim, 0, view.viewproj, cam.yaw);
            else if (ov == 2) cret_hud_draw_credits(&hud);
            EndDrawing();
        }
        if (extra > 0) { glFinish(); printf("frame %s%s%s: %.2f ms\n", shots[s].name, tag[s] ? "_" : "", tag[s] ? tag[s] : "", (GetTime() - t0) * 1000.0 / extra); gprof_report(shots[s].name); }
        Image im = LoadImageFromScreen(); char path[1024]; snprintf(path, sizeof path, "%s/%s%s%s.png", dir, shots[s].name, tag[s] ? "_" : "", tag[s] ? tag[s] : "");
        ExportImage(im, path); UnloadImage(im);
        printf("wrote %s  (cam %.0f %.0f %.0f)\n", path, cam.pos[0], cam.pos[1], cam.pos[2]);
        if (scn && scn->nh) {   // combat feedback state in the still
            int tr = 0; for (int i = 0; i < CRET_MAX_AGENTS; i++) tr += fx->trail[i].n;
            printf("  fx: %d hits, %d droplets, %d decals, %d trail samples, marker %.2f s, vignette %.2f\n", fx->stats_hits, fx->npart, fx->ndecal, tr, fx->hud.marker_t, fx->hud.vignette);
            if (getenv("CRET_FX_DEBUG")) for (int q = 0; q < 2; q++) {   // effect joints against the animal (fx.c)
                const CretAgent* ag = &sim->agents[q]; float jp[3][3]; bool ok[3];
                for (int w = 0; w < 3; w++) ok[w] = view.dinos_ok && cret_dino_joint(&view.dinos, q, w, jp[w]);
                printf("  agent %d at %.1f %.1f %.1f yaw %.2f: head %d %.1f %.1f %.1f  mid %d %.1f %.1f %.1f  tip %d %.1f %.1f %.1f\n", q, ag->x, ag->y, ag->z, ag->yaw,
                       ok[0], jp[0][0], jp[0][1], jp[0][2], ok[1], jp[1][0], jp[1][1], jp[1][2], ok[2], jp[2][0], jp[2][1], jp[2][2]);
            }
        }
    }
    cret_fx_close(fx); free(fx);
    cret_hud_close(&hud); cret_view_close(&view); CloseWindow(); return 0;
}

// ---------------------------------------------------------------- settings and title
static void apply_settings(const CretSettings* st, CretView* view, CretAudio* au, Control* ctl, CretHud* hud, bool window) {
    // Environment overrides (CRET_PRESET, CRET_RENDER_SCALE) win over the saved settings.
    if (!getenv("CRET_PRESET")) {
        cret_view_set_preset(view, st->preset);
        if (!getenv("CRET_RENDER_SCALE")) view->settings.render_scale = st->render_scale;
    }
    for (int b = 0; b < CRET_AUDIO_BUSES; b++) cret_audio_set_volume(au, b, st->volume[b]);
    ctl->sens = st->mouse_sens; ctl->invert_y = st->invert_y;
    hud->ui_scale = st->ui_scale;
    g_fxs.shake = st->shake; g_fxs.hitstop = st->hitstop;
    if (!window) return;
    if (st->fullscreen != IsWindowFullscreen()) {
        if (st->fullscreen) { int m = GetCurrentMonitor(); SetWindowSize(GetMonitorWidth(m), GetMonitorHeight(m)); }
        ToggleFullscreen();
    }
    if (!st->fullscreen) {
        int w, h; cret_settings_resolution(st->res, &w, &h);
        if (GetScreenWidth() != w || GetScreenHeight() != h) SetWindowSize(w, h);
    }
    cret_view_resize(view, GetRenderWidth(), GetRenderHeight());
}

// Title background: the review viewpoints (pick_shots) as slow dolly moves,
// each faded in and out, with the bots living their lives in the world.
typedef struct { Shot shot[24]; int n, cur; float t; } Flyover;
#define FLY_SECONDS 13.0f
static void flyover_init(Flyover* f, const CretWorld* w) {
    Shot all[24]; int n = pick_shots(w, all); f->n = 0; f->cur = 0; f->t = 0;
    static const char* keep[] = {"ridge", "coast", "morning", "river", "aerial", "lake", "sunset", "dusk"};
    for (int k = 0; k < 8; k++) for (int i = 0; i < n; i++) if (!strcmp(all[i].name, keep[k]) && f->n < 24) f->shot[f->n++] = all[i];
}
// Camera for this frame; returns the black fade (0 clear .. 1 black).
static float flyover_camera(Flyover* f, CretSim* sim, CretView* view, CretCamera* cam, float dt) {
    if (f->n == 0) return 0;
    f->t += dt;
    if (f->t >= FLY_SECONDS) { f->t -= FLY_SECONDS; f->cur = (f->cur + 1) % f->n; }
    const Shot* s = &f->shot[f->cur]; float u = f->t / FLY_SECONDS;
    bool high = s->pos[1] - cret_height(sim->world, s->pos[0], s->pos[2]) > 300;
    float speed = high ? 22.0f : 2.2f, fx = cosf(s->yaw), fz = sinf(s->yaw);
    float x = s->pos[0] + fx * speed * f->t, z = s->pos[2] + fz * speed * f->t;
    float y = fmaxf(s->pos[1] + (high ? 0 : 0.6f * u), fmaxf(cret_height(sim->world, x, z), cret_water(sim->world, x, z)) + 1.5f);
    *cam = (CretCamera){{x, y, z}, s->yaw + (u - 0.5f) * 0.12f, s->pitch + (high ? 0 : 0.02f * (u - 0.5f)), s->fov};
    sim->time_of_day = s->tod; view->settings.cloud_coverage = s->clouds;
    float in = clampf(f->t / 1.4f, 0, 1), out = clampf((FLY_SECONDS - f->t) / 1.4f, 0, 1);
    return 1 - fminf(in, out);
}

// ---------------------------------------------------------------- characters
// The local player's character: loaded on Play / Host, saved every minute,
// on death, on a species change and on quit (character.c).
typedef struct { bool on; CretCharacter c; int quests_done, marks; bool alive; int save_tick; } Persona;

static void persona_start(Persona* p, CretSim* s, int me, const char* name, int species, CretHud* h) {
    memset(p, 0, sizeof *p);
    if (!name || !name[0]) return;
    p->on = true;
    if (!cret_character_load(name, &p->c)) cret_character_new(&p->c, name, species, s->cfg.player_growth);
    if (species >= 0 && species != p->c.species) { p->c.species = species; p->c.growth = s->cfg.player_growth; p->c.has_pos = false; }   // another species: a new life
    cret_character_apply(s, me, &p->c);
    snprintf(h->character, sizeof h->character, "%s", p->c.name);
    p->quests_done = s->prog[me].quests_done; p->marks = s->prog[me].marks; p->alive = true; p->save_tick = s->tick;
    cret_character_save(&p->c);
}
static void persona_save(Persona* p, const CretSim* s, int me) {
    if (!p->on) return;
    cret_character_capture(s, me, s->cfg.player_growth, &p->c); cret_character_save(&p->c); p->save_tick = s->tick;
}
// Every frame: notes for finished quests, saves on death and each minute.
static void persona_update(Persona* p, const CretSim* s, int me, CretHud* h, bool saves) {
    const CretProgress* pr = &s->prog[me];
    if (pr->quests_done > p->quests_done) {
        char msg[96]; snprintf(msg, sizeof msg, "Quest complete  -  %d marks", pr->marks);
        if (pr->marks > p->marks) snprintf(msg, sizeof msg, "Quest complete  +%d marks", pr->marks - p->marks);
        cret_hud_note(h, msg);
    }
    p->quests_done = pr->quests_done; p->marks = pr->marks;
    bool alive = s->agents[me].alive;
    if (saves && p->on && ((p->alive && !alive) || s->tick - p->save_tick >= 60 * CRET_TICK_HZ)) persona_save(p, s, me);
    p->alive = alive;
}
// Server side: clients' characters by name (MSG_PROFILE sets load_pending),
// saved each minute and when they leave; a freed slot goes back to a bot.
static void server_characters(CretSim* s, uint8_t* was_human, int first) {
    for (int i = first; i < s->cfg.num_agents; i++) {
        CretProgress* p = &s->prog[i];
        if (s->human[i] && p->load_pending) {
            CretCharacter c; char name[24]; memcpy(name, p->name, sizeof name);
            if (!cret_character_load(name, &c)) cret_character_new(&c, name, s->agents[i].species, s->cfg.player_growth);
            p->load_pending = 0; cret_character_apply(s, i, &c);
            fprintf(stderr, "cretaceous: slot %d is %s (%s, growth %.0f%%, %d marks)\n", i, c.name, CRET_SPECIES_TABLE[c.species].name, c.growth * 100, c.marks);
        }
        bool save = (s->human[i] && p->name[0] && s->tick % (60 * CRET_TICK_HZ) == 0) || (was_human[i] && !s->human[i] && p->name[0]);
        if (save) { CretCharacter c; memset(&c, 0, sizeof c); memcpy(c.name, p->name, sizeof c.name); cret_character_capture(s, i, s->cfg.player_growth, &c); cret_character_save(&c); }
        if (was_human[i] && !s->human[i]) { p->name[0] = 0; cret_prog_reset(s, i, false); }
        was_human[i] = s->human[i];
    }
}

// ---------------------------------------------------------------- main
typedef struct { CretSim* sim; CretConfig cfg; int rc; } SimInit;
static void* sim_init_thread(void* p) { SimInit* s = p; s->rc = cret_sim_init(s->sim, s->cfg); return NULL; }

int main(int argc, char** argv) {
    // WSL: Mesa defaults to llvmpipe (CPU). /dev/dxg means the d3d12 GPU path works.
    cret_load_seconds();   // load profiling clock (CRET_PROFILE_LOAD=1)
#ifndef _WIN32
    struct stat st; if (stat("/dev/dxg", &st) == 0) setenv("GALLIUM_DRIVER", "d3d12", 0);
#endif
    Mode mode = MODE_PLAY; const char* addr = NULL; const char* dir = "."; const char* only = NULL; const char* path = NULL; int frames = 0, fps = 30;
    int port = CRET_DEFAULT_PORT, W = 1600, H = 900, want_species = -1;
    CretConfig cfg = cret_default_config(); cfg.seed = (uint32_t)time(NULL);
    // No mode word: the title screen. Test switches: --bot (slot 0 plays
    // itself), --quit-after N frames, --screenshot PATH (last frame), --hidden
    // window, --settings-page (open the title's settings page).
    bool title = true, bot_player = false, hidden = false, size_set = false, settings_page = false; int quit_after = 0; const char* shot_path = NULL;
    const char* char_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bot")) { bot_player = true; continue; }
        if (!strcmp(argv[i], "--hidden")) { hidden = true; continue; }
        if (!strcmp(argv[i], "--settings-page")) { settings_page = true; continue; }
        if (!strcmp(argv[i], "--quit-after") && i + 1 < argc) { quit_after = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) { shot_path = argv[++i]; continue; }
        if (!strcmp(argv[i], "play") || !strcmp(argv[i], "host") || !strcmp(argv[i], "server") || !strcmp(argv[i], "join") || !strcmp(argv[i], "capture")) title = false;
        if (!strcmp(argv[i], "--size")) size_set = true;
        if (!strcmp(argv[i], "play")) mode = MODE_PLAY;
        else if (!strcmp(argv[i], "host")) mode = MODE_HOST;
        else if (!strcmp(argv[i], "server")) mode = MODE_SERVER;
        else if (!strcmp(argv[i], "join") && i + 1 < argc) { mode = MODE_JOIN; addr = argv[++i]; }
        else if (!strcmp(argv[i], "capture") && i + 1 < argc) { mode = MODE_CAPTURE; dir = argv[++i]; }
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--path") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) cfg.world_seed = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) cfg.world_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--agents") && i + 1 < argc) cfg.num_agents = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--species") && i + 1 < argc) {
            const char* sp = argv[++i]; cfg.player_species = -1;
            for (int k = 0; k < CRET_SPECIES; k++) if (!strcmp(sp, CRET_SPECIES_TABLE[k].asset)) cfg.player_species = k;
            if (cfg.player_species < 0) { fprintf(stderr, "unknown species %s (raptor, stego, trex, pachy, sauropelta, galli)\n", sp); return 2; }
            want_species = cfg.player_species;   // a client asks the server for it (the welcome replaces cfg)
        }
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--netsim") && i + 1 < argc) setenv("CRET_NETSIM", argv[++i], 1);
        else if (!strcmp(argv[i], "--character") && i + 1 < argc) char_name = argv[++i];
        else if (!strcmp(argv[i], "--day-minutes") && i + 1 < argc) cfg.day_seconds = 60.0f * (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--clouds") && i + 1 < argc) { cfg.cloud_cover = clampf((float)atof(argv[++i]), 0, 1); cfg.weather = 0; }
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) { W = atoi(argv[++i]); H = atoi(argv[++i]); }
        else { fprintf(stderr, "usage: see the header of ocean/cretaceous/cretaceous.c\n"); return 2; }
    }
    CretNet* net = NULL; int me = 0;
    if (mode == MODE_JOIN) {
        net = cret_net_join(addr, port, 5.0f); if (!net) return 1;
        cret_net_client_welcomed(net, &cfg, &me);
        if (char_name) cret_net_client_profile(net, char_name);
    }
    CretSim* sim = calloc(1, sizeof(CretSim));
    // Windowed modes load the world on a thread while the window and GL
    // context come up (the view needs the world only after that).
    bool windowed = mode == MODE_PLAY || mode == MODE_HOST || mode == MODE_JOIN;
    SimInit si = {sim, cfg, 0}; pthread_t sim_thread;
    bool sim_async = windowed && pthread_create(&sim_thread, NULL, sim_init_thread, &si) == 0;
    if (!sim_async) {
        if (cret_sim_init(sim, cfg) != 0) { fprintf(stderr, "cretaceous: world failed\n"); return 1; }
        cret_load_mark("sim: world, colliders, agents");
    }
    if (mode == MODE_CAPTURE && path) { int r = cret_capture_paths(sim, dir, path, W, H, frames, fps); cret_sim_close(sim); free(sim); return r; }
    if (mode == MODE_CAPTURE) { int r = run_capture(sim, dir, only, W, H); cret_sim_close(sim); free(sim); return r; }
    if (mode == MODE_HOST || mode == MODE_SERVER) net = cret_net_host(port, mode == MODE_HOST ? 1 : 0);
    if ((mode == MODE_HOST || mode == MODE_SERVER) && !net) return 1;
    CretInput inputs[CRET_MAX_AGENTS] = {0}; bool remote[CRET_MAX_AGENTS] = {0}; uint8_t was_human[CRET_MAX_AGENTS] = {0};
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
            server_characters(sim, was_human, 0);
            cret_sim_step(sim, inputs);
            if (sim->tick % CRET_SNAPSHOT_DIV == 0) cret_net_server_broadcast(net, sim);
        }
    }
    CretSettings settings; cret_settings_load(&settings);
    if (!size_set) cret_settings_resolution(settings.res, &W, &H);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | (hidden ? FLAG_WINDOW_HIDDEN : 0));
    InitWindow(W, H, "Cretaceous");
    SetTraceLogLevel(LOG_WARNING); CRET_LOAD_MARK(sim_async ? "window and GL context (world loading)" : "window and GL context");
    if (sim_async) {
        pthread_join(sim_thread, NULL);
        if (si.rc != 0) { fprintf(stderr, "cretaceous: world failed\n"); return 1; }
        cret_load_mark("sim: world, colliders, agents (rest)");
    }
    SetExitKey(KEY_NULL);   // Esc opens the menu
    CretView view; if (!cret_view_init(&view, sim->world, GetRenderWidth(), GetRenderHeight())) return 1;
    CretHud hud; cret_hud_init(&hud); CRET_LOAD_MARK("hud fonts");   // clients respawn through requests to the server
    CretAudio* audio = cret_audio_init(); CRET_LOAD_MARK("audio");
    CretFx* fx = calloc(1, sizeof(CretFx));   // blood, flinches, hit-stop, shake, feedback HUD
    cret_fx_init(fx, sim->world, view.dinos_ok ? &view.dinos : NULL);
    view.fx_draw = cret_fx_draw; view.fx_ctx = fx; hud.fb = &fx->hud;
    Control ctl = {.yaw = sim->agents[me].yaw, .pitch = -0.15f, .zoom = 1}; mouse_init(&ctl.mouse);
    apply_settings(&settings, &view, audio, &ctl, &hud, !hidden && !size_set);
    CretCamera cam = {0};
    double acc = 0; bool show_help = true; float time_scale = 1;
    CretSim* draw = calloc(1, sizeof(CretSim)); CretAgent prev[CRET_MAX_AGENTS];
    bool quit = false; int frame_no = 0;
    CretMenu menu; cret_menu_init(&menu, sim->agents[0].species); cret_menu_characters(&menu, settings.character);
    Persona persona; memset(&persona, 0, sizeof persona);
    if (settings_page) menu.page = 1;
    // Title: the world flies by under the menu until Play, Host or Join.
    if (title) {
        Flyover fly; flyover_init(&fly, sim->world);
        float tod0 = sim->time_of_day, cloud0 = view.settings.cloud_coverage;
        EnableCursor();
        CretMenuAction go = MENU_NONE;
        while (!WindowShouldClose() && go == MENU_NONE) {
            float dt = GetFrameTime(); if (dt > 0.1f) dt = 0.1f;
            if (IsWindowResized()) cret_view_resize(&view, GetRenderWidth(), GetRenderHeight());
            acc += dt;
            while (acc >= CRET_DT) { acc -= CRET_DT; for (int i = 0; i < sim->cfg.num_agents; i++) inputs[i] = cret_bot_input(sim, i); cret_sim_step(sim, inputs); }
            float fade = flyover_camera(&fly, sim, &view, &cam, dt);
            cret_audio_update(audio, sim, -1, cam.pos, cam.pos, cam.yaw, view.settings.cloud_coverage, false, dt);
            BeginDrawing();
            cret_view_render(&view, sim, &cam, dt);
            if (fade > 0.001f) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){0, 0, 0, (unsigned char)(255 * fade)});
            CretMenuAction a = cret_menu_title(&menu, &hud, audio, &settings, dt);
            if (a == MENU_APPLY) apply_settings(&settings, &view, audio, &ctl, &hud, !hidden);
            else if (a == MENU_QUIT) quit = true;
            else if (a == MENU_JOIN) {
                // Join: a new sim with the server's world (the same world stays loaded when it matches).
                CretNet* jn = cret_net_join(menu.address, port, 5.0f); CretConfig jc = cfg; int slot = 0;
                if (!jn || !cret_net_client_welcomed(jn, &jc, &slot)) { snprintf(menu.note, sizeof menu.note, "Could not join %s:%d", menu.address, port); if (jn) cret_net_close(jn); }
                else {
                    CretSim* ns = calloc(1, sizeof(CretSim));
                    if (cret_sim_init(ns, jc) != 0) { free(ns); cret_net_close(jn); snprintf(menu.note, sizeof menu.note, "That server's world failed to load"); }
                    else {
                        if (ns->world != sim->world) {
                            cret_view_close(&view); cret_view_init(&view, ns->world, GetRenderWidth(), GetRenderHeight());
                            cret_hud_close(&hud); cret_hud_init(&hud);
                            apply_settings(&settings, &view, audio, &ctl, &hud, false);
                        }
                        cret_sim_close(sim); free(sim); sim = ns; net = jn; me = slot; mode = MODE_JOIN; go = a;
                        want_species = menu.species;   // the species picked on the title
                        if (menu.character[0]) { cret_net_client_profile(net, menu.character); snprintf(hud.character, sizeof hud.character, "%s", menu.character); }
                    }
                }
            }
            else if (a != MENU_NONE) go = a;
            if (quit_after && ++frame_no >= quit_after) { if (shot_path) { Image im = LoadImageFromScreen(); ExportImage(im, shot_path); UnloadImage(im); } quit = true; }
            EndDrawing();
            static bool first_title = true; if (first_title) { first_title = false; CRET_LOAD_MARK("first title frame"); }
            if (quit) break;
        }
        if (WindowShouldClose()) quit = true;
        sim->time_of_day = tod0; view.settings.cloud_coverage = cloud0;
        if (go == MENU_PLAY || go == MENU_HOST) {
            if (go == MENU_HOST) {
                net = cret_net_host(port, 1);
                if (net) mode = MODE_HOST; else fprintf(stderr, "cretaceous: could not host on port %d; playing solo\n", port);
            }
            cret_sim_set_species(sim, me, menu.species);
            persona_start(&persona, sim, me, menu.character, menu.species, &hud);
        }
        if (menu.character[0] && strcmp(settings.character, menu.character)) { snprintf(settings.character, sizeof settings.character, "%s", menu.character); cret_settings_save(&settings); }
        frame_no = 0;
    } else if (mode != MODE_JOIN && char_name) {
        persona_start(&persona, sim, me, char_name, cfg.player_species, &hud);
        if (!persona.on) fprintf(stderr, "cretaceous: unusable character name %s\n", char_name);
    } else if (mode == MODE_JOIN && char_name) snprintf(hud.character, sizeof hud.character, "%s", char_name);
    ctl.yaw = sim->agents[me].yaw; ctl.target_ok = false;
    memcpy(prev, sim->agents, sizeof prev);
    int req_seq = 0;   // the client's latest respawn request (older replies are stale)
    if (mode == MODE_JOIN && want_species >= 0) req_seq = cret_net_client_request(net, want_species);
    bool was_alive = sim->agents[me].alive; float was_x = sim->agents[me].x, was_z = sim->agents[me].z;
    mouse_capture(&ctl.mouse, true);   // mouse look; Tab and the menus release the cursor
    bool in_settings = false;
    while (!WindowShouldClose() && !quit) {
        float dt = GetFrameTime(); if (dt > 0.1f) dt = 0.1f;
        bool menu_was = hud.menu;
        if (!hud.menu && IsKeyPressed(KEY_ESCAPE)) { hud.menu = true; hud.sel = 0; hud.species = sim->agents[me].species; mouse_capture(&ctl.mouse, false); }
        // Dead: Enter respawns at once (the sim respawns by itself after a delay).
        if (!hud.menu && !sim->agents[me].alive && IsKeyPressed(KEY_ENTER)) {
            if (mode == MODE_JOIN) req_seq = cret_net_client_request(net, -1); else cret_sim_respawn(sim, me);
        }
        if (!hud.menu && IsKeyPressed(KEY_M)) hud.map_big = !hud.map_big;
        if (IsKeyPressed(KEY_F)) { ctl.free_cam = !ctl.free_cam; if (ctl.free_cam) { ctl.pitch = cam.pitch; ctl.yaw = cam.yaw; } }
        if (IsKeyPressed(KEY_F5)) cret_view_reload_shaders(&view);
        if (IsKeyPressed(KEY_F1)) show_help = !show_help;
        if (IsKeyPressed(KEY_F2)) view.settings.wireframe = !view.settings.wireframe;
        if (IsKeyPressed(KEY_C)) view.settings.clouds = !view.settings.clouds;
        if (IsKeyPressed(KEY_TAB) && !hud.menu && !ctl.mouse.drag_mode) mouse_capture(&ctl.mouse, !IsCursorHidden());
        if (IsKeyDown(KEY_T)) time_scale = 60; else time_scale = 1;
        // Weather is the sim's (the server's for clients); +/- fixes the cover by hand (solo and host).
        if (mode != MODE_JOIN && (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_MINUS))) {
            sim->cfg.weather = 0; sim->cfg.cloud_cover = clampf(sim->cloud_cover + (IsKeyPressed(KEY_EQUAL) ? 0.05f : -0.05f), 0, 1); sim->cloud_cover = sim->cfg.cloud_cover;
        }
        view.settings.cloud_coverage = sim->cloud_cover;
        if (IsWindowResized()) cret_view_resize(&view, GetRenderWidth(), GetRenderHeight());
        CretInput mine = ctl.free_cam || hud.menu ? (CretInput){0} : player_input(&ctl, &sim->agents[me], sim->world);
        // Presses (attacks, call, jump, abilities) wait for the next tick: frames
        // outrun the 30 Hz sim, and a click in a frame without a tick was lost.
        static uint8_t press_act, press_ability, press_jump;
        if (ctl.free_cam || hud.menu) press_act = press_ability = press_jump = 0;
        if (mine.action == CRET_ACT_CALL || cret_act_is_attack(mine.action)) press_act = mine.action;
        if (mine.ability) press_ability = mine.ability;
        if (mine.jump) press_jump = 1;
        if (press_act) mine.action = press_act;
        mine.ability = press_ability; mine.jump = press_jump;
        acc += dt;
        if (hud.menu && mode == MODE_PLAY) acc = 0;   // solo play pauses under the menu
        while (acc >= CRET_DT) {
            acc -= CRET_DT;
            if (mode == MODE_JOIN) {   // send and predict; the server's snapshots reconcile (cret_net_client_poll)
                cret_net_client_tick(net, sim, &mine);
                if (press_act) mine.action = 0;
                mine.jump = mine.ability = 0; press_act = press_ability = press_jump = 0;
                continue;
            }
            for (int i = 0; i < sim->cfg.num_agents; i++) inputs[i] = cret_bot_input(sim, i);
            if (!bot_player) inputs[me] = mine;
            if (net) { cret_net_server_poll(net, sim, inputs, remote); server_characters(sim, was_human, 1); }
            memcpy(prev, sim->agents, sizeof prev);
            cret_sim_step(sim, inputs);
            if (time_scale > 1) sim->time_of_day = fmodf(sim->time_of_day + CRET_DT * (time_scale - 1) / sim->cfg.day_seconds, 1.0f);
            if (net && sim->tick % CRET_SNAPSHOT_DIV == 0) cret_net_server_broadcast(net, sim);
            if (press_act) mine.action = 0;   // presses act once
            mine.jump = mine.ability = 0; press_act = press_ability = press_jump = 0;
        }
        if (mode == MODE_JOIN && !cret_net_client_poll(net, sim)) { fprintf(stderr, "cretaceous: disconnected\n"); break; }
        persona_update(&persona, sim, me, &hud, mode != MODE_JOIN);
        CretSpawnReply rep;
        if (mode == MODE_JOIN && cret_net_client_spawn_reply(net, &rep) && rep.seq == (req_seq & 255)) {
            if (rep.result == CRET_SPAWN_IN_COMBAT) cret_hud_notice(&hud, TextFormat("You cannot respawn while fighting (%d s)", rep.wait_s), 3);
            else if (rep.result == CRET_SPAWN_BAD_SPECIES) cret_hud_notice(&hud, "The server refused that species", 3);
        }
        // A new life or a jump across the map (respawn, local or replicated): the camera cuts to it.
        {
            const CretAgent* a = &sim->agents[me];
            if (a->alive && (!was_alive || fabsf(a->x - was_x) + fabsf(a->z - was_z) > 20)) ctl.target_ok = false;
            was_alive = a->alive; was_x = a->x; was_z = a->z;
        }
        // A hit-stop holds the drawn world still for a few frames (the sim runs on).
        bool frozen = cret_fx_frozen(fx);
        if (!frozen) {
            if (mode == MODE_JOIN) cret_net_client_draw(net, sim, draw, (float)(acc / CRET_DT), dt);   // snapshot buffer, smoothed prediction
            else interpolate_agents(draw, sim, prev, (float)(acc / CRET_DT));
        }
        cret_fx_update(fx, sim, draw, me, &g_fxs, audio, hud.menu && mode == MODE_PLAY ? 0 : dt);
        update_camera(&ctl, draw, view.veg_ok ? &view.veg : NULL, me, &cam, !hud.menu && mouse_playing(&ctl.mouse), dt);
        cret_fx_camera(fx, &cam);
        {   // Sound: distances from the player's head, panning from the camera.
            const CretAgent* a = &draw->agents[me]; float lis[3] = {cam.pos[0], cam.pos[1], cam.pos[2]};
            if (!ctl.free_cam && a->alive) { lis[0] = a->x; lis[1] = a->y + a->height * 0.8f; lis[2] = a->z; }
            cret_audio_update(audio, draw, me, cam.pos, lis, cam.yaw, view.settings.cloud_coverage, hud.menu && mode == MODE_PLAY, dt);
        }
        BeginDrawing();
        cret_view_render(&view, draw, &cam, frozen ? 1e-4f : dt);
        // Overlay from the interpolated copy (positions) with the live stats.
        cret_hud_draw(&hud, draw, me, view.viewproj, cam.yaw);
        if (hud.menu && in_settings) {
            CretMenuAction a = cret_menu_settings(&menu, &hud, audio, &settings);
            if (a == MENU_APPLY) apply_settings(&settings, &view, audio, &ctl, &hud, !hidden);
            else if (a == MENU_BACK) in_settings = false;
        } else if (hud.menu) {
            int sel0 = hud.sel;
            CretHudAction act = cret_hud_menu(&hud, sim, me);
            if (!menu_was) act = HUD_NONE;   // the Esc press that opened it
            if (hud.sel != sel0) cret_audio_ui(audio, CRET_UI_MOVE);
            if (act != HUD_NONE) cret_audio_ui(audio, CRET_UI_CLICK);
            if (act == HUD_SETTINGS) { in_settings = true; menu.page = 1; menu.set_sel = 0; }
            if (act == HUD_RESUME) { hud.menu = false; mouse_capture(&ctl.mouse, true); }
            else if (act == HUD_ABILITY) {
                const CretSpeciesAbilities* P = &CRET_SPECIES_ABILITIES[sim->agents[me].species % CRET_SPECIES];
                const char* nm = CRET_ABILITIES[P->ab[2 + hud.choice]].name;
                if (mode == MODE_JOIN) { cret_net_client_unlock(net, hud.choice); cret_hud_note(&hud, TextFormat("%s requested", nm)); }
                else if (cret_choice_take(sim, me, hud.choice)) { cret_hud_note(&hud, TextFormat("%s ready on key 3", nm)); persona_save(&persona, sim, me); }
                else cret_hud_note(&hud, TextFormat("%s needs %d marks", nm, P->cost[hud.choice]));
            }
            else if (act == HUD_RESPAWN) {
                if (mode == MODE_JOIN) req_seq = cret_net_client_request(net, hud.species); else { cret_sim_set_species(sim, me, hud.species); persona_save(&persona, sim, me); }
                ctl.target_ok = false; hud.menu = false; mouse_capture(&ctl.mouse, true);
            }
            else if (act == HUD_QUIT) quit = true;
        }
        if (show_help && !hud.menu) {   // grows with the HUD above 1080 lines, never shrinks
            const CretAgent* a = &sim->agents[me]; float u = fmaxf(1, cret_hud_scale(&hud)); int fs = (int)roundf(14 * u);
            DrawText(TextFormat("%d fps   time %05.2f   pos %.0f %.0f  alt %.0f m  %.1f m/s  %s%s", GetFPS(), sim->time_of_day * 24, a->x, a->z, a->y, a->speed,
                                a->swimming ? "swimming " : "", ctl.autorun ? "autorun" : ""), (int)roundf(12 * u), (int)roundf(10 * u), fs, (Color){230, 230, 230, 170});
            const char* help = ctl.mouse.drag_mode
                ? "WASD move  RMB drag camera  Shift sprint  Ctrl walk  Space jump  R autorun  E eat/drink  G call  LMB attack  RMB tap or Q tail/slash  1-3 abilities  N nest  M map  Esc menu  F free cam  T time x60  F1 hide"
                : "WASD move  Shift sprint  Ctrl walk  Space jump  R autorun  E eat/drink  G call  LMB attack  RMB or Q tail/slash  1-3 abilities  N nest  M map  Esc menu  Tab cursor  F free cam  T time x60  F1 hide";
            DrawText(help, GetScreenWidth() - MeasureText(help, fs) - (int)roundf(16 * u), GetScreenHeight() - (int)roundf(24 * u), fs, (Color){230, 230, 230, 160});
        }
        if (quit_after && ++frame_no >= quit_after) { if (shot_path) { Image im = LoadImageFromScreen(); ExportImage(im, shot_path); UnloadImage(im); } quit = true; }
        EndDrawing();
        static bool first_play = true; if (first_play) { first_play = false; CRET_LOAD_MARK("first play frame"); }
    }
    if (mode != MODE_JOIN) persona_save(&persona, sim, me);
    cret_audio_close(audio);
    cret_fx_close(fx); free(fx);
    cret_hud_close(&hud); cret_view_close(&view); CloseWindow();
    cret_net_close(net); cret_sim_close(sim); free(sim); free(draw);
    return 0;
}
