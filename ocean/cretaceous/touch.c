// Touch controls (touch.h). Included by hud.c, after its drawing helpers:
// layout is in the HUD's 1080-line units (U, SW, SH, text) and sizes come
// from CSS pixels, so a button is the same physical size on any phone.
#include "touch.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define TOUCH_MAX 10
// A finger's job, fixed when it comes down.
enum { F_IDLE, F_STICK, F_LOOK, F_BUTTON, F_SPENT };
enum { B_ATTACK, B_ATTACK2, B_JUMP, B_EAT, B_CALL, B_AB1, B_AB2, B_AB3, B_MENU, B_CARRY, B_NEST, B_SNIFF, B_COUNT };

typedef struct { int id, role, button; bool fresh; Vector2 pos, last; } Finger;
static struct {
    bool init, shown, forced, never;
    Finger f[TOUCH_MAX]; int n;
    bool tap; Vector2 tap_at;           // a finger came down this frame, and where (screen pixels)
    double last_touch;                  // GetTime() of the last frame a finger was down
    Vector2 stick_c; bool stick_on;     // the floating stick's centre while held
    bool held[B_COUNT];                 // buttons under a finger (drawn pressed)
} T;

// ---------------------------------------------------------------- browser
#ifdef __EMSCRIPTEN__
// raylib 5.5's web touch callback lists the touches of each event by
// identifier and drops the last entry on a touchend, so when one of two
// fingers lifts it can keep the lifted finger and lose the one still down
// (lifting the camera finger stopped the stick). The page's own record of the
// fingers still down (TouchEvent.touches) decides which of raylib's entries
// are real; a finger raylib lost keeps its last position until it moves.
// Each touchstart is also queued (canvas pixels): a tap shorter than a frame
// starts and ends between two polls, and raylib's list never shows it.
EM_JS(void, touch_web_watch, (void), {
    if (Module.cretTouchIds) return;
    const ids = new Set(), starts = []; Module.cretTouchIds = ids; Module.cretTouchStarts = starts;
    const sync = (e) => {
        ids.clear(); for (const t of e.touches) ids.add(t.identifier);
        if (e.type !== "touchstart" || !Module.canvas) return;
        const c = Module.canvas, r = c.getBoundingClientRect();
        if (r.width > 0 && r.height > 0)
            for (const t of e.changedTouches) starts.push([t.identifier, (t.clientX - r.left) * c.width / r.width, (t.clientY - r.top) * c.height / r.height]);
    };
    for (const ev of ["touchstart", "touchmove", "touchend", "touchcancel"]) window.addEventListener(ev, sync, { capture: true, passive: true });
});
EM_JS(int, touch_web_down, (int id), { return Module.cretTouchIds && Module.cretTouchIds.has(id) ? 1 : 0; });
// The oldest touchstart since the last call: 1 and its id and position, or 0.
EM_JS(int, touch_web_start, (int* id, float* x, float* y), {
    const q = Module.cretTouchStarts; if (!q || !q.length) return 0;
    const [i, px, py] = q.shift();
    HEAP32[id >> 2] = i; HEAPF32[x >> 2] = px; HEAPF32[y >> 2] = py;
    return 1;
});
// Records a key (raylib's code) or mouse button (1000 + button) going down
// (1) or up (0) in this frame (Module.cretInput: [frame, code, down], the
// latest few hundred).
EM_JS(void, touch_web_input, (int code, int down), {
    const l = Module.cretInput || (Module.cretInput = []);
    l.push([Module.cretFrames | 0, code, down]);
    if (l.length > 512) l.splice(0, 256);
});
// A phone or tablet: touch is the main pointer and nothing hovers.
EM_JS(int, touch_web_first, (void), {
    return window.matchMedia && matchMedia("(pointer: coarse)").matches && !matchMedia("(hover: hover)").matches ? 1 : 0;
});
// Canvas pixels per CSS pixel (the page sizes the canvas in CSS pixels).
EM_JS(double, touch_web_px, (void), {
    const c = Module.canvas, r = c ? c.getBoundingClientRect() : null;
    return r && r.width > 0 ? c.width / r.width : 1;
});
#endif

