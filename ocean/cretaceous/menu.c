#include "menu.h"
#include "touch.h"
#include "render/view.h"
#include "character.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

// ---------------------------------------------------------------- settings file
static const int RES[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
int cret_settings_resolutions(void) { return (int)(sizeof RES / sizeof RES[0]); }
void cret_settings_resolution(int i, int* w, int* h) {
    if (i < 0 || i >= cret_settings_resolutions()) i = 1;
    *w = RES[i][0]; *h = RES[i][1];
}

void cret_settings_default(CretSettings* s) {
    memset(s, 0, sizeof *s);
    s->preset = CRET_PRESET_HIGH; s->render_scale = 1.0f; s->ui_scale = 1.0f; s->res = 1; s->mouse_sens = 1.0f; s->shake = 1.0f; s->hitstop = 1.0f;
    s->volume[CRET_AUDIO_MASTER] = 0.8f; s->volume[CRET_AUDIO_AMBIENCE] = 0.8f; s->volume[CRET_AUDIO_EFFECTS] = 0.9f; s->volume[CRET_AUDIO_UI] = 0.6f;
#ifdef __EMSCRIPTEN__
    // WebGL runs on whatever GPU the browser has. The page tells a desktop with
    // a hardware GPU (high: start there and step down if it is slow) from a
    // phone, a tablet or a software renderer (low, as before).
    const char* q = getenv("CRET_WEB_QUALITY");
    if (q && !strcmp(q, "high")) s->auto_quality = true;
    else { s->preset = CRET_PRESET_LOW; s->render_scale = 0.7f; }
#endif
}

static void settings_path(char* out, size_t n, bool make_dir) {
    char dir[512];
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");   // %APPDATA%\Cretaceous
    snprintf(dir, sizeof dir, "%s\\Cretaceous", appdata && *appdata ? appdata : ".");
    if (make_dir) mkdir(dir, 0755);
#else
    const char* xdg = getenv("XDG_CONFIG_HOME"); const char* home = getenv("HOME");
    if (xdg && *xdg) snprintf(dir, sizeof dir, "%s/cretaceous", xdg);
    else snprintf(dir, sizeof dir, "%s/.config/cretaceous", home ? home : ".");
    if (make_dir) {
        char parent[512]; snprintf(parent, sizeof parent, "%s", dir);
        char* sl = strrchr(parent, '/'); if (sl) { *sl = 0; mkdir(parent, 0755); }
        mkdir(dir, 0755);
    }
#endif
    snprintf(out, n, "%s/settings.ini", dir);
}

static const char* KEYS[] = {"preset", "render_scale", "fullscreen", "resolution", "mouse_sensitivity", "invert_y",
                             "volume_master", "volume_ambience", "volume_effects", "volume_interface", "ui_scale", "camera_shake", "hit_stop", "auto_quality"};
enum { KEY_UI_SCALE = 6 + CRET_AUDIO_BUSES, KEY_SHAKE, KEY_HITSTOP, KEY_AUTO };

void cret_settings_load(CretSettings* s) {
    cret_settings_default(s);
    char path[600]; settings_path(path, sizeof path, false);
    FILE* f = fopen(path, "r"); if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[64]; float v;
        if (sscanf(line, " character = %23s", s->character) == 1) continue;
        if (line[0] == '#' || line[0] == ';' || sscanf(line, " %63[a-z_] = %f", key, &v) != 2) continue;
        if (!strcmp(key, KEYS[0])) s->preset = (int)clampf(v, CRET_PRESET_LOW, CRET_PRESET_ULTRA);
        else if (!strcmp(key, KEYS[1])) s->render_scale = clampf(v, 0.5f, 1.0f);
        else if (!strcmp(key, KEYS[2])) s->fullscreen = v != 0;
        else if (!strcmp(key, KEYS[3])) s->res = (int)clampf(v, 0, cret_settings_resolutions() - 1);
        else if (!strcmp(key, KEYS[4])) s->mouse_sens = clampf(v, 0.05f, 3.0f);
        else if (!strcmp(key, KEYS[5])) s->invert_y = v != 0;
        else if (!strcmp(key, KEYS[KEY_UI_SCALE])) s->ui_scale = clampf(v, 0.5f, 2.0f);
        else if (!strcmp(key, KEYS[KEY_SHAKE])) s->shake = clampf(v, 0, 1);
        else if (!strcmp(key, KEYS[KEY_HITSTOP])) s->hitstop = clampf(v, 0, 1);
        else if (!strcmp(key, KEYS[KEY_AUTO])) s->auto_quality = v != 0;
        else for (int b = 0; b < CRET_AUDIO_BUSES; b++) if (!strcmp(key, KEYS[6 + b])) s->volume[b] = clampf(v, 0, 1);
    }
    fclose(f);
}

