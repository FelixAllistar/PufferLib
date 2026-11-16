// The Look studio (studio.h): the panel, its widgets and the screenshot.
// Compiled from menu.c (build.sh lists the player sources outside ocean/cretaceous).
#include "studio.h"
#include "ui_style.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define ST_ROW 30.0f      // a slider row
#define ST_TEXT 14.0f     // row text
#define ST_HEAD 30.0f     // a section heading
#define ST_SCENE 100      // scene sliders' ids after the knobs (active, hover)
enum { SC_TIME, SC_CLOUDS, SC_WEATHER, SC_FREEZE, SC_COUNT };
enum { SEC_LOOKS = LKS_COUNT, SEC_SCENE = LKS_COUNT + 1 };

static float st_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static void st_note(CretStudio* s, const char* msg) { snprintf(s->note, sizeof s->note, "%s", msg); s->note_t = GetTime(); }
static void st_changed(CretStudio* s) { s->dirty = true; s->dirty_t = GetTime(); }

void cret_studio_init(CretStudio* s) {
    memset(s, 0, sizeof *s); s->split_x = 0.33f; s->active = -1; s->last_click = -1; s->hover = -1;
    for (int k = 0; k < LKS_COUNT; k++) s->collapsed[k] = k != LKS_TONE && k != LKS_COLOUR;   // the two sections that matter most start open
}

static void st_list(CretStudio* s) { s->nnames = cret_look_list(s->names, 64); }

void cret_studio_open(CretStudio* s, CretView* v) {
    if (!s->have_before) { s->before = v->look; s->have_before = true; }
    s->open = true; s->fresh = true; s->naming = 0; s->active = -1; st_list(s);
}
void cret_studio_close(CretStudio* s, CretView* v) {
    if (s->dirty) { cret_look_save_current(&v->look); s->dirty = false; }
    s->open = false; s->naming = 0; s->active = -1; s->hold_before = false;
    v->split = -1; v->show_before = false;
}
void cret_studio_after_tick(const CretStudio* s, CretSim* sim) { if (s->open && s->freeze && sim) sim->time_of_day = s->freeze_tod; }

Vector2 cret_studio_orbit(CretStudio* s) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) s->orbit_ok = !s->over_panel;
    if (!s->open || !s->orbit_ok || !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) return (Vector2){0, 0};
    return GetMouseDelta();
}

// ---------------------------------------------------------------- the screenshot
void cret_studio_take_shot(CretStudio* s, const CretView* v) {
    s->shot = false;
    MakeDirectory("screenshots");
    char stamp[32]; time_t now = time(NULL); struct tm* t = localtime(&now);
    if (t) strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", t); else snprintf(stamp, sizeof stamp, "%ld", (long)now);
    char png[160], ini[160]; snprintf(png, sizeof png, "screenshots/look_%s.png", stamp); snprintf(ini, sizeof ini, "screenshots/look_%s.ini", stamp);
    Image im = LoadImageFromScreen();   // the frame as rendered: no panel or HUD drawn yet
    bool ok = ExportImage(im, png); UnloadImage(im);
    ok &= cret_look_save_file(&v->look, ini);
    char msg[200]; snprintf(msg, sizeof msg, ok ? "Saved %s and its look" : "Could not write %s", png); st_note(s, msg);
}

// ---------------------------------------------------------------- widgets
typedef struct { CretHud* h; Vector2 m; bool click, rclick, down, dbl; float x, w; } St;

