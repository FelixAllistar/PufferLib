#include "menu.h"
#include "render/view.h"
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
    s->preset = CRET_PRESET_HIGH; s->render_scale = 1.0f; s->res = 1; s->mouse_sens = 1.0f;
    s->volume[CRET_AUDIO_MASTER] = 0.8f; s->volume[CRET_AUDIO_AMBIENCE] = 0.8f; s->volume[CRET_AUDIO_EFFECTS] = 0.9f; s->volume[CRET_AUDIO_UI] = 0.6f;
}

static void settings_path(char* out, size_t n, bool make_dir) {
    const char* xdg = getenv("XDG_CONFIG_HOME"); const char* home = getenv("HOME");
    char dir[512];
    if (xdg && *xdg) snprintf(dir, sizeof dir, "%s/cretaceous", xdg);
    else snprintf(dir, sizeof dir, "%s/.config/cretaceous", home ? home : ".");
    if (make_dir) {
        char parent[512]; snprintf(parent, sizeof parent, "%s", dir);
        char* sl = strrchr(parent, '/'); if (sl) { *sl = 0; mkdir(parent, 0755); }
        mkdir(dir, 0755);
    }
    snprintf(out, n, "%s/settings.ini", dir);
}

static const char* KEYS[] = {"preset", "render_scale", "fullscreen", "resolution", "mouse_sensitivity", "invert_y",
                             "volume_master", "volume_ambience", "volume_effects", "volume_interface"};

void cret_settings_load(CretSettings* s) {
    cret_settings_default(s);
    char path[600]; settings_path(path, sizeof path, false);
    FILE* f = fopen(path, "r"); if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[64]; float v;
        if (line[0] == '#' || line[0] == ';' || sscanf(line, " %63[a-z_] = %f", key, &v) != 2) continue;
        if (!strcmp(key, KEYS[0])) s->preset = (int)clampf(v, CRET_PRESET_LOW, CRET_PRESET_HIGH);
        else if (!strcmp(key, KEYS[1])) s->render_scale = clampf(v, 0.5f, 1.0f);
        else if (!strcmp(key, KEYS[2])) s->fullscreen = v != 0;
        else if (!strcmp(key, KEYS[3])) s->res = (int)clampf(v, 0, cret_settings_resolutions() - 1);
        else if (!strcmp(key, KEYS[4])) s->mouse_sens = clampf(v, 0.25f, 3.0f);
        else if (!strcmp(key, KEYS[5])) s->invert_y = v != 0;
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
    fclose(f);
    return true;
}

// ---------------------------------------------------------------- widgets
typedef struct { CretHud* h; CretAudio* au; Vector2 m; bool click, moved; } Ui;

static Ui ui_frame(CretHud* h, CretAudio* au) {
    Vector2 d = GetMouseDelta();
    return (Ui){h, au, cret_ui_mouse(), IsMouseButtonPressed(MOUSE_BUTTON_LEFT), d.x != 0 || d.y != 0};
}

// Arrow keys / WS move the selection over `n` rows.
static void nav(Ui* u, int* sel, int n) {
    int s0 = *sel;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) *sel = (*sel + 1) % n;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) *sel = (*sel + n - 1) % n;
    if (*sel != s0) cret_audio_ui(u->au, CRET_UI_MOVE);
}

// A selectable row; returns true when picked (Enter, Space or click).
static bool row(Ui* u, int r, int* sel, float x, float y, float w, const char* s, bool enabled) {
    Rectangle box = {x - 16, y - 8, w, 38};
    bool hover = CheckCollisionPointRec(u->m, box);
    if (hover && u->moved && *sel != r) { *sel = r; cret_audio_ui(u->au, CRET_UI_MOVE); }
    bool on = *sel == r;
    if (on) DrawRectangleRounded(box, 0.15f, 6, (Color){255, 255, 255, 26});
    Color c = !enabled ? (Color){150, 145, 135, 120} : on ? CRET_UI_INK : CRET_UI_DIM;
    cret_ui_text(u->h, false, s, x, y, 22, c);
    bool pick = enabled && on && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || (u->click && hover));
    if (pick) cret_audio_ui(u->au, CRET_UI_CLICK);
    return pick;
}