bool cret_settings_save(const CretSettings* s) {
    char path[600]; settings_path(path, sizeof path, true);
    FILE* f = fopen(path, "w"); if (!f) return false;
    fprintf(f, "# Cretaceous settings (written by the game)\n");
    fprintf(f, "%s = %d\n%s = %.2f\n%s = %d\n%s = %d\n%s = %.2f\n%s = %d\n", KEYS[0], s->preset, KEYS[1], s->render_scale, KEYS[2], s->fullscreen,
            KEYS[3], s->res, KEYS[4], s->mouse_sens, KEYS[5], s->invert_y);
    for (int b = 0; b < CRET_AUDIO_BUSES; b++) fprintf(f, "%s = %.2f\n", KEYS[6 + b], s->volume[b]);
    if (s->character[0]) fprintf(f, "character = %s\n", s->character);
    fprintf(f, "%s = %.2f\n", KEYS[KEY_UI_SCALE], s->ui_scale);
    fprintf(f, "%s = %.2f\n%s = %.2f\n", KEYS[KEY_SHAKE], s->shake, KEYS[KEY_HITSTOP], s->hitstop);
    fprintf(f, "%s = %d\n", KEYS[KEY_AUTO], s->auto_quality);
    bool ok = fclose(f) == 0;
    cret_config_saved();
    return ok;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// The page's fullscreen. Asked from the frame after the click or key that
// changed the setting, which is still within the browser's grace period for
// a user gesture; a refusal leaves the page as it is.
EM_JS(void, web_fullscreen, (int on), {
    const d = document, el = d.documentElement;
    if (on && !d.fullscreenElement && el.requestFullscreen) el.requestFullscreen({ navigationUI: "hide" }).catch(() => {});
    if (!on && d.fullscreenElement && d.exitFullscreen) d.exitFullscreen().catch(() => {});
});
EM_JS(int, web_is_fullscreen, (void), { return document.fullscreenElement ? 1 : 0; });
void cret_settings_web_fullscreen(bool on) { web_fullscreen(on); }
#endif

// ---------------------------------------------------------------- automatic quality
void cret_quality_watch(CretQualityWatch* q) { q->watching = true; q->t = 0; q->frames = q->n = q->slow = 0; }

bool cret_quality_frame(CretQualityWatch* q, CretSettings* s, float dt) {
    if (!q->watching || !s->auto_quality || getenv("CRET_PRESET") || getenv("CRET_RENDER_SCALE")) return false;
    q->t += dt; q->frames++;
    if (q->t < 2.0f || q->frames <= 3) return false;   // loading hitches and shader compiles
    q->n++; q->slow += dt > 1.0f / 30;
    if (q->t < 6.0f || q->n < 4) return false;
    // Most frames under 30 fps (a median, so one long hitch does not count): one step lower.
    if (q->slow * 2 <= q->n) { q->watching = false; return false; }
    if (s->render_scale > 0.71f) s->render_scale = fmaxf(0.7f, s->render_scale - 0.15f);
    else if (s->preset > CRET_PRESET_LOW) s->preset--;
    else if (s->render_scale > 0.51f) s->render_scale = 0.5f;
    else { q->watching = false; return false; }   // nothing lower
    static const char* PRESET[3] = {"Low", "Medium", "High"};
    if (!q->told) snprintf(q->note, sizeof q->note, "Graphics lowered to %s at %d%% to keep the game smooth. Change it in Settings.",
                           PRESET[s->preset], (int)roundf(s->render_scale * 100));
    q->told = true;
    cret_quality_watch(q);   // measure the new level
    return true;
}

// ---------------------------------------------------------------- widgets
typedef struct { CretHud* h; CretAudio* au; Vector2 m; bool click, moved, tap; } Ui;

// A tap (touch.h) is a click where the finger came down that also moves the
// selection there, as hovering does with a mouse.
static Ui ui_frame(CretHud* h, CretAudio* au) {
    Vector2 d = GetMouseDelta();
    Ui u = {h, au, cret_ui_mouse(), IsMouseButtonPressed(MOUSE_BUTTON_LEFT), d.x != 0 || d.y != 0, false};
    Vector2 t; if (cret_ui_tap(&t)) { u.m = t; u.click = u.moved = u.tap = true; }
    return u;
}

// Each screen fades in over UI_FADE when it opens (a page changes, or it was
// not drawn a moment ago); returns the fade, 0..1.
static float screen_fade(int screen) {
    static int last = -1; static double t0, seen;
    double now = GetTime();
    if (screen != last || now - seen > 0.25) { last = screen; t0 = now; }
    seen = now;
    return clampf((float)(now - t0) / UI_FADE, 0, 1);
}

// The controls for the way the player works the menus, along the bottom:
// key caps for keys, words for the mouse and for touch.
static void controls(const CretHud* h, float x, float y, bool centre, const char* back, const char* mouse_words, const char* touch_words) {
    int in = cret_ui_input();
    if (in == CRET_UI_TOUCH || in == CRET_UI_MOUSE) {
        const char* keys[2] = {in == CRET_UI_MOUSE ? "LMB" : "", in == CRET_UI_MOUSE ? "Esc" : ""};
        const char* verbs[2] = {in == CRET_UI_MOUSE ? mouse_words : touch_words, back};
        cret_ui_footer(h, keys, verbs, in == CRET_UI_MOUSE && back ? 2 : 1, x, y, centre);
        return;
    }
    const char* keys[4] = {"Up Down", "Left Right", "Enter", "Esc"}; const char* verbs[4] = {"Choose", "Change", "Select", back};
    cret_ui_footer(h, keys, verbs, back ? 4 : 3, x, y, centre);
}

// Arrow keys / WS move the selection over `n` rows.
static void nav(Ui* u, int* sel, int n) {
    int s0 = *sel;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) *sel = (*sel + 1) % n;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) *sel = (*sel + n - 1) % n;
    if (*sel != s0) cret_audio_ui(u->au, CRET_UI_MOVE);
}

// Rows are 22 units of text in a 38-unit box; a page with tighter rows (the
// scenario list on a phone) sets g_pitch, and the text and the selection
// mark shrink to fit while the box that takes clicks stays the same.
static float g_pitch = 44;
static float row_text(void) { return clampf(g_pitch * 0.62f, 15, UI_T_ROW + 2); }
static Rectangle row_mark(Rectangle box) { float h = fminf(box.height, g_pitch - 2); return (Rectangle){box.x, box.y + 8 + row_text() * 0.5f - h * 0.5f, box.width, h}; }

// A selectable row; returns true when picked (Enter, Space or click). The
// selected row carries the sliding mark; a disabled row is faint.
static bool row(Ui* u, int r, int* sel, float x, float y, float w, const char* s, bool enabled) {
    Rectangle box = {x - 16, y - 8, w, 38};
    bool hover = CheckCollisionPointRec(u->m, box);
    if (hover && u->moved && *sel != r) { *sel = r; cret_audio_ui(u->au, CRET_UI_MOVE); }
    bool on = *sel == r;
    if (on) cret_ui_select(row_mark(box));
    Color c = !enabled ? UI_FAINT : on ? UI_INK : UI_DIM;
    cret_ui_text(u->h, false, s, x, y, row_text(), c);
    bool pick = enabled && on && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || (u->click && hover));
    if (pick) cret_audio_ui(u->au, CRET_UI_CLICK);
    return pick;
}