static bool st_hit(const St* u, Rectangle r) { return CheckCollisionPointRec(u->m, r); }
// A text button; returns true when clicked.
static bool st_button(St* u, const char* label, float x, float y, float* w_out, bool on, bool enabled) {
    float w = cret_ui_text_w(u->h, false, label, ST_TEXT) + 18; if (w_out) *w_out = w;
    Rectangle r = {x, y, w, 24};
    bool hov = enabled && st_hit(u, r);
    DrawRectangleRounded(r, 0.35f, 6, cret_ui_fa(on ? (Color){232, 186, 98, 70} : hov ? (Color){255, 248, 230, 34} : (Color){255, 248, 230, 16}));
    DrawRectangleRoundedLinesEx(r, 0.35f, 6, 1, cret_ui_fa(on ? UI_ACCENT : (Color){255, 248, 230, 40}));
    cret_ui_text(u->h, false, label, x + 9, y + 5, ST_TEXT, !enabled ? UI_FAINT : on || hov ? UI_INK : UI_DIM);
    return enabled && hov && u->click;
}

// A slider row: the name, a bar with the default marked, the value. Dragging sets it; a double click or a
// right click resets it. Returns true when the value changed.
static bool st_slider(CretStudio* s, St* u, int id, float y, const char* name, char* value_txt, float* v, float lo, float hi, float def, float step, bool scene_mark) {
    float x = u->x, w = u->w, bx = x + w * 0.46f, bw = w * 0.54f - 74, vx = bx + bw + 12;
    Rectangle row = {x - 6, y - 3, w + 12, ST_ROW - 2}, bar = {bx - 8, y, bw + 16, ST_ROW - 6};
    bool hov = st_hit(u, row) && !s->naming;
    if (hov) s->hover = id;
    bool changed = false;
    if (hov && (u->rclick || (u->dbl && st_hit(u, bar)))) { if (*v != def) { *v = def; changed = true; } s->active = -1; }
    else if (u->click && st_hit(u, bar) && !s->naming) s->active = id;
    if (s->active == id && u->down) {
        float t = st_clamp((u->m.x - bx) / bw, 0, 1), n = lo + t * (hi - lo);
        n = roundf(n / step) * step; n = st_clamp(n, lo, hi);
        if (fabsf(n - def) < step * 0.75f) n = def;   // the default is easy to land on
        if (n != *v) { *v = n; changed = true; }
    }
    bool lit = s->active == id || hov;
    if (hov) DrawRectangleRec(row, cret_ui_fa(UI_HILITE));
    cret_ui_text(u->h, false, name, x, y + 4, ST_TEXT, *v != def ? UI_INK : UI_DIM);
    if (scene_mark) DrawCircleV((Vector2){x - 3 + cret_ui_text_w(u->h, false, name, ST_TEXT) + 8, y + 12}, 2.2f, cret_ui_fa(UI_DIM));
    float dt = (def - lo) / (hi - lo);
    DrawRectangleRec((Rectangle){bx + bw * dt - 1, y + 3, 2, 16}, cret_ui_fa((Color){255, 248, 230, 90}));   // the default
    cret_ui_slider(bx, y + 2, bw, ST_TEXT, (*v - lo) / (hi - lo), lit);
    cret_ui_text(u->h, false, value_txt, vx, y + 4, ST_TEXT - 1, *v != def ? UI_ACCENT : UI_DIM);
    return changed;
}

// A row with a value that steps left and right ("<  AgX  >"): clicks on its halves step; right click resets.
static int st_stepper(CretStudio* s, St* u, int id, float y, const char* name, const char* value, bool is_default) {
    float x = u->x, w = u->w, vx = x + w * 0.46f, vw = w * 0.54f;
    Rectangle row = {x - 6, y - 3, w + 12, ST_ROW - 2};
    bool hov = st_hit(u, row) && !s->naming;
    if (hov) { s->hover = id; DrawRectangleRec(row, cret_ui_fa(UI_HILITE)); }
    cret_ui_text(u->h, false, name, x, y + 4, ST_TEXT, is_default ? UI_DIM : UI_INK);
    cret_ui_stepper(u->h, value, vx, y + 2, vw - 12, ST_TEXT, is_default ? UI_DIM : UI_ACCENT, u->m);
    if (hov && u->rclick) return 2;
    return hov && !s->naming ? cret_ui_step_hit(u->m, u->click, vx, y + 2, vw - 12, ST_TEXT, false) : 0;
}