// Screen pixels per CSS pixel (1/96 inch on a desktop; phones scale theirs to
// a comfortable reading size), times the player's interface size.
static float touch_px(const CretHud* h) {
#ifdef __EMSCRIPTEN__
    float px = (float)touch_web_px();
#else
    float px = GetWindowScaleDPI().x;
#endif
    if (!(px > 0.25f && px < 8)) px = 1;
    return px * clampf(h->ui_scale > 0 ? h->ui_scale : 1, 0.5f, 2.0f);
}

static void touch_init(void) {
    T.init = true;
    const char* e = getenv("CRET_TOUCH");
    T.forced = e && atoi(e) == 1; T.never = e && *e && atoi(e) == 0;
    T.shown = T.forced;
#ifdef __EMSCRIPTEN__
    touch_web_watch();
    if (!T.never && touch_web_first()) T.shown = true;
#endif
}

static Finger* finger(int id) { for (int i = 0; i < T.n; i++) if (T.f[i].id == id) return &T.f[i]; return NULL; }

void cret_touch_frame(void) {
    if (!T.init) touch_init();
    T.tap = false;
    for (int i = 0; i < T.n; i++) T.f[i].fresh = false;
    // This frame's fingers: raylib's list (checked against the page's on web).
    int ids[TOUCH_MAX]; Vector2 at[TOUCH_MAX]; int n = 0;
    int c = GetTouchPointCount();
    for (int i = 0; i < c && n < TOUCH_MAX; i++) {
        int id = GetTouchPointId(i);
#ifdef __EMSCRIPTEN__
        if (!touch_web_down(id)) continue;
#endif
        ids[n] = id; at[n] = GetTouchPosition(i); n++;
    }
#ifdef __EMSCRIPTEN__
    for (int i = 0; i < T.n && n < TOUCH_MAX; i++) {   // still down, missing from raylib's list
        bool listed = false; for (int k = 0; k < n; k++) listed |= ids[k] == T.f[i].id;
        if (!listed && touch_web_down(T.f[i].id)) { ids[n] = T.f[i].id; at[n] = T.f[i].pos; n++; }
    }
#endif
    // Lifted fingers go; the stick lets go with its finger.
    for (int i = 0; i < T.n;) {
        bool down = false; for (int k = 0; k < n; k++) down |= ids[k] == T.f[i].id;
        if (down) { i++; continue; }
        if (T.f[i].role == F_STICK) T.stick_on = false;
        T.f[i] = T.f[--T.n];
    }
    for (int k = 0; k < n; k++) {
        Finger* f = finger(ids[k]);
        if (f) { f->last = f->pos; f->pos = at[k]; continue; }
        if (T.n >= TOUCH_MAX) break;
        T.f[T.n++] = (Finger){ids[k], F_IDLE, -1, true, at[k], at[k]};
        if (!T.tap) { T.tap = true; T.tap_at = at[k]; }
    }
#ifdef __EMSCRIPTEN__
    // Taps that came and went since the last frame: here for this frame only
    // (they are not down, so the next frame lets them go), enough to press a
    // button or pick a menu row.
    int sid; float sx, sy;
    while (touch_web_start(&sid, &sx, &sy)) {
        if (finger(sid) || T.n >= TOUCH_MAX) continue;
        T.f[T.n++] = (Finger){sid, F_IDLE, -1, true, {sx, sy}, {sx, sy}};
        if (!T.tap) { T.tap = true; T.tap_at = (Vector2){sx, sy}; }
        n++;   // counts as touch for showing the controls
    }
#endif
    double now = GetTime();
    if (n > 0) { T.last_touch = now; if (!T.never) T.shown = true; }
    // A key, a mouse click or the wheel hides the controls (touch shows them
    // again). Motion alone does not: raylib moves the mouse with a single
    // finger, and a resize (turning the phone) moved it too. A click counts
    // only well after the last touch, in case a browser follows a tap with
    // the mouse events it emulates.
    if (T.shown && !T.forced) {
        bool used = false;
        for (int k = KEY_SPACE; k <= KEY_KB_MENU && !used; k++) used = IsKeyPressed(k);
        if (n == 0 && now - T.last_touch > 0.5) {
            used |= GetMouseWheelMove() != 0;
            for (int b = MOUSE_BUTTON_LEFT; b <= MOUSE_BUTTON_MIDDLE; b++) used |= IsMouseButtonPressed(b);
        }
        if (used) { T.shown = false; T.stick_on = false; }
    }
#ifdef __EMSCRIPTEN__
    // For tools/web_smoke.js --short-press: the keys and mouse buttons that
    // went down or up this frame, as everything in the game sees them.
    for (int k = KEY_SPACE; k <= KEY_KB_MENU; k++)
        if (IsKeyPressed(k) || IsKeyReleased(k)) touch_web_input(k, IsKeyPressed(k));
    for (int b = MOUSE_BUTTON_LEFT; b <= MOUSE_BUTTON_MIDDLE; b++)
        if (IsMouseButtonPressed(b) || IsMouseButtonReleased(b)) touch_web_input(1000 + b, IsMouseButtonPressed(b));
#endif
}