// A value row: the name at x, the value from vx to the row's right end
// (vx + vw), "<" and ">" at its ends. A bar (0..1 values) runs between the
// arrows and takes clicks itself; its number sits left of ">".
static float value_x(float x, float w) { return x + w * 0.5f; }
static float value_w(float w) { return w * 0.5f - 28; }
static void bar_span(float x, float w, float* bx, float* bw) { *bx = value_x(x, w) + 28; *bw = value_w(w) - 112; }

// Left/right on the selected row, or a click or tap on the left or right
// half of its value (only on the arrows of a bar): -1, 0, +1. A click on the
// name only selects the row.
static int adjust(Ui* u, int r, int* sel, float x, float y, float w, bool bar) {
    int d = 0;
    if (*sel == r) {
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) d = -1;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || IsKeyPressed(KEY_ENTER)) d = 1;
    }
    int c = cret_ui_step_hit(u->m, u->click, value_x(x, w), y, value_w(w), 20, bar);
    if (c) { d = c; *sel = r; }
    if (d) cret_audio_ui(u->au, CRET_UI_MOVE);
    return d;
}

// How a value row shows its value.
enum { V_STEP, V_SLIDER, V_SWITCH };
// "Name ........ <  value  >"; a slider ("Name ...... <  ----o---  number  >")
// for 0..1 values (bar); an on/off switch and its word (on: bar > 0.5).
static void value_row(Ui* u, int r, int sel, float x, float y, float w, const char* name, const char* value, int kind, float bar) {
    Rectangle box = {x - 16, y - 8, w, 38};
    bool on = sel == r;
    if (on) cret_ui_select(box);
    Color c = on ? UI_INK : UI_DIM;
    cret_ui_text(u->h, false, name, x, y, UI_T_ROW, c);
    float vx = value_x(x, w), vw = value_w(w);
    if (kind == V_SLIDER) {
        float bx, bw; bar_span(x, w, &bx, &bw);
        cret_ui_slider(bx, y, bw, UI_T_ROW, bar, on);
        cret_ui_text(u->h, false, value, bx + bw + 16, y + 2, UI_T_BODY + 1, c);
        cret_ui_stepper(u->h, NULL, vx, y, vw, UI_T_ROW, c, u->m);
    } else if (kind == V_SWITCH) {
        float tw = UI_T_ROW * 1.62f, ww = cret_ui_text_w(u->h, false, value, UI_T_BODY + 1), x0 = vx + (vw - tw - UI_GAP - ww) * 0.5f;
        cret_ui_toggle(x0, y, UI_T_ROW, bar > 0.5f, on);
        cret_ui_text(u->h, false, value, x0 + tw + UI_GAP, y + 2, UI_T_BODY + 1, c);
    } else cret_ui_stepper(u->h, value, vx, y, vw, UI_T_ROW, c, u->m);
}

// Clicking or dragging on a value row's bar (or tapping it): the 0..1
// position under the pointer, or -1 (the bar is drawn by value_row at the same place).
static float bar_pick(Ui* u, float x, float y, float w) {
    float bx, bw; bar_span(x, w, &bx, &bw);
    Rectangle hit = {bx - 10, y - 8, bw + 20, 38};
    if (!(IsMouseButtonDown(MOUSE_BUTTON_LEFT) || u->tap) || !CheckCollisionPointRec(u->m, hit)) return -1;
    return clampf((u->m.x - bx) / bw, 0, 1);
}
// Sets *v from a bar position (lo..hi, snapped to step); true when it changed.
static bool bar_set(float pick, float* v, float lo, float hi, float step) {
    if (pick < 0) return false;
    float n = clampf(roundf((lo + pick * (hi - lo)) / step) * step, lo, hi);
    if (fabsf(n - *v) < step * 0.5f) return false;
    *v = n; return true;
}

// ---------------------------------------------------------------- settings page
enum { S_PRESET, S_SCALE, S_DISPLAY, S_RES, S_UI, S_SENS, S_INVERT, S_SHAKE, S_HITSTOP, S_VOL0, S_KEYS = S_VOL0 + CRET_AUDIO_BUSES, S_BACK, S_COUNT };
// Sections: their headings and first rows (the last ends at Key bindings).
#define S_GROUPS 4
static const char* const S_HEAD[S_GROUPS] = {"Display", "Controls", "Combat feel", "Sound"};
static const int S_FIRST[S_GROUPS + 1] = {S_PRESET, S_SENS, S_SHAKE, S_VOL0, S_KEYS};
#define S_HEAD_H 30.0f   // a heading's height
#define S_GROUP_GAP 10.0f

// Where every row and heading goes. One column when the window is tall
// enough; else (a phone held sideways) display and controls on the left,
// combat feel, sound, key bindings and Back on the right.
typedef struct { float x[S_COUNT], y[S_COUNT], hx[S_GROUPS], hy[S_GROUPS], w, rh, top, bottom; bool two; } SetLayout;
static SetLayout settings_layout(float sw, float sh) {
    SetLayout L; memset(&L, 0, sizeof L);
    float fixed = S_GROUPS * S_HEAD_H + (S_GROUPS - 1) * S_GROUP_GAP + 16, rows = S_COUNT;
    L.w = 640; L.rh = 44;
    float y0 = fmaxf(110, fminf(sh * 0.2f, sh - 70 - (fixed + rows * L.rh)));
    if (y0 + fixed + rows * L.rh + 70 > sh) L.rh = (sh - 70 - y0 - fixed) / rows;
    L.two = L.rh < 38;
    if (!L.two) {
        float x = sw * 0.5f - L.w * 0.5f + 16, y = y0;
        for (int g = 0; g < S_GROUPS; g++) {
            L.hx[g] = x; L.hy[g] = y; y += S_HEAD_H;
            for (int r = S_FIRST[g]; r < S_FIRST[g + 1]; r++) { L.x[r] = x; L.y[r] = y; y += L.rh; }
            if (g + 1 < S_GROUPS) y += S_GROUP_GAP;
        }
        y += 16; for (int r = S_KEYS; r < S_COUNT; r++) { L.x[r] = x; L.y[r] = y; y += L.rh; }
        L.top = y0; L.bottom = y; return L;
    }
    y0 = fmaxf(sh * 0.12f, 96); L.w = fminf(600, (sw - 120) * 0.5f);
    L.rh = fminf(44, fmaxf(34, (sh - y0 - 70 - (2 * S_HEAD_H + S_GROUP_GAP + 16)) / 8));
    for (int col = 0; col < 2; col++) {
        float x = col ? sw * 0.5f + 36 : sw * 0.5f - L.w - 4, y = y0;
        for (int g = col * 2; g < col * 2 + 2; g++) {
            L.hx[g] = x; L.hy[g] = y; y += S_HEAD_H;
            for (int r = S_FIRST[g]; r < S_FIRST[g + 1]; r++) { L.x[r] = x; L.y[r] = y; y += L.rh; }
            if (g == col * 2) y += S_GROUP_GAP;
        }
        if (col) { y += 16; for (int r = S_KEYS; r < S_COUNT; r++) { L.x[r] = x; L.y[r] = y; y += L.rh; } }
        if (y > L.bottom) L.bottom = y;
    }
    L.top = y0; return L;
}