// A collapsible heading with a Reset button; returns 1 when opened or closed, 2 on Reset.
static int st_heading(CretStudio* s, St* u, float y, const char* title, int sec, bool reset) {
    Rectangle r = {u->x - 6, y, u->w + 12, ST_HEAD - 4};
    bool hov = st_hit(u, r) && !s->naming;
    cret_ui_text(u->h, false, s->collapsed[sec] ? "+" : "-", u->x, y + 5, ST_TEXT + 2, hov ? UI_INK : UI_DIM);
    cret_ui_label(u->h, title, u->x + 18, y + 8, UI_T_SMALL, hov ? UI_INK : UI_DIM);
    DrawRectangleRec((Rectangle){u->x, y + ST_HEAD - 5, u->w, 1}, cret_ui_fa(UI_FAINT));
    float bw = 0;
    if (reset) {
        float tw = cret_ui_text_w(u->h, false, "Reset", ST_TEXT) + 18;
        if (st_button(u, "Reset", u->x + u->w - tw, y + 1, &bw, false, true)) return 2;
    }
    if (hov && u->click && u->m.x < u->x + u->w - bw - 6) return 1;
    return 0;
}

static void st_value(const CretLookKnob* K, float v, char* out, size_t n) {
    if (K->flags & LKF_CHOICE) snprintf(out, n, "%s", CRET_LOOK_TONEMAPS[(int)v]);
    else snprintf(out, n, K->fmt, v);
}

// ---------------------------------------------------------------- looks
static void st_load(CretStudio* s, CretView* v, const CretLook* l, const char* what) {
    v->look = *l; st_changed(s);
    char m[120]; snprintf(m, sizeof m, "%s %s", what, l->name); st_note(s, m);
}
static bool st_is_builtin(const char* name) {
    CretLook b; for (int i = 0; cret_look_builtin(i, &b); i++) if (!strcmp(b.name, name)) return true;
    return false;
}
// The look's name, "(edited)" when its sliders differ from the look of that name.
static bool st_edited(const CretView* v) {
    CretLook b;
    for (int i = 0; cret_look_builtin(i, &b); i++) if (!strcmp(b.name, v->look.name)) return !cret_look_equal(&b, &v->look);
    if (cret_look_load_named(v->look.name, &b)) return !cret_look_equal(&b, &v->look);
    return false;
}

// Typing a name: printable characters, Backspace, Enter (true) and Esc (cancels).
static bool st_type(CretStudio* s) {
    for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) {
        size_t n = strlen(s->name_buf);
        if (c >= 32 && c < 127 && n < sizeof s->name_buf - 1) { s->name_buf[n] = (char)c; s->name_buf[n + 1] = 0; }
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && s->name_buf[0]) s->name_buf[strlen(s->name_buf) - 1] = 0;
    if (IsKeyPressed(KEY_ESCAPE)) { s->naming = 0; return false; }
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