bool cret_touch_active(void) { return T.shown; }

bool cret_touch_tap(Vector2* at) {
    if (T.tap && at) *at = T.tap_at;
    return T.tap;
}

// ---------------------------------------------------------------- layout
// Screen pixels. Sizes are CSS pixels times k: about 9 mm for a button and
// 12 mm for the attack on a phone (Apple and Google ask for at least 7 mm).
typedef struct {
    Vector2 c[B_COUNT]; float r[B_COUNT]; bool on[B_COUNT];
    Vector2 stick_home; float stick_r; float k;
    Rectangle map;      // the minimap (a tap there opens the island map)
} Layout;

static Layout touch_layout(CretHud* h, const CretSim* sim, int me) {
    Layout L; memset(&L, 0, sizeof L);
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    // Shrunk when the window is too small to hold the right cluster (230 CSS
    // pixels tall) in its lower two thirds, or the stick, the bottom row and
    // the cluster side by side (900) in its width. Held upright (the page asks
    // to turn the phone, but play goes on) the row moves up above the cluster,
    // so the stick and the cluster alone need the width (430).
    bool tall = H > W;
    float k = fminf(touch_px(h), fminf(H * 0.62f / 230, W / (tall ? 430 : 900)));
    L.k = k;
    const CretAgent* a = &sim->agents[me]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    // Right thumb: attack in the corner, jump, the secondary attack and eat in an arc round it.
    static const float R[4][3] = {{82, 82, 40}, {170, 152, 30}, {192, 50, 30}, {82, 200, 30}};   // dx, dy from the corner, radius
    for (int b = B_ATTACK; b <= B_EAT; b++) { L.c[b] = (Vector2){W - R[b][0] * k, H - R[b][1] * k}; L.r[b] = R[b][2] * k; L.on[b] = true; }
    L.on[B_ATTACK2] = S->alt.kind != CRET_ALT_NONE;
    L.on[B_JUMP] = S->jump > 0 || a->latch;   // jump also lets go of a latch
    // Carry (carnivores): left of the arc, shown only when B would do something.
    L.c[B_CARRY] = (Vector2){W - 262 * k, H - 118 * k}; L.r[B_CARRY] = 26 * k;
    L.on[B_CARRY] = S->diet == CRET_DIET_CARNIVORE && cret_carry_state(sim, me, NULL) > 0;
    // Sniff (hold, as V; carnivores): above it. Nest (hold, as N): above that,
    // for a fed adult without a nest (progress.c builds it in 5 s).
    L.c[B_SNIFF] = (Vector2){W - 262 * k, H - 206 * k}; L.r[B_SNIFF] = 26 * k;
    L.on[B_SNIFF] = a->alive && S->diet == CRET_DIET_CARNIVORE;
    L.c[B_NEST] = (Vector2){W - 262 * k, H - 294 * k}; L.r[B_NEST] = 26 * k;
    L.on[B_NEST] = a->alive && a->growth >= 0.9f && a->food >= 0.3f && cret_nest_of(sim, me) < 0;
    // Bottom row, where the ability bar is with a keyboard: call, abilities 1-3.
    for (int b = B_CALL; b <= B_AB3; b++) { L.c[b] = (Vector2){W * 0.5f + (b - B_CALL - 1.5f) * 68 * k, H - (tall ? 290 : 48) * k}; L.r[b] = 27 * k; L.on[b] = true; }
    // Menu: top right, left of the minimap.
    Rectangle mb = map_box(h); float u = scale_for(h, 600);
    L.map = (Rectangle){mb.x * u, mb.y * u, mb.width * u, mb.height * u};
    L.r[B_MENU] = 22 * k; L.c[B_MENU] = (Vector2){L.map.x - 14 * k - L.r[B_MENU], L.map.y + L.r[B_MENU]}; L.on[B_MENU] = true;
    L.stick_r = 62 * k; L.stick_home = (Vector2){(28 + 62) * k, H - (28 + 62) * k};
    return L;
}