static CretMenuAction settings_page(CretMenu* m, Ui* u, CretSettings* s) {
    float sw = cret_ui_w(), sh = cret_ui_h();
    cret_ui_fade(screen_fade(m->page == 2 ? 2 : 1));
    DrawRectangle(0, 0, (int)sw + 1, (int)sh + 1, cret_ui_fa((Color){8, 8, 7, 175}));
    if (m->page == 2) {   // key bindings (read-only): the same list as the F1 help
        const char* const* K; const char* const* W; int n = cret_ui_keys(&K, &W);
        float y0 = fmaxf(96, sh * 0.16f), rh = 32;
        bool two = y0 + 14 + n * rh + 70 > sh; int per = two ? (n + 1) / 2 : n;
        float colw = two ? fminf(560, (sw - 80) * 0.5f - 20) : 560;
        float x0 = two ? sw * 0.5f - colw - 20 : sw * 0.5f - 320;
        cret_ui_text(u->h, true, "Key bindings", x0, y0 - 82, UI_T_HEAD, UI_INK);
        for (int i = 0; i < n; i++) {
            float x = x0 + (i >= per ? colw + 40 : 0), y = y0 + 14 + (i % per) * rh;
            cret_ui_keycap(u->h, K[i], x, y);
            cret_ui_text(u->h, false, W[i], x + 132, y + 2, UI_T_BODY + 2, UI_DIM);
        }
        if (cret_ui_input() == CRET_UI_TOUCH) { const char* t = "Tap to go back"; cret_ui_text(u->h, false, t, sw * 0.5f - cret_ui_text_w(u->h, false, t, UI_T_SMALL + 1) * 0.5f, sh - 48, UI_T_SMALL + 1, UI_DIM); }
        else { const char* keys[1] = {"Esc"}; const char* verbs[1] = {"Back"}; cret_ui_footer(u->h, keys, verbs, 1, sw * 0.5f, sh - 52, true); }
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || u->click) { m->page = 1; cret_audio_ui(u->au, CRET_UI_CLICK); }
        return MENU_NONE;
    }
    SetLayout L = settings_layout(sw, sh);
    float w = L.w;
    float px0 = L.two ? sw * 0.5f - w - 28 : sw * 0.5f - w * 0.5f - 12, pw = L.two ? 2 * w + 80 : w + 24;
    cret_ui_panel((Rectangle){px0, L.top - 104, pw, L.bottom - L.top + 116});
    cret_ui_text(u->h, true, "Settings", L.hx[0], L.top - 86, UI_T_HEAD, UI_INK);
    for (int g = 0; g < S_GROUPS; g++) cret_ui_heading(u->h, S_HEAD[g], L.hx[g] - 4, L.hy[g] + 4, w - 24);
    nav(u, &m->set_sel, S_COUNT);
#ifdef __EMSCRIPTEN__
    s->fullscreen = web_is_fullscreen();   // the browser's own (F11 aside, Esc held leaves it): show what is