static float st_looks(CretStudio* s, St* u, CretView* v, float y) {
    CretLook b; int nb = 0;
    float colw = (u->w - 8) * 0.5f;
    // Shipped looks, then saved ones: two columns of buttons; the one in play lit.
    for (int i = 0; cret_look_builtin(i, &b); i++, nb++) {
        float x = u->x + (nb % 2) * (colw + 8), yy = y + (nb / 2) * 30;
        char lab[64]; snprintf(lab, sizeof lab, "%s", b.name);
        if (st_button(u, lab, x, yy, NULL, !strcmp(v->look.name, b.name), !s->naming)) st_load(s, v, &b, "Loaded");
    }
    for (int i = 0; i < s->nnames; i++, nb++) {
        if (st_is_builtin(s->names[i])) { nb--; continue; }
        float x = u->x + (nb % 2) * (colw + 8), yy = y + (nb / 2) * 30;
        if (st_button(u, s->names[i], x, yy, NULL, !strcmp(v->look.name, s->names[i]), !s->naming) && cret_look_load_named(s->names[i], &b)) st_load(s, v, &b, "Loaded");
    }
    y += ((nb + 1) / 2) * 30 + 6;
    bool builtin = st_is_builtin(v->look.name);
    float bw, x = u->x;
    if (s->naming) {
        cret_ui_text(u->h, false, s->naming == 1 ? "Name:" : "New name:", x, y + 5, ST_TEXT, UI_DIM);
        float tx = x + cret_ui_text_w(u->h, false, "New name:", ST_TEXT) + 8;
        Rectangle box = {tx, y, u->w - (tx - x) - 70, 24};
        DrawRectangleRounded(box, 0.3f, 6, cret_ui_fa((Color){0, 0, 0, 90})); DrawRectangleRoundedLinesEx(box, 0.3f, 6, 1, cret_ui_fa(UI_ACCENT));
        char shown[64]; snprintf(shown, sizeof shown, "%s%s", s->name_buf, fmodf((float)GetTime(), 1.0f) < 0.5f ? "|" : "");
        cret_ui_text(u->h, false, shown, tx + 6, y + 5, ST_TEXT, UI_INK);
        bool ok = st_type(s) | st_button(u, "OK", x + u->w - 60, y, &bw, false, true);
        if (s->naming && ok) {
            char nm[48]; snprintf(nm, sizeof nm, "%s", s->name_buf); cret_look_clean_name(nm);
            if (st_is_builtin(nm)) st_note(s, "That is a shipped look's name: pick another");
            else {
                if (s->naming == 2 && !builtin) cret_look_delete_named(v->look.name);
                snprintf(v->look.name, sizeof v->look.name, "%s", nm);
                char m[160]; snprintf(m, sizeof m, cret_look_save_named(&v->look) ? "Saved looks/%s.ini" : "Could not save %s", nm); st_note(s, m);
                s->naming = 0; st_list(s); st_changed(s);
            }
        }
        return y + 32;
    }
    if (st_button(u, "Save", x, y, &bw, false, !builtin)) {
        char m[160]; snprintf(m, sizeof m, cret_look_save_named(&v->look) ? "Saved looks/%s.ini" : "Could not save %s", v->look.name); st_note(s, m); st_list(s);
    }
    x += bw + 6;
    if (st_button(u, "Save as new", x, y, &bw, false, true)) { s->naming = 1; snprintf(s->name_buf, sizeof s->name_buf, "%s", builtin ? "My look" : v->look.name); while (GetCharPressed() > 0) {} }
    x += bw + 6;
    if (st_button(u, "Rename", x, y, &bw, false, !builtin)) { s->naming = 2; snprintf(s->name_buf, sizeof s->name_buf, "%s", v->look.name); while (GetCharPressed() > 0) {} }
    x += bw + 6;
    if (st_button(u, "Delete", x, y, &bw, false, !builtin)) {
        char m[160]; snprintf(m, sizeof m, cret_look_delete_named(v->look.name) ? "Deleted %s (the sliders stay)" : "Could not delete %s", v->look.name); st_note(s, m);
        snprintf(v->look.name, sizeof v->look.name, "Unsaved"); st_list(s); st_changed(s);
    }
    y += 30;
    cret_ui_text(u->h, false, "Shipped looks cannot be overwritten: Save as new keeps your own.", u->x, y, ST_TEXT - 2, UI_FAINT);
    return y + 22;
}