// Left/right on the selected row (or clicking its left/right half): -1, 0, +1.
static int adjust(Ui* u, int r, int sel, float x, float y, float w) {
    if (sel != r) return 0;
    int d = 0;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) d = -1;
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || IsKeyPressed(KEY_ENTER)) d = 1;
    Rectangle box = {x - 16, y - 8, w, 38};
    if (u->click && CheckCollisionPointRec(u->m, box)) d = u->m.x < x + w * 0.55f ? -1 : 1;
    if (d) cret_audio_ui(u->au, CRET_UI_MOVE);
    return d;
}

// "Name ........ < value >" with a bar for 0..1 values (bar < 0: none).
static void value_row(Ui* u, int r, int sel, float x, float y, float w, const char* name, const char* value, float bar) {
    Rectangle box = {x - 16, y - 8, w, 38};
    bool on = sel == r;
    if (on) DrawRectangleRounded(box, 0.15f, 6, (Color){255, 255, 255, 26});
    Color c = on ? CRET_UI_INK : CRET_UI_DIM;
    cret_ui_text(u->h, false, name, x, y, 20, c);
    float vx = x + w * 0.52f, vw = w * 0.42f;
    if (bar >= 0) {
        DrawRectangleRec((Rectangle){vx, y + 11, vw - 70, 4}, (Color){255, 255, 255, 40});
        DrawRectangleRec((Rectangle){vx, y + 11, (vw - 70) * clampf(bar, 0, 1), 4}, c);
        cret_ui_text(u->h, false, value, vx + vw - 56, y + 1, 18, c);
    } else {
        char s[96]; snprintf(s, sizeof s, on ? "<  %s  >" : "%s", value);
        cret_ui_text(u->h, false, s, vx + (on ? 0 : cret_ui_text_w(u->h, false, "<  ", 20)), y, 20, c);
    }
}