#endif
    CretMenuAction act = MENU_NONE; char v[64];
    static const char* PRESET[4] = {"Low", "Medium", "High", "Ultra"};
    static const char* VOL[CRET_AUDIO_BUSES] = {"Master volume", "Ambience", "Effects", "Interface"};
    for (int r = 0; r < S_COUNT; r++) {
        float x = L.x[r], y = L.y[r];
        Rectangle box = {x - 16, y - 8, w, 38};
        if (CheckCollisionPointRec(u->m, box) && u->moved && m->set_sel != r) m->set_sel = r;
        bool has_bar = r == S_SCALE || r == S_UI || r == S_SENS || r == S_SHAKE || r == S_HITSTOP || (r >= S_VOL0 && r < S_KEYS);
        int d = r == S_KEYS || r == S_BACK ? 0 : adjust(u, r, &m->set_sel, x, y, w, has_bar);
        float pick = has_bar ? bar_pick(u, x, y, w) : -1;
        if (pick >= 0) { d = 0; m->set_sel = r; }
        switch (r) {
        case S_PRESET:
            // A quality the player picks wins: the automatic stepping (web) stops for good.
            if (d) { s->preset = (s->preset + 4 + d) % 4; s->render_scale = s->preset == CRET_PRESET_LOW ? 0.7f : s->preset == CRET_PRESET_MEDIUM ? 0.8f : 1.0f; s->auto_quality = false; act = MENU_APPLY; }
            value_row(u, r, m->set_sel, x, y, w, "Graphics quality", PRESET[s->preset], V_STEP, 0); break;
        case S_SCALE:
            if (d) { s->render_scale = clampf(roundf((s->render_scale + d * 0.05f) * 20) / 20, 0.5f, 1.0f); act = MENU_APPLY; }
            if (u->click && bar_set(pick, &s->render_scale, 0.5f, 1.0f, 0.05f)) act = MENU_APPLY;   // a click, not a drag: each change reallocates targets
            if (act == MENU_APPLY) s->auto_quality = false;
            snprintf(v, sizeof v, "%d%%", (int)roundf(s->render_scale * 100));
            value_row(u, r, m->set_sel, x, y, w, "Render resolution", v, V_SLIDER, (s->render_scale - 0.5f) / 0.5f); break;
        case S_DISPLAY:
            if (d) { s->fullscreen = !s->fullscreen; act = MENU_APPLY; }
            value_row(u, r, m->set_sel, x, y, w, "Display", s->fullscreen ? "Fullscreen" : "Window", V_STEP, 0); break;
        case S_RES: {
#ifdef __EMSCRIPTEN__
            snprintf(v, sizeof v, s->fullscreen ? "Screen" : "Browser window");   // the page sizes the canvas
#else
            int n = cret_settings_resolutions();
            if (d && !s->fullscreen) { s->res = (s->res + n + d) % n; act = MENU_APPLY; }
            int rw, rhh; cret_settings_resolution(s->res, &rw, &rhh);
            if (s->fullscreen) snprintf(v, sizeof v, "Desktop"); else snprintf(v, sizeof v, "%d x %d", rw, rhh);
#endif
            value_row(u, r, m->set_sel, x, y, w, "Window size", v, V_STEP, 0); break; }
        case S_UI:
            if (d) { s->ui_scale = clampf(roundf((s->ui_scale + d * 0.1f) * 10) / 10, 0.5f, 2.0f); act = MENU_APPLY; }
            if (u->click && bar_set(pick, &s->ui_scale, 0.5f, 2.0f, 0.1f)) act = MENU_APPLY;
            snprintf(v, sizeof v, "%d%%", (int)roundf(s->ui_scale * 100));
            value_row(u, r, m->set_sel, x, y, w, "Interface size", v, V_SLIDER, (s->ui_scale - 0.5f) / 1.5f); break;
        case S_SENS:
            if (d) { s->mouse_sens = clampf(roundf((s->mouse_sens + d * 0.05f) * 20) / 20, 0.05f, 3.0f); act = MENU_APPLY; }
            if (bar_set(pick, &s->mouse_sens, 0.05f, 3.0f, 0.05f)) act = MENU_APPLY;
            snprintf(v, sizeof v, "%.2f", s->mouse_sens);
            value_row(u, r, m->set_sel, x, y, w, "Mouse sensitivity", v, V_SLIDER, (s->mouse_sens - 0.05f) / 2.95f); break;
        case S_INVERT:
            if (d) { s->invert_y = !s->invert_y; act = MENU_APPLY; }
            value_row(u, r, m->set_sel, x, y, w, "Invert mouse Y", s->invert_y ? "On" : "Off", V_SWITCH, s->invert_y); break;
        case S_SHAKE: case S_HITSTOP: {   // combat feedback: off, then 25% steps
            float* q = r == S_SHAKE ? &s->shake : &s->hitstop;
            if (d) { *q = clampf(roundf((*q + d * 0.25f) * 4) / 4, 0, 1); act = MENU_APPLY; }
            if (bar_set(pick, q, 0, 1, 0.25f)) act = MENU_APPLY;
            if (*q <= 0) snprintf(v, sizeof v, "Off"); else snprintf(v, sizeof v, "%d%%", (int)roundf(*q * 100));
            value_row(u, r, m->set_sel, x, y, w, r == S_SHAKE ? "Camera shake" : "Hit-stop", v, V_SLIDER, *q); break; }
        case S_KEYS:
            if (row(u, r, &m->set_sel, x, y, w, "Key bindings", true)) m->page = 2;
            break;
        case S_BACK:
            if (row(u, r, &m->set_sel, x, y, w, "Back", true)) act = MENU_BACK;
            break;
        default: {
            int b = r - S_VOL0;
            if (d) { s->volume[b] = clampf(roundf((s->volume[b] + d * 0.05f) * 20) / 20, 0, 1); act = MENU_APPLY; }
            if (bar_set(pick, &s->volume[b], 0, 1, 0.05f)) act = MENU_APPLY;
            snprintf(v, sizeof v, "%d%%", (int)roundf(s->volume[b] * 100));
            value_row(u, r, m->set_sel, x, y, w, VOL[b], v, V_SLIDER, s->volume[b]); break; }
        }
    }
    controls(u->h, sw * 0.5f, sh - 52, true, "Back", "Click a row, an arrow or a slider", "Tap a slider, or the < or > of a value  -  Back to return");
    if (IsKeyPressed(KEY_ESCAPE)) act = MENU_BACK;
#ifdef __EMSCRIPTEN__
    if (act == MENU_APPLY) cret_settings_save(s);   // kept at once: a reload or a closed tab never says Back
#endif
    if (act == MENU_BACK) cret_settings_save(s);
    return act;
}

CretMenuAction cret_menu_settings(CretMenu* m, CretHud* h, CretAudio* au, CretSettings* s) {
    cret_ui_begin(h);
    Ui u = ui_frame(h, au);
    if (m->page != 2) m->page = 1;
    CretMenuAction a = settings_page(m, &u, s);
    if (a == MENU_BACK) m->page = 0;
    cret_ui_end();
    return a;
}

// ---------------------------------------------------------------- title
void cret_menu_init(CretMenu* m, int species) {
    memset(m, 0, sizeof *m); m->species = species; snprintf(m->address, sizeof m->address, "127.0.0.1");
}

static void pick_character(CretMenu* m) {
    if (m->char_sel < m->nnames) {
        snprintf(m->character, sizeof m->character, "%s", m->names[m->char_sel]);
        CretCharacter c; if (cret_character_load(m->character, &c)) m->species = c.species;
    } else snprintf(m->character, sizeof m->character, "%s", m->new_name);
}

// A name for a new character without a keyboard: the species' name and the
// first number no saved character has ("Velociraptor2").
static void name_new(CretMenu* m) {
    char base[24]; int n = 0;
    for (const char* p = CRET_SPECIES_TABLE[m->species].name; *p && n < 16; p++) if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) base[n++] = *p;
    base[n] = 0;
    for (int k = 1; k < 1000; k++) {
        snprintf(m->new_name, sizeof m->new_name, "%.16s%d", base, k % 1000);
        bool taken = false; for (int i = 0; i < m->nnames; i++) taken |= !strcmp(m->names[i], m->new_name);
        if (!taken) break;
    }
    pick_character(m);
}