static int hit_button(const Layout* L, Vector2 p, bool alive) {
    for (int b = 0; b < B_COUNT; b++) {
        if (!L->on[b] || (!alive && b != B_MENU)) continue;
        float dx = p.x - L->c[b].x, dy = p.y - L->c[b].y, r = L->r[b] * 1.15f;   // a little slack round the rim
        if (dx * dx + dy * dy <= r * r) return b;
    }
    return -1;
}

CretTouchPlay cret_touch_play(CretHud* h, const CretSim* sim, int me) {
    CretTouchPlay out; memset(&out, 0, sizeof out);
    memset(T.held, 0, sizeof T.held);
    h->touch = T.shown;   // the HUD's touch layout (a smaller minimap) is what the layout measures
    if (!T.shown) return out;
    Layout L = touch_layout(h, sim, me);
    const CretAgent* a = &sim->agents[me];
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    for (int i = 0; i < T.n; i++) {
        Finger* f = &T.f[i];
        if (f->fresh && f->role == F_IDLE) {
            int b = hit_button(&L, f->pos, a->alive);
            if (b >= 0) {
                f->role = F_BUTTON; f->button = b;
                if (b == B_ATTACK) out.attack = true;
                else if (b == B_ATTACK2) out.attack2 = true;
                else if (b == B_JUMP) out.jump = true;
                else if (b == B_CALL) out.call = true;
                else if (b >= B_AB1 && b <= B_AB3) out.ability = b - B_AB1 + 1;
                else if (b == B_MENU) out.menu = true;
                else if (b == B_CARRY) out.carry = true;
            } else if (h->map_big || CheckCollisionPointRec(f->pos, L.map)) { f->role = F_SPENT; out.map = true; }
            else if (!a->alive) { f->role = F_SPENT; out.respawn = true; }
            else if (!T.stick_on && f->pos.x < W * 0.45f && f->pos.y > H * 0.3f) {
                // The stick centres where the thumb lands, kept whole on the screen.
                f->role = F_STICK; T.stick_on = true;
                T.stick_c = (Vector2){clampf(f->pos.x, L.stick_r, W - L.stick_r), clampf(f->pos.y, L.stick_r, H - L.stick_r)};
            } else f->role = F_LOOK;
        }
        if (f->role == F_BUTTON) { T.held[f->button] = true; if (f->button == B_EAT) out.eat = true; if (f->button == B_NEST) out.nest = true; if (f->button == B_SNIFF) out.sniff = true; }
        if (f->role == F_LOOK && !f->fresh) {
            // 0.006 rad per CSS pixel: a drag up the height of a phone turns about 130 degrees.
            float s = 3.0f / (L.k > 0 ? L.k : 1);
            out.look.x += (f->pos.x - f->last.x) * s; out.look.y += (f->pos.y - f->last.y) * s; out.looking = true;
        }
        if (f->role == F_LOOK) out.looking = true;
        if (f->role == F_STICK) {
            float dx = (f->pos.x - T.stick_c.x) / L.stick_r, dy = (f->pos.y - T.stick_c.y) / L.stick_r, m = sqrtf(dx * dx + dy * dy);
            if (m > 1) { dx /= m; dy /= m; }
            if (m > 0.12f) { out.move_x = dx; out.move_y = -dy; }   // a small dead zone round the centre
            out.sprint = m >= 0.95f;                                 // pushed to the rim, or past it
        }
    }
    if (!a->alive) T.stick_on = false;
    T.tap = false;   // play took this frame's fingers: a menu opened by the menu button must not take its tap too
    return out;
}