// ---------------------------------------------------------------- settings page
static CretMenuAction settings_page(CretMenu* m, Ui* u, CretSettings* s) {
    float sw = cret_ui_w(), sh = cret_ui_h();
    DrawRectangle(0, 0, (int)sw + 1, (int)sh + 1, (Color){8, 8, 7, 175});
    enum { S_PRESET, S_SCALE, S_DISPLAY, S_RES, S_SENS, S_INVERT, S_VOL0, S_KEYS = S_VOL0 + CRET_AUDIO_BUSES, S_BACK, S_COUNT };
    float w = 640, x = sw * 0.5f - w * 0.5f + 16, y0 = sh * 0.2f, rh = 44;
    cret_ui_text(u->h, true, "Settings", x, y0 - 82, 44, CRET_UI_INK);
    if (m->page == 2) {   // key bindings (read-only)
        static const char* K[][2] = {
            {"W A S D", "Move (relative to the camera)"}, {"Shift", "Sprint"}, {"Ctrl", "Walk"}, {"Space", "Jump (where the species can)"},
            {"R", "Autorun"}, {"E (hold)", "Eat, or drink when facing water"}, {"G", "Call: rally and form a group"},
            {"Left mouse", "Attack"}, {"Mouse", "Look (Tab frees the cursor)"}, {"Wheel", "Camera distance"}, {"M", "Island map"},
            {"Esc", "Menu"}, {"F", "Free camera"}, {"T (hold)", "Time x60"}, {"F1", "Hide the help line"}};
        int n = (int)(sizeof K / sizeof K[0]);
        cret_ui_label(u->h, "KEY BINDINGS", x, y0 - 18, 13, CRET_UI_DIM);
        for (int i = 0; i < n; i++) {
            cret_ui_text(u->h, false, K[i][0], x, y0 + 14 + i * 32, 19, CRET_UI_INK);
            cret_ui_text(u->h, false, K[i][1], x + 200, y0 + 14 + i * 32, 19, CRET_UI_DIM);
        }
        const char* back = "Esc  back";
        cret_ui_text(u->h, false, back, sw * 0.5f - cret_ui_text_w(u->h, false, back, 14) * 0.5f, sh - 48, 14, CRET_UI_DIM);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || u->click) { m->page = 1; cret_audio_ui(u->au, CRET_UI_CLICK); }
        return MENU_NONE;
    }
    nav(u, &m->set_sel, S_COUNT);
    CretMenuAction act = MENU_NONE; char v[64];
    static const char* PRESET[3] = {"Low", "Medium", "High"};
    static const char* VOL[CRET_AUDIO_BUSES] = {"Master volume", "Ambience", "Effects", "Interface"};
    for (int r = 0; r < S_COUNT; r++) {
        float y = y0 + r * rh + (r >= S_VOL0 ? 16 : 0) + (r >= S_KEYS ? 16 : 0);
        Rectangle box = {x - 16, y - 8, w, 38};
        if (CheckCollisionPointRec(u->m, box) && u->moved && m->set_sel != r) m->set_sel = r;
        int d = r == S_KEYS || r == S_BACK ? 0 : adjust(u, r, m->set_sel, x, y, w);
        switch (r) {
        case S_PRESET:
            if (d) { s->preset = (s->preset + 3 + d) % 3; s->render_scale = s->preset == CRET_PRESET_LOW ? 0.7f : s->preset == CRET_PRESET_MEDIUM ? 0.8f : 1.0f; act = MENU_APPLY; }
            value_row(u, r, m->set_sel, x, y, w, "Graphics quality", PRESET[s->preset], -1); break;
        case S_SCALE:
            if (d) { s->render_scale = clampf(roundf((s->render_scale + d * 0.05f) * 20) / 20, 0.5f, 1.0f); act = MENU_APPLY; }
            snprintf(v, sizeof v, "%d%%", (int)roundf(s->render_scale * 100));
            value_row(u, r, m->set_sel, x, y, w, "Render resolution", v, (s->render_scale - 0.5f) / 0.5f); break;
        case S_DISPLAY:
            if (d) { s->fullscreen = !s->fullscreen; act = MENU_APPLY; }
            value_row(u, r, m->set_sel, x, y, w, "Display", s->fullscreen ? "Fullscreen" : "Window", -1); break;
        case S_RES: {
            int n = cret_settings_resolutions();
            if (d && !s->fullscreen) { s->res = (s->res + n + d) % n; act = MENU_APPLY; }
            int rw, rhh; cret_settings_resolution(s->res, &rw, &rhh);
            if (s->fullscreen) snprintf(v, sizeof v, "Desktop"); else snprintf(v, sizeof v, "%d x %d", rw, rhh);
            value_row(u, r, m->set_sel, x, y, w, "Window size", v, -1); break; }
        case S_SENS:
            if (d) { s->mouse_sens = clampf(roundf((s->mouse_sens + d * 0.1f) * 10) / 10, 0.25f, 3.0f); act = MENU_APPLY; }
            snprintf(v, sizeof v, "%.1f", s->mouse_sens);
            value_row(u, r, m->set_sel, x, y, w, "Mouse sensitivity", v, (s->mouse_sens - 0.25f) / 2.75f); break;
        case S_INVERT:
            if (d) { s->invert_y = !s->invert_y; act = MENU_APPLY; }
            value_row(u, r, m->set_sel, x, y, w, "Invert mouse Y", s->invert_y ? "On" : "Off", -1); break;
        case S_KEYS:
            if (row(u, r, &m->set_sel, x, y, w, "Key bindings", true)) m->page = 2;
            break;
        case S_BACK:
            if (row(u, r, &m->set_sel, x, y, w, "Back", true)) act = MENU_BACK;
            break;
        default: {
            int b = r - S_VOL0;
            if (d) { s->volume[b] = clampf(roundf((s->volume[b] + d * 0.05f) * 20) / 20, 0, 1); act = MENU_APPLY; }
            snprintf(v, sizeof v, "%d%%", (int)roundf(s->volume[b] * 100));
            value_row(u, r, m->set_sel, x, y, w, VOL[b], v, s->volume[b]); break; }
        }
    }
    cret_ui_label(u->h, "SOUND", x, y0 + S_VOL0 * rh - 6, 12, CRET_UI_DIM);
    const char* hint = "Left / right to change  -  Esc to go back";
    cret_ui_text(u->h, false, hint, sw * 0.5f - cret_ui_text_w(u->h, false, hint, 14) * 0.5f, sh - 48, 14, CRET_UI_DIM);
    if (IsKeyPressed(KEY_ESCAPE)) act = MENU_BACK;
    if (act == MENU_BACK) cret_settings_save(s);
    return act;
}