void cret_menu_characters(CretMenu* m, const char* last) {
    m->nnames = cret_character_list(m->names, 32); m->char_sel = m->nnames;
    for (int k = 0; k < m->nnames; k++) if (last && !strcmp(m->names[k], last)) m->char_sel = k;
    pick_character(m);
}

static const char* const FIGHT_SIZE[3] = {"hatchling", "juvenile", "adult"};
static const float FIGHT_GROWTH[3] = {0.35f, 0.65f, 1.0f};
static const char* const FIGHT_BEH[4] = {"fight you", "game AI", "run away", "stand still"};
static const char* const FIGHT_BEH_KEY[4] = {"hostile", "bot", "passive", "dummy"};
void cret_menu_fight_ini(const CretMenu* m, char* out, int n) {
    const CretSpecies *Y = &CRET_SPECIES_TABLE[m->ar_you], *F = &CRET_SPECIES_TABLE[m->ar_foe];
    float gap = 10 + 0.6f * (Y->length * cret_growth_scale(FIGHT_GROWTH[m->ar_you_size]) + F->length * cret_growth_scale(FIGHT_GROWTH[m->ar_foe_size]));
    snprintf(out, n,
             "[scenario]\ntitle = You: %s %s against %d %s %s (%s)\nseconds = 1800\nplace = open\ncamera = follow\nend = time\nyou = on\n"
             "[player]\nspecies = %s\ngrowth = %.2f\nat = 0 0\nfacing = 0\n"
             "[actor foe]\nspecies = %s\ncount = %d\nspread = %.0f\ngrowth = %.2f\nat = %.0f 0\nfacing = 180\nbehaviour = %s\n",
             FIGHT_SIZE[m->ar_you_size], Y->name, m->ar_n, FIGHT_SIZE[m->ar_foe_size], F->name, FIGHT_BEH[m->ar_beh],
             Y->asset, FIGHT_GROWTH[m->ar_you_size], F->asset, m->ar_n, 3 + 2.5f * m->ar_n, FIGHT_GROWTH[m->ar_foe_size], gap, FIGHT_BEH_KEY[m->ar_beh]);
}

int cret_menu_scenarios(CretMenu* m, CretHud* h, CretAudio* au, const char (*titles)[96], int n, int current) {
    cret_ui_begin(h);
    Ui u = ui_frame(h, au);
    float sw = cret_ui_w(), sh = cret_ui_h();
    // Left: Resume, Restart, scenarios, Gallery, Quit. Right: your fight (7 rows).
    enum { F_YOU, F_YOU_SIZE, F_FOE, F_N, F_FOE_SIZE, F_BEH, F_GO, F_ROWS };
    int rows = n + 4, total = rows + F_ROWS, act = CRET_SCNMENU_NONE;
    if (m->ar_n < 1) { m->ar_you = CRET_SP_RAPTOR; m->ar_you_size = 2; m->ar_foe = CRET_SP_RAPTOR; m->ar_n = 1; m->ar_foe_size = 2; m->ar_beh = 0; }
    cret_ui_fade(screen_fade(20));
    g_pitch = fminf(44, (sh - 260) / rows);
    DrawRectangle(0, 0, (int)sw + 1, (int)sh + 1, cret_ui_fa((Color){0, 0, 0, 120}));
    DrawRectangleGradientH(0, 0, (int)sw + 1, (int)sh + 1, cret_ui_fa((Color){6, 6, 5, 200}), cret_ui_fa((Color){6, 6, 5, 110}));
    float rh = fminf(44, (sh - 260) / rows), x = 100, y0 = fmaxf(150, sh * 0.5f - rows * rh * 0.5f);
    float fx = fmaxf(x + 640, sw * 0.52f), fw = fminf(560, sw - fx - 40), fy = y0, lw = fminf(560, fx - x - 40);
    cret_ui_panel((Rectangle){x - 36, y0 - 120, lw + 56, rows * rh + 28 + 160});
    cret_ui_panel((Rectangle){fx - 36, y0 - 120, fw + 56, F_ROWS * 46 + 16 + 148});
    cret_ui_text(h, true, "Watch", x, y0 - 100, UI_T_HEAD, UI_INK);
    cret_ui_text(h, false, "Scripted tests that play themselves. Press M in one to take over.", x + 2, y0 - 46, UI_T_BODY, UI_DIM);
    nav(&u, &m->sel, total);
    if (m->sel < 0 || m->sel >= total) m->sel = 0;
    // Your fight: you play it from the start.
    cret_ui_text(h, true, "Play a fight", fx, y0 - 100, UI_T_HEAD, UI_INK);
    cret_ui_text(h, false, "You play it. Pick both sides (< and >, or left / right), then Start.", fx + 2, y0 - 46, UI_T_BODY, UI_DIM);
    for (int r = 0; r < F_ROWS; r++) {
        int R = rows + r; float y = fy + r * 46 + (r == F_GO ? 16 : 0);
        Rectangle box = {fx - 16, y - 8, fw, 38};
        if (CheckCollisionPointRec(u.m, box) && u.moved && m->sel != R) { m->sel = R; cret_audio_ui(au, CRET_UI_MOVE); }
        if (r == F_GO) {
            if (row(&u, R, &m->sel, fx, y, fw, "Start the fight  (you play it)", true)) act = CRET_SCNMENU_FIGHT;
            continue;
        }
        int d = adjust(&u, R, &m->sel, fx, y, fw, false);
        if (r == F_YOU) m->ar_you = (m->ar_you + CRET_SPECIES + d) % CRET_SPECIES;
        if (r == F_FOE) m->ar_foe = (m->ar_foe + CRET_SPECIES + d) % CRET_SPECIES;
        if (r == F_YOU_SIZE) m->ar_you_size = (m->ar_you_size + 3 + d) % 3;
        if (r == F_FOE_SIZE) m->ar_foe_size = (m->ar_foe_size + 3 + d) % 3;
        if (r == F_N) m->ar_n = 1 + (m->ar_n - 1 + 8 + d) % 8;
        if (r == F_BEH) m->ar_beh = (m->ar_beh + 4 + d) % 4;
        static const char* const NAME[F_ROWS] = {"You", "Your size", "Against", "How many", "Their size", "They"};
        char v[64];
        if (r == F_YOU) snprintf(v, sizeof v, "%s", CRET_SPECIES_TABLE[m->ar_you].name);
        else if (r == F_YOU_SIZE) snprintf(v, sizeof v, "%s", FIGHT_SIZE[m->ar_you_size]);
        else if (r == F_FOE) snprintf(v, sizeof v, "%s", CRET_SPECIES_TABLE[m->ar_foe].name);
        else if (r == F_N) snprintf(v, sizeof v, "%d", m->ar_n);
        else if (r == F_FOE_SIZE) snprintf(v, sizeof v, "%s", FIGHT_SIZE[m->ar_foe_size]);
        else snprintf(v, sizeof v, "%s", FIGHT_BEH[m->ar_beh]);
        value_row(&u, R, m->sel, fx, y, fw, NAME[r], v, V_STEP, 0);
    }
    for (int r = 0; r < rows; r++) {
        char label[128]; int k = r - 2;
        if (r == 0) snprintf(label, sizeof label, "Resume");
        else if (r == 1) snprintf(label, sizeof label, "Restart");
        else if (r == rows - 2) snprintf(label, sizeof label, "Gallery  (every tree, prop and dinosaur)");
        else if (r == rows - 1) snprintf(label, sizeof label, "Quit");
        else snprintf(label, sizeof label, k == current ? "%s   (playing)" : "%s", titles[k]);
        float y = y0 + r * rh + (r >= 2 ? 14 : 0) + (r >= rows - 2 ? 14 : 0);
        if (!row(&u, r, &m->sel, x, y, lw, label, true)) continue;
        act = r == 0 ? CRET_SCNMENU_RESUME : r == 1 ? CRET_SCNMENU_RESTART : r == rows - 2 ? CRET_SCNMENU_GALLERY : r == rows - 1 ? CRET_SCNMENU_QUIT : k;
    }
    if (IsKeyPressed(KEY_ESCAPE)) act = CRET_SCNMENU_RESUME;
    controls(h, x, y0 + rows * rh + 44, false, "Resume", "Click a row, or the < or > of a value", "Tap a row, or the < or > of a value");
    g_pitch = 44;
    cret_ui_end();
    return act;
}