// ---------------------------------------------------------------- scene controls
static float st_scene(CretStudio* s, St* u, CretStudioWorld w, float y) {
    bool can = w.sim && w.can_set;
    if (!can) {
        cret_ui_text(u->h, false, w.sim ? "The server sets the time and weather." : "Time and weather: in play.", u->x, y + 4, ST_TEXT, UI_FAINT);
        return y + ST_ROW;
    }
    CretSim* sim = w.sim; char t[32];
    float hours = sim->time_of_day * 24; int hh = (int)hours, mm = (int)((hours - hh) * 60);
    snprintf(t, sizeof t, "%02d:%02d", hh, mm);
    float h0 = hours;
    if (st_slider(s, u, ST_SCENE + SC_TIME, y, "Time of day", t, &hours, 0, 23.99f, 12, 0.05f, false) && hours != h0) {
        sim->time_of_day = hours / 24; s->freeze_tod = sim->time_of_day;
    }
    y += ST_ROW;
    float cover = sim->cloud_cover, c0 = cover; snprintf(t, sizeof t, "%d%%", (int)roundf(cover * 100));
    if (st_slider(s, u, ST_SCENE + SC_CLOUDS, y, "Cloud cover", t, &cover, 0, 1, 0.5f, 0.01f, false) && cover != c0) {
        sim->cfg.weather = 0; sim->cfg.cloud_cover = cover; sim->cloud_cover = cover;   // as + and - do: the weather stops drifting
    }
    return y + ST_ROW;
}