// ---------------------------------------------------------------- drawing
// Each button is a soft dark disc with a faint rim, its icon and a short
// word under the icon; pressed, the rim turns amber. The stick and the
// buttons keep the places and sizes touch_layout gives them.
static void touch_disc(Vector2 c, float r, bool pressed, bool dim) {
    DrawCircleV(c, r + 2, fa(with(UI_SHADOW, 0.8f)));
    DrawCircleV(c, r, fa(with(UI_PANEL, pressed ? 1.25f : 0.85f)));
    DrawRing(c, r - 1.6f, r, 0, 360, 48, fa(pressed ? UI_ACCENT : with(UI_INK, dim ? 0.2f : 0.42f)));
}
// The icon above the button's centre and its word below, sized to the button.
static void touch_face(const CretHud* h, Vector2 c, float r, int ic, const char* word, Color col) {
    float fs = clampf(r * 0.36f, 9, UI_T_SMALL + 1);
    icon(ic, (Vector2){c.x, c.y - r * 0.18f}, r * 0.78f, col);
    if (word) text(h, false, word, c.x - text_w(h, false, word, fs) * 0.5f, c.y + r * 0.36f, fs, with(col, 0.9f));
}
// The dark sweep of a cooldown and the rim coming back, as on the ability bar.
static void touch_cooldown(Vector2 c, float r, float frac) {
    if (frac <= 0) return;
    DrawCircleSector(c, r - 1, -90, -90 + 360 * frac, 40, fa((Color){0, 0, 0, 120}));
    DrawRing(c, r - 2.5f, r, -90 + 360 * frac, 270, 40, fa(with(UI_INK, 0.55f)));
}