// Pages: 0 title, 1 settings, 2 key bindings, 3 credits.
CretMenuAction cret_menu_title(CretMenu* m, CretHud* h, CretAudio* au, CretSettings* s, float dt) {
    m->t += dt;
    cret_ui_begin(h);
    Ui u = ui_frame(h, au);
    float sw = cret_ui_w(), sh = cret_ui_h();
    CretMenuAction act = MENU_NONE;
    if (m->page == 1 || m->page == 2) {
        CretMenuAction a = settings_page(m, &u, s);
        if (a == MENU_BACK) { m->page = 0; a = MENU_NONE; }
        cret_ui_end(); return a;
    }
    if (m->page == 3) {
        cret_ui_fade(screen_fade(3));
        cret_ui_credits(h);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || u.click) { m->page = 0; cret_audio_ui(au, CRET_UI_CLICK); }
        cret_ui_end(); return MENU_NONE;
    }
    // The page fades in with the flyover the first time, and over UI_FADE from another page.
    cret_ui_fade(clampf(m->t / 1.5f, 0, 1) * screen_fade(0));
    // A soft vignette on the left keeps the text legible over the flyover.
    DrawRectangleGradientH(0, 0, (int)(sw * 0.6f), (int)sh + 1, cret_ui_fa((Color){6, 6, 5, 210}), cret_ui_fa((Color){6, 6, 5, 0}));
    enum { T_CHAR, T_PLAY, T_HOST, T_JOIN, T_SCENARIOS, T_GALLERY, T_SETTINGS, T_CREDITS, T_QUIT, T_COUNT };
    float x = 120, y0 = sh * 0.36f, rh = 48, ty = y0 - 160, tsize = UI_T_HERO;
    // Short windows (a phone held sideways): a smaller title and closer rows, all on screen.
    if (y0 + T_COUNT * rh + 70 > sh) { y0 = 128; rh = fmaxf(34, (sh - y0 - 60) / T_COUNT); ty = 6; tsize = 50; x = 72; }
    // The name in spaced capitals, a short amber rule, and the line under it.
    cret_ui_title(h, "Cretaceous", x - 2, ty, tsize, UI_INK);
    DrawRectangleRec((Rectangle){x, ty + tsize * 1.06f, 56, 2}, cret_ui_fa(UI_ACCENT));
    cret_ui_text(h, false, "Survive the late Cretaceous: graze, hunt, grow and live with your herd or pack.", x, ty + tsize * 1.06f + 14, tsize < UI_T_HERO ? UI_T_BODY : UI_T_BODY + 2, UI_DIM);
    bool touch = cret_touch_active();
    if (!m->editing && !m->naming) nav(&u, &m->sel, T_COUNT);
    char join[128], who[96], chv[48];
    // Character: a saved one (left / right), or a new one (Enter types its name).
    if (m->sel == T_CHAR && !m->naming) {
        int d = 0;
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) d = -1;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) d = 1;
        if (d) { m->char_sel = (m->char_sel + m->nnames + 1 + d) % (m->nnames + 1); pick_character(m); cret_audio_ui(au, CRET_UI_MOVE); }
    }
    if (m->naming) snprintf(who, sizeof who, "Name:  %s_", m->new_name); else snprintf(who, sizeof who, "Character");
    if (m->char_sel < m->nnames) snprintf(chv, sizeof chv, "%s", m->names[m->char_sel]);
    else if (m->new_name[0]) snprintf(chv, sizeof chv, "%s (new)", m->new_name);
    else snprintf(chv, sizeof chv, "New");
    snprintf(join, sizeof join, m->editing ? "Join  %s_" : "Join  %s", m->address);
#ifdef __EMSCRIPTEN__
    // The browser build has no networking (net_web.c): Host and Join stay listed but cannot be picked.
    const bool net_ok = false; snprintf(join, sizeof join, "Join a game  (desktop version only)");
    const char* host = "Host a game  (desktop version only)";