// ---------------------------------------------------------------- the panel
bool cret_studio_frame(CretStudio* s, CretHud* h, CretView* v, CretStudioWorld w, float dt) {
    (void)dt;
    cret_ui_begin(h);
    float sw = cret_ui_w(), sh = cret_ui_h(), U = GetScreenHeight() / fmaxf(sh, 1);
    float pw = st_clamp(sw * 0.34f, 430, 640), px = sw - pw - 10, py = 10, ph = sh - 20;
    double now = GetTime();
    St u = {h, cret_ui_mouse(), IsMouseButtonPressed(MOUSE_BUTTON_LEFT), IsMouseButtonPressed(MOUSE_BUTTON_RIGHT), IsMouseButtonDown(MOUSE_BUTTON_LEFT), false, px + 22, pw - 44};
    s->over_panel = u.m.x >= px && u.m.y >= py && u.m.y <= py + ph;
    if (!u.down) s->active = -1;
    s->hover = -1;
    bool closed = false;
    // The split divider, over the scene: dragged anywhere left of the panel near its line.
    float dx = s->split_x * sw;
    if (s->split) {
        if (u.click && !s->over_panel && fabsf(u.m.x - dx) < 14) s->drag_split = true;
        if (!u.down) s->drag_split = false;
        if (s->drag_split) s->split_x = st_clamp(u.m.x / sw, 0.02f, (px - 4) / sw);
        dx = s->split_x * sw;
        DrawRectangleRec((Rectangle){dx - 1, 0, 2, sh}, cret_ui_fa((Color){255, 248, 230, 200}));
        DrawCircleV((Vector2){dx, sh * 0.5f}, 9, cret_ui_fa((Color){16, 17, 15, 200})); DrawCircleLinesV((Vector2){dx, sh * 0.5f}, 9, cret_ui_fa(UI_INK));
        const char* lt = s->left_default ? "DEFAULT" : "BEFORE";
        cret_ui_label(h, lt, dx - cret_ui_label_w(h, lt, UI_T_SMALL) - 12, 18, UI_T_SMALL, UI_INK);
        cret_ui_label(h, "NOW", dx + 12, 18, UI_T_SMALL, UI_INK);
    }
    // Hold for before: the key or the button. The whole frame, its lighting too.
    bool hold = (IsKeyDown(CRET_STUDIO_BEFORE_KEY) && !s->naming) || s->hold_before;
    if (hold) cret_ui_label(h, "BEFORE", 24, 18, UI_T_SMALL, UI_ACCENT);

    cret_ui_panel((Rectangle){px, py, pw, ph});
    // Header: title, the look in play, the compare and the screenshot.
    float y = py + 14, bw;
    cret_ui_text(h, false, "Look studio", u.x, y, UI_T_ROW, UI_INK);
    if (st_button(&u, "Close", u.x + u.w - 54, y - 1, NULL, false, true)) closed = true;
    y += 30;
    char nm[96]; snprintf(nm, sizeof nm, "Look: %s%s", v->look.name, st_edited(v) ? "  (edited)" : "");
    cret_ui_text(h, false, nm, u.x, y, ST_TEXT + 1, UI_ACCENT);
    y += 24;
    float x = u.x;
    if (st_button(&u, s->split ? "Split on" : "Split off", x, y, &bw, s->split, true)) s->split = !s->split;
    x += bw + 6;
    if (st_button(&u, s->left_default ? "Left: Default" : "Left: Before", x, y, &bw, false, true)) s->left_default = !s->left_default;
    x += bw + 6;
    {   // hold to see before (mouse): held while the button stays pressed
        float tw = cret_ui_text_w(h, false, "Hold: before", ST_TEXT) + 18; Rectangle r = {x, y, tw, 24};
        if (u.click && st_hit(&u, r)) s->hold_before = true;
        if (!u.down) s->hold_before = false;
        st_button(&u, "Hold: before", x, y, &bw, s->hold_before, true);
    }
    x += bw + 6;
    if (st_button(&u, "Screenshot", x, y, &bw, false, true)) s->shot = true;
    y += 30;
    if (s->note[0] && now - s->note_t < 6) cret_ui_text(h, false, s->note, u.x, y, ST_TEXT - 1, UI_GOOD);
    y += 20;
    // What scrolls: looks, scene and the knob sections. The footer under it keeps the help line.
    float top = y, bottom = py + ph - 64;
    if (s->over_panel && u.m.y > top && u.m.y < bottom) s->scroll -= GetMouseWheelMove() * 60;
    s->scroll = st_clamp(s->scroll, 0, fmaxf(0, s->content_h - (bottom - top)));
    BeginScissorMode((int)(px * U), (int)(top * U), (int)(pw * U), (int)((bottom - top) * U));
    bool in_view = u.m.y > top && u.m.y < bottom;
    if (!in_view) { u.click = u.rclick = false; }
    // A double click on the same slider within a third of a second.
    if (u.click) { u.dbl = now - s->last_click_t < 0.33; s->last_click_t = now; }
    y = top - s->scroll + 4;
    int open_sec = -1;
    for (int sec = SEC_LOOKS; sec <= SEC_SCENE; sec++) {
        int r = st_heading(s, &u, y, sec == SEC_LOOKS ? "Looks" : "Scene (to judge the look)", sec, false);
        if (r == 1) open_sec = sec;
        y += ST_HEAD;
        if (s->collapsed[sec]) continue;
        if (sec == SEC_LOOKS) y = st_looks(s, &u, v, y);
        else {
            y = st_scene(s, &u, w, y);
            if (w.sim && w.can_set) {   // weather and the clock
                static const char* const WEATHER[4] = {"From clouds", "Clear", "Overcast", "Rain"};
                int d = st_stepper(s, &u, ST_SCENE + SC_WEATHER, y, "Weather", WEATHER[v->weather_force + 1], v->weather_force < 0);
                if (d == 2) v->weather_force = -1; else if (d) v->weather_force = (v->weather_force + 1 + d + 4) % 4 - 1;
                y += ST_ROW;
                int f = st_stepper(s, &u, ST_SCENE + SC_FREEZE, y, "Clock", s->freeze ? "Frozen" : "Running", !s->freeze);
                if (f == 1 || f == -1) { s->freeze = !s->freeze; s->freeze_tod = w.sim->time_of_day; } else if (f == 2) s->freeze = false;
                y += ST_ROW;
            }
        }
        y += 8;
    }
    for (int sec = 0; sec < LKS_COUNT; sec++) {
        int r = st_heading(s, &u, y, CRET_LOOK_SECTIONS[sec], sec, true);
        if (r == 1) open_sec = sec;
        if (r == 2) {
            for (int k = 0; k < LK_COUNT; k++) if (CRET_LOOK_KNOBS[k].section == sec) v->look.v[k] = CRET_LOOK_KNOBS[k].def;
            st_changed(s);
        }
        y += ST_HEAD;
        if (s->collapsed[sec]) continue;
        for (int k = 0; k < LK_COUNT; k++) {
            const CretLookKnob* K = &CRET_LOOK_KNOBS[k]; if (K->section != sec) continue;
            char val[48]; st_value(K, v->look.v[k], val, sizeof val);
            bool ch;
            if (K->flags & LKF_CHOICE) {
                int d = st_stepper(s, &u, k, y, K->name, val, v->look.v[k] == K->def);
                int n = (int)(K->hi - K->lo) + 1;
                ch = d != 0;
                if (d == 2) v->look.v[k] = K->def; else if (d) v->look.v[k] = (float)(((int)v->look.v[k] + d + n) % n);
            } else ch = st_slider(s, &u, k, y, K->name, val, &v->look.v[k], K->lo, K->hi, K->def, K->step, !(K->flags & LKF_POST));
            if (ch) st_changed(s);
            y += ST_ROW;
        }
        if (sec == LKS_FOLIAGE || sec == LKS_LIGHT || sec == LKS_ATMOS) y += 2;
        y += 8;
    }
    s->content_h = y + s->scroll - top;
    EndScissorMode();
    if (open_sec >= 0) s->collapsed[open_sec] = !s->collapsed[open_sec];
    // Footer: the hovered row's help, the legend and the keys.
    y = bottom + 6;
    DrawRectangleRec((Rectangle){u.x, bottom, u.w, 1}, cret_ui_fa(UI_FAINT));
    const char* help = "Drag a slider. Double-click or right-click resets it to its default (the tick).";
    if (s->hover >= 0 && s->hover < LK_COUNT) help = CRET_LOOK_KNOBS[s->hover].help;
    else if (s->hover == ST_SCENE + SC_TIME) help = "The time of day (the clock keeps running unless frozen)";
    else if (s->hover == ST_SCENE + SC_CLOUDS) help = "How much of the sky is cloud; above about 70% it greys over, above 90% it rains";
    else if (s->hover == ST_SCENE + SC_WEATHER) help = "Force clear, overcast or rain, or follow the cloud cover";
    else if (s->hover == ST_SCENE + SC_FREEZE) help = "Hold the time of day where it is";
    cret_ui_text(h, false, help, u.x, y, ST_TEXT - 1, UI_INK);
    y += 20;
    DrawCircleV((Vector2){u.x + 3, y + 7}, 2.2f, cret_ui_fa(UI_DIM));
    cret_ui_text(h, false, "Scene lighting: the split cannot show it (hold Before can).", u.x + 10, y, ST_TEXT - 2, UI_DIM);
    y += 18;
    char keys[160]; snprintf(keys, sizeof keys, "%s close   Hold %s: before   Right-drag the scene: turn the camera", CRET_STUDIO_KEY_NAME, CRET_STUDIO_BEFORE_KEY_NAME);
    cret_ui_text(h, false, keys, u.x, y, ST_TEXT - 2, UI_DIM);
    cret_ui_end();
    // Compare: the split is the post pass's (view.h); before is the look when the studio first opened, or Default.
    if (s->left_default) cret_look_default(&v->look_before); else v->look_before = s->before;
    v->split = s->split && !hold ? s->split_x : -1;
    v->show_before = hold;
    // Keep look.ini current (the look in play) a moment after the sliders stop.
    if (s->dirty && now - s->dirty_t > 0.6 && s->active < 0) { cret_look_save_current(&v->look); s->dirty = false; }
    if (!s->naming && !s->fresh && (IsKeyPressed(CRET_STUDIO_KEY) || IsKeyPressed(KEY_ESCAPE))) closed = true;
    s->fresh = false;
    if (closed) cret_studio_close(s, v);
    return closed;
}