CretMenuAction cret_menu_settings(CretMenu* m, CretHud* h, CretAudio* au, CretSettings* s) {
    cret_ui_begin();
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

// Pages: 0 title, 1 settings, 2 key bindings, 3 credits.
CretMenuAction cret_menu_title(CretMenu* m, CretHud* h, CretAudio* au, CretSettings* s, float dt) {
    m->t += dt;
    cret_ui_begin();
    Ui u = ui_frame(h, au);
    float sw = cret_ui_w(), sh = cret_ui_h();
    CretMenuAction act = MENU_NONE;
    if (m->page == 1 || m->page == 2) {
        CretMenuAction a = settings_page(m, &u, s);
        if (a == MENU_BACK) { m->page = 0; a = MENU_NONE; }
        cret_ui_end(); return a;
    }
    if (m->page == 3) {
        cret_ui_credits(h);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || u.click) { m->page = 0; cret_audio_ui(au, CRET_UI_CLICK); }
        cret_ui_end(); return MENU_NONE;
    }
    // A soft vignette on the left keeps the text legible over the flyover.
    DrawRectangleGradientH(0, 0, (int)(sw * 0.55f), (int)sh + 1, (Color){6, 6, 5, 200}, (Color){6, 6, 5, 0});
    float fade = clampf(m->t / 1.5f, 0, 1);
    Color ink = CRET_UI_INK; ink.a = (unsigned char)(ink.a * fade);
    float x = 120, y0 = sh * 0.36f, rh = 48;
    cret_ui_text(h, true, "Cretaceous", x, y0 - 150, 84, ink);
    Color dim = CRET_UI_DIM; dim.a = (unsigned char)(dim.a * fade);
    cret_ui_text(h, false, "Survive the late Cretaceous: graze, hunt, grow and live with your herd or pack.", x + 4, y0 - 52, 17, dim);
    enum { T_PLAY, T_HOST, T_JOIN, T_SETTINGS, T_CREDITS, T_QUIT, T_COUNT };
    if (!m->editing) nav(&u, &m->sel, T_COUNT);
    char play[96], join[128];
    snprintf(play, sizeof play, m->sel == T_PLAY ? "Play  as  <  %s  >" : "Play  as  %s", CRET_SPECIES_TABLE[m->species].name);
    snprintf(join, sizeof join, m->editing ? "Join  %s_" : "Join  %s", m->address);
    const char* rows[T_COUNT] = {play, "Host a game", join, "Settings", "Credits", "Quit"};
    for (int r = 0; r < T_COUNT; r++) {
        float y = y0 + r * rh;
        if (r == T_PLAY && m->sel == T_PLAY && !m->editing) {
            int d = 0;
            if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) d = -1;
            if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) d = 1;
            if (d) { m->species = (m->species + CRET_SPECIES + d) % CRET_SPECIES; cret_audio_ui(au, CRET_UI_MOVE); }
        }
        if (row(&u, r, &m->sel, x, y, 460, rows[r], true) && !m->editing) {
            if (r == T_PLAY) act = MENU_PLAY;
            else if (r == T_HOST) act = MENU_HOST;
            else if (r == T_JOIN) m->editing = true;
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
    const char* hint = m->editing ? "Type the host's address, Enter to join, Esc to cancel"
                                  : "Arrows or mouse to choose  -  left / right picks the species";
    cret_ui_text(h, false, hint, x + 4, y0 + T_COUNT * rh + 14, 14, dim);
    if (m->note[0]) cret_ui_text(h, false, m->note, x + 4, y0 + T_COUNT * rh + 40, 16, CRET_UI_ACCENT);
    const char* by = "raylib - Box3D - ENet - PufferLib";
    cret_ui_text(h, false, by, sw - cret_ui_text_w(h, false, by, 13) - 28, sh - 36, 13, dim);
    cret_ui_end();
    return act;
}