#else
    const bool net_ok = true; const char* host = "Host a game";
#endif
    const char* rows[T_COUNT] = {who, "Play  as", host, join, "Fights and scenarios", "Gallery", "Settings", "Credits", "Quit"};
    // The character and species values step: "<" and ">" either side, right of the label, always shown.
    float vx = x + fmaxf(cret_ui_text_w(h, false, "Character", 22), cret_ui_text_w(h, false, "Play  as", 22)) + 32, vw = x + 428 - vx;
    for (int r = 0; r < T_COUNT; r++) {
        float y = y0 + r * rh;
        if (r == T_PLAY && m->sel == T_PLAY && !m->editing) {
            int d = 0;
            if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) d = -1;
            if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) d = 1;
            if (d) { m->species = (m->species + CRET_SPECIES + d) % CRET_SPECIES; cret_audio_ui(au, CRET_UI_MOVE); }
        }
        if (r == T_CHAR && m->naming) {
            int c;
            while ((c = GetCharPressed()) > 0) {
                size_t n = strlen(m->new_name);
                if (n + 1 < sizeof m->new_name && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_'))
                    { m->new_name[n] = (char)c; m->new_name[n + 1] = 0; }
            }
            if (IsKeyPressed(KEY_BACKSPACE) && m->new_name[0]) m->new_name[strlen(m->new_name) - 1] = 0;
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
                // The Enter that ends the name stays with this row: the mouse may rest on another, which it would pick.
                m->naming = false; m->sel = T_CHAR; cret_audio_ui(au, CRET_UI_CLICK);
                for (int k = 0; k < m->nnames; k++) if (!strcmp(m->names[k], m->new_name)) m->char_sel = k;   // an existing name: that one
                pick_character(m);
            }
            row(&u, r, &m->sel, x, y, 460, rows[r], true);
            continue;
        }
        // A click or tap on the value's left or right half steps it; only one on the label picks the row.
        bool steps = r == T_CHAR || r == T_PLAY;
        int step = steps && !m->editing ? cret_ui_step_hit(u.m, u.click, vx, y, vw, 22, false) : 0;
        if (step) {
            if (r == T_PLAY) m->species = (m->species + CRET_SPECIES + step) % CRET_SPECIES;
            else { m->char_sel = (m->char_sel + m->nnames + 1 + step) % (m->nnames + 1); pick_character(m); }
            m->sel = r; cret_audio_ui(au, CRET_UI_MOVE);
        }
        Ui lab = u; if (steps && u.m.x >= vx - 12) lab.click = false;
        bool picked = row(&lab, r, &m->sel, x, y, 460, rows[r], net_ok || (r != T_HOST && r != T_JOIN));
        if (steps) cret_ui_stepper(h, r == T_PLAY ? CRET_SPECIES_TABLE[m->species].name : chv, vx, y, vw, 22, m->sel == r ? UI_INK : UI_DIM, u.m);
        if (picked && !m->editing) {
            // A new character is named by typing; without a keyboard it gets the species' name and a number.
            if (r == T_CHAR) { if (m->char_sel != m->nnames) m->sel = T_PLAY; else if (touch) { name_new(m); m->sel = T_PLAY; } else m->naming = true; }
            else if (r == T_PLAY) act = MENU_PLAY;
            else if (r == T_HOST) act = MENU_HOST;
            else if (r == T_JOIN) m->editing = true;
            else if (r == T_SCENARIOS) act = MENU_SCENARIOS;
            else if (r == T_GALLERY) act = MENU_GALLERY;
            else if (r == T_SETTINGS) { m->page = 1; m->set_sel = 0; }
            else if (r == T_CREDITS) m->page = 3;
            else if (r == T_QUIT) act = MENU_QUIT;
        } else if (r == T_JOIN && m->editing) {
            int c;
            while ((c = GetCharPressed()) > 0) {
                size_t n = strlen(m->address);
                if (n + 1 < sizeof m->address && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == ':'))
                    { m->address[n] = (char)c; m->address[n + 1] = 0; }
            }
            if (IsKeyPressed(KEY_BACKSPACE) && m->address[0]) m->address[strlen(m->address) - 1] = 0;
            if (IsKeyPressed(KEY_ENTER) && m->address[0]) { m->editing = false; act = MENU_JOIN; cret_audio_ui(au, CRET_UI_CLICK); }
            if (IsKeyPressed(KEY_ESCAPE)) m->editing = false;
        }
    }
    // What the keys, the mouse or a finger do here; typing says what it types.
    float fy = y0 + T_COUNT * rh + 14;
    if (m->editing || m->naming) {
        const char* keys[2] = {"Enter", "Esc"}; const char* verbs[2] = {m->editing ? "Join" : "Keep the name", "Cancel"};
        const char* what = m->editing ? "Type the host's address" : "Type a name (letters, digits, - and _)";
        cret_ui_text(h, false, what, x, fy + 3, UI_T_SMALL + 1, UI_DIM);
        cret_ui_footer(h, keys, verbs, m->editing ? 2 : 1, x + cret_ui_text_w(h, false, what, UI_T_SMALL + 1) + 24, fy, false);
    } else if (!touch && m->sel == T_CHAR && cret_ui_input() == CRET_UI_KEYS) {
        const char* keys[3] = {"Up Down", "Left Right", "Enter"}; const char* verbs[3] = {"Choose", "Saved characters", m->char_sel == m->nnames ? "Name a new one" : "Select"};
        cret_ui_footer(h, keys, verbs, 3, x, fy, false);
    } else controls(h, x, fy, false, NULL, "Click a row, or the < or > of the character or species", "Tap a row to choose it  -  tap < or > to change the character or species");
    if (m->note[0]) cret_ui_text(h, false, m->note, x, fy + 34, UI_T_BODY + 1, UI_ACCENT);
    const char* by = "raylib - Box3D - ENet - PufferLib";
    cret_ui_text(h, false, by, sw - cret_ui_text_w(h, false, by, UI_T_SMALL) - 28, sh - 36, UI_T_SMALL, UI_FAINT);
    cret_ui_end();
    return act;
}