void cret_touch_draw(CretHud* h, const CretSim* sim, int me) {
    h->touch = T.shown;
    if (!T.shown) return;
    Layout L = touch_layout(h, sim, me);
    scale_begin(h, 600);
    float u = U, k = L.k / u;   // layout units per CSS pixel
    #define LP(p) ((Vector2){(p).x / u, (p).y / u})
    const CretAgent* a = &sim->agents[me]; const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    // Menu is always there; the rest only while alive.
    Vector2 mc = LP(L.c[B_MENU]); float mr = L.r[B_MENU] / u;
    touch_disc(mc, mr, T.held[B_MENU], false);
    icon(UI_I_MENU, mc, mr * 0.9f, UI_INK);
    if (!a->alive || h->map_big) { scale_end(); return; }
    // Stick: where it is held, or faintly at home.
    float sr = L.stick_r / u;
    Vector2 sc = LP(T.stick_on ? T.stick_c : L.stick_home), knob = sc;
    bool sprint = false;
    for (int i = 0; i < T.n; i++) if (T.f[i].role == F_STICK) {
        Vector2 p = LP(T.f[i].pos); float dx = p.x - sc.x, dy = p.y - sc.y, m = sqrtf(dx * dx + dy * dy);
        sprint = m >= sr * 0.95f;
        if (m > sr) { dx *= sr / m; dy *= sr / m; }
        knob = (Vector2){sc.x + dx, sc.y + dy};
    }
    float al = T.stick_on ? 1 : 0.5f;
    DrawCircleV(sc, sr, fa(with(UI_PANEL, 0.55f * al)));
    DrawRing(sc, sr - 1.6f, sr, 0, 360, 64, fa(sprint ? UI_ACCENT : with(UI_INK, 0.42f * al)));
    for (int q = 0; q < 4; q++) {   // four faint ticks: the directions
        float an = q * 1.5707963f; Vector2 t = {sc.x + cosf(an) * sr * 0.8f, sc.y + sinf(an) * sr * 0.8f};
        DrawCircleV(t, 1.6f * k, fa(with(UI_INK, 0.35f * al)));
    }
    DrawCircleV(knob, sr * 0.42f + 1.5f, fa(with(UI_SHADOW, al)));
    DrawCircleV(knob, sr * 0.42f, fa(with(UI_INK, 0.55f * al)));
    if (sprint) {   // clear of the thumb
        Vector2 lc = {sc.x, knob.y < sc.y ? sc.y + sr + 14 * k : sc.y - sr - 14 * k};
        icon(UI_I_SPRINT, (Vector2){lc.x - 24 * k, lc.y}, 13 * k, UI_ACCENT);
        text_sh(h, "Sprint", lc.x - 14 * k, lc.y - 6.5f * k, 13 * k, UI_ACCENT);
    }
    // Attacks, with their cooldowns.
    for (int b = B_ATTACK; b <= B_ATTACK2; b++) {
        if (!L.on[b]) continue;
        Vector2 c = LP(L.c[b]); float r = L.r[b] / u;
        float full = (b == B_ATTACK2 ? S->alt.cooldown : S->cooldown) * CRET_TICK_HZ, cd = (float)(b == B_ATTACK2 ? a->attack2_cd : a->attack_cd);
        float frac = full > 0 ? clampf(cd / full, 0, 1) : 0;
        int kk = b == B_ATTACK2;
        touch_disc(c, r, T.held[b], frac > 0);
        touch_face(h, c, r, attack_icon(S, kk), kk && S->alt.kind != CRET_ALT_SLASH ? "Swipe" : attack_name(S, kk), frac > 0 ? UI_DIM : UI_INK);
        touch_cooldown(c, r, frac);
    }
    // Jump and eat: eat says drink when water is ahead (as E does).
    if (L.on[B_JUMP]) { Vector2 c = LP(L.c[B_JUMP]); float r = L.r[B_JUMP] / u; touch_disc(c, r, T.held[B_JUMP], false); touch_face(h, c, r, UI_I_JUMP, a->latch ? "Let go" : "Jump", UI_INK); }
    {
        float ax = a->x + cosf(a->yaw) * (a->radius + 1.5f), az = a->z + sinf(a->yaw) * (a->radius + 1.5f);
        bool water = sim->world && cret_water(sim->world, ax, az) > cret_height(sim->world, ax, az);
        Vector2 c = LP(L.c[B_EAT]); float r = L.r[B_EAT] / u; touch_disc(c, r, T.held[B_EAT], false);
        touch_face(h, c, r, water ? UI_I_DROP : S->diet == CRET_DIET_CARNIVORE ? UI_I_MEAT : UI_I_LEAF, water ? "Drink" : "Eat", UI_INK);
    }
    if (L.on[B_CARRY]) {   // "Drop" while carrying
        Vector2 c = LP(L.c[B_CARRY]); float r = L.r[B_CARRY] / u; touch_disc(c, r, T.held[B_CARRY], false);
        touch_face(h, c, r, UI_I_CARRY, a->carry ? "Drop" : "Carry", UI_INK);
    }
    if (L.on[B_SNIFF]) { Vector2 c = LP(L.c[B_SNIFF]); float r = L.r[B_SNIFF] / u; touch_disc(c, r, T.held[B_SNIFF], false); touch_face(h, c, r, UI_I_SNIFF, "Sniff", UI_INK); }
    if (L.on[B_NEST]) { Vector2 c = LP(L.c[B_NEST]); float r = L.r[B_NEST] / u; touch_disc(c, r, T.held[B_NEST], false); touch_face(h, c, r, UI_I_NEST, "Nest", UI_INK); }
    // Call and abilities 1-3: the ability bar's look (its icon, the cooldown
    // sweep and seconds, an amber ring when ready, blue when short of stamina).
    { Vector2 c = LP(L.c[B_CALL]); float r = L.r[B_CALL] / u; touch_disc(c, r, T.held[B_CALL], false); touch_face(h, c, r, UI_I_CALL, "Call", UI_INK); }
    for (int s = 0; s < CRET_AB_SLOTS && s < 3; s++) {
        int b = B_AB1 + s, ab = cret_ability_of(sim, me, s);
        Vector2 c = LP(L.c[b]); float r = L.r[b] / u;
        if (!ab) { slot(h, c, r, 0, 0, 0, false, true, true); continue; }
        const CretAbility* A = &CRET_ABILITIES[ab];
        float frac = A->cooldown > 0 ? clampf(a->ab_cd[s] / A->cooldown, 0, 1) : 0;
        bool stam = a->stamina >= A->stamina, blocked = a->st[CRET_ST_STUN] > 0 || a->latch;
        slot(h, c, r, ability_icon(ab), frac, a->ab_cd[s], blocked, stam, false);
        if (T.held[b]) DrawRing(c, r - 2.5f, r, 0, 360, 48, fa(UI_ACCENT));
    }
    #undef LP
    scale_end();
}
