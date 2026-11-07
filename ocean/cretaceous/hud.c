#include "hud.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CREDITS_MD "ocean/cretaceous/assets/dinos/CREDITS.md"

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float wrapd(float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }

// Palette: muted, close to the world's colours.
static const Color INK = {236, 230, 216, 235}, DIM = {200, 192, 176, 170}, PANEL = {12, 12, 10, 120};
static const Color C_HEALTH = {178, 52, 42, 235}, C_STAMINA = {214, 198, 152, 230}, C_FOOD = {196, 140, 62, 230}, C_WATER = {86, 146, 196, 230};

// ---------------------------------------------------------------- text
static void text(const CretHud* h, bool big, const char* s, float x, float y, float size, Color c) {
    if (h->font_ok) DrawTextEx(big ? h->font_big : h->font, s, (Vector2){roundf(x), roundf(y)}, size, size * 0.04f, c);
    else DrawText(s, (int)x, (int)y, (int)size, c);
}
static float text_w(const CretHud* h, bool big, const char* s, float size) {
    if (h->font_ok) return MeasureTextEx(big ? h->font_big : h->font, s, size, size * 0.04f).x;
    return (float)MeasureText(s, (int)size);
}
// Letter-spaced small capitals for labels.
static void label(const CretHud* h, const char* s, float x, float y, float size, Color c) {
    char buf[64]; int n = 0;
    for (const char* p = s; *p && n < 60; p++) { buf[n++] = *p; if (p[1]) buf[n++] = ' '; }
    buf[n] = 0; text(h, false, buf, x, y, size, c);
}

// ---------------------------------------------------------------- credits
static void strip(char* s) {
    char* a = s; while (*a == ' ' || *a == '\t') a++;
    memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s); while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}
// "[text](url)" -> "text"; anything else unchanged.
static void md_unlink(char* s) {
    if (s[0] != '[') return;
    char* e = strstr(s, "]("); if (!e) return;
    *e = 0; memmove(s, s + 1, strlen(s + 1) + 1);
}
static void load_credits(CretHud* h) {
    h->ncredits = 0;
    char* md = FileExists(CREDITS_MD) ? LoadFileText(CREDITS_MD) : NULL;
    if (md) {
        for (char* line = strtok(md, "\n"); line && h->ncredits < CRET_HUD_MAX_CREDITS; line = strtok(NULL, "\n")) {
            if (line[0] != '|' || strstr(line, "---") || strstr(line, "| Species")) continue;
            char cells[6][160] = {{0}}; int nc = 0; const char* p = line + 1;
            while (*p && nc < 6) {
                const char* q = strchr(p, '|'); if (!q) break;
                int len = (int)(q - p); if (len > 159) len = 159;
                memcpy(cells[nc], p, len); cells[nc][len] = 0; strip(cells[nc]); md_unlink(cells[nc]); nc++; p = q + 1;
            }
            if (nc < 4) continue;
            char* tick = strstr(cells[0], " (`"); if (tick) *tick = 0;   // "Velociraptor (`raptor`)" -> "Velociraptor"
            for (int k = 0; k < 4; k++) snprintf(h->credit[h->ncredits][k], 160, "%s", cells[k]);
            h->ncredits++;
        }
        UnloadFileText(md);
    }
    if (h->ncredits == 0) {   // the file is missing: the same list, built in
        static const char* rows[3][4] = {
            {"Velociraptor", "PBR Velociraptor (Animated)", "Ferocious Industries", "CC BY 4.0"},
            {"Stegosaurus", "PBR Stegasaurus (Animated)", "Ferocious Industries", "CC BY 4.0"},
            {"Tyrannosaurus", "Tyrannosaurus rex - Animated, Science-Based", "Animal Animator", "CC BY 4.0"}};
        for (int r = 0; r < 3; r++) for (int k = 0; k < 4; k++) snprintf(h->credit[r][k], 160, "%s", rows[r][k]);
        h->ncredits = 3;
    }
}

void cret_hud_init(CretHud* h) {
    memset(h, 0, sizeof *h);
    // A clean sans-serif if the system has one (CRET_FONT overrides); raylib's own font otherwise.
    const char* tries[] = {getenv("CRET_FONT"), "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                           "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", "C:/Windows/Fonts/segoeui.ttf", "/System/Library/Fonts/Helvetica.ttc"};
    for (int i = 0; i < 6 && !h->font_ok; i++) {
        if (!tries[i] || !FileExists(tries[i])) continue;
        h->font = LoadFontEx(tries[i], 36, NULL, 0); h->font_big = LoadFontEx(tries[i], 96, NULL, 0);
        h->font_ok = h->font.texture.id && h->font_big.texture.id;
        if (h->font_ok) { SetTextureFilter(h->font.texture, TEXTURE_FILTER_BILINEAR); SetTextureFilter(h->font_big.texture, TEXTURE_FILTER_BILINEAR); }
    }
    load_credits(h);
    h->species = -1; h->can_respawn = true;
}

void cret_hud_close(CretHud* h) {
    if (h->font_ok) { UnloadFont(h->font); UnloadFont(h->font_big); }
    memset(h, 0, sizeof *h);
}

// ---------------------------------------------------------------- in-game overlay
static void bar(const CretHud* h, float x, float y, float w, const char* name, float v, Color c, bool pulse) {
    v = clampf(v, 0, 1);
    label(h, name, x, y, 12, DIM);
    char pct[16]; snprintf(pct, sizeof pct, "%d", (int)roundf(v * 100));
    text(h, false, pct, x + w - text_w(h, false, pct, 13), y - 1, 13, DIM);
    float by = y + 16;
    DrawRectangleRec((Rectangle){x, by, w, 5}, (Color){255, 255, 255, 34});
    Color cc = c;
    if (pulse) cc.a = (unsigned char)(150 + 100 * (0.5f + 0.5f * sinf((float)GetTime() * 6)));
    DrawRectangleRec((Rectangle){x, by, w * v, 5}, cc);
}

static bool project(const float* vp, float x, float y, float z, float* sx, float* sy) {
    float cx = vp[0] * x + vp[4] * y + vp[8] * z + vp[12], cy = vp[1] * x + vp[5] * y + vp[9] * z + vp[13];
    float cw = vp[3] * x + vp[7] * y + vp[11] * z + vp[15];
    if (cw < 0.1f) return false;
    *sx = (cx / cw * 0.5f + 0.5f) * GetScreenWidth(); *sy = (0.5f - cy / cw * 0.5f) * GetScreenHeight();
    return *sx > -100 && *sx < GetScreenWidth() + 100 && *sy > -50 && *sy < GetScreenHeight() + 50;
}

// Smoothstep on [0.7, 1]: compass marks fade out toward the ends.
static float sstep_edge(float t) { t = clampf((t - 0.7f) / 0.3f, 0, 1); return t * t * (3 - 2 * t); }

static void compass(const CretHud* h, float cam_yaw) {
    // Bearing: north is -z, east +x (the map's top and right).
    float heading = wrapd((cam_yaw + 1.5707963f) * 57.29578f);
    float sw = (float)GetScreenWidth(), W = fminf(520, sw * 0.5f), cx = sw * 0.5f, y = 22, span = 75;
    // A faint band behind the marks keeps them legible against bright sky.
    DrawRectangleGradientH((int)(cx - W * 0.5f - 20), (int)y - 6, (int)(W * 0.5f + 20), 48, (Color){0, 0, 0, 0}, (Color){0, 0, 0, 70});
    DrawRectangleGradientH((int)cx, (int)y - 6, (int)(W * 0.5f + 20), 48, (Color){0, 0, 0, 70}, (Color){0, 0, 0, 0});
    static const char* names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    for (int b = 0; b < 360; b += 15) {
        float d = wrapd(b - heading); if (fabsf(d) > span) continue;
        float x = cx + d / span * W * 0.5f, a = 1 - sstep_edge(fabsf(d) / span);
        Color c = INK; c.a = (unsigned char)(c.a * a);
        if (b % 45 == 0) {
            const char* s = names[b / 45]; float sz = b % 90 == 0 ? 17 : 13;
            text(h, false, s, x - text_w(h, false, s, sz) * 0.5f, y + (b % 90 == 0 ? 0 : 2), sz, b == 0 ? (Color){226, 120, 96, c.a} : c);
        } else {
            Color t = c; t.a = (unsigned char)(t.a * 0.6f);
            DrawRectangleRec((Rectangle){x - 0.5f, y + 6, 1, 7}, t);
        }
    }
    DrawTriangle((Vector2){cx - 5, y + 24}, (Vector2){cx + 5, y + 24}, (Vector2){cx, y + 19}, INK);
    char deg[8]; snprintf(deg, sizeof deg, "%03d", ((int)roundf(heading) + 360) % 360);
    text(h, false, deg, cx - text_w(h, false, deg, 12) * 0.5f, y + 27, 12, DIM);
}

void cret_hud_draw(CretHud* h, const CretSim* sim, int me, const float* viewproj, float cam_yaw) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    const CretAgent* a = &sim->agents[me];
    // Nameplates over other animals within 70 m (larger ones further).
    for (int i = 0; i < sim->cfg.num_agents; i++) {
        const CretAgent* b = &sim->agents[i];
        if (i == me || !b->alive) continue;
        const CretSpecies* S = &CRET_SPECIES_TABLE[b->species];
        float dx = b->x - a->x, dz = b->z - a->z, d = sqrtf(dx * dx + dz * dz), range = 40 + 6 * S->length * cret_growth_scale(b->growth);
        if (d > range) continue;
        float px, py; if (!project(viewproj, b->x, b->y + b->height * 1.05f + 0.4f, b->z, &px, &py)) continue;
        float al = clampf((range - d) / (range * 0.25f), 0, 1);
        const char* nm = b->growth < 0.6f ? TextFormat("Young %s", S->name) : S->name;
        float tw = text_w(h, false, nm, 14);
        Color c = INK; c.a = (unsigned char)(200 * al);
        Color sh2 = {0, 0, 0, (unsigned char)(120 * al)};
        text(h, false, nm, px - tw * 0.5f + 1, py + 1, 14, sh2); text(h, false, nm, px - tw * 0.5f, py, 14, c);
        float hp = b->max_health > 0 ? b->health / b->max_health : 1;
        if (hp < 0.995f) {
            float w = fmaxf(tw, 44);
            DrawRectangleRec((Rectangle){px - w * 0.5f, py + 18, w, 3}, (Color){0, 0, 0, (unsigned char)(110 * al)});
            DrawRectangleRec((Rectangle){px - w * 0.5f, py + 18, w * hp, 3}, (Color){C_HEALTH.r, C_HEALTH.g, C_HEALTH.b, (unsigned char)(220 * al)});
        }
    }
    compass(h, cam_yaw);
    // Vitals, bottom left.
    float x = 28, w = 230, y = sh - 196;
    DrawRectangleRounded((Rectangle){x - 14, y - 14, w + 28, 182}, 0.08f, 6, PANEL);
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    text(h, false, S->name, x, y, 20, INK);
    const char* stage = a->growth >= 0.999f ? "Adult" : a->growth < 0.6f ? "Juvenile" : "Sub-adult";
    char gr[48]; snprintf(gr, sizeof gr, "%s  %d%%", stage, (int)floorf(a->growth * 100));
    text(h, false, gr, x + w - text_w(h, false, gr, 13), y + 5, 13, DIM);
    float hp = a->max_health > 0 ? a->health / a->max_health : 0;
    bool bleeding = a->bleed > 0.002f * a->max_health;
    bar(h, x, y + 32, w, bleeding ? "HEALTH  -  BLEEDING" : "HEALTH", hp, C_HEALTH, bleeding || hp < 0.25f);
    bar(h, x, y + 64, w, "STAMINA", a->stamina, C_STAMINA, false);
    bar(h, x, y + 96, w, "HUNGER", a->food, C_FOOD, a->food < 0.15f);
    bar(h, x, y + 128, w, "THIRST", a->water, C_WATER, a->water < 0.15f);
    // Damage flash at the screen edges.
    if (a->alive && a->hurt_ticks > 0) {
        unsigned char k = (unsigned char)(90 * a->hurt_ticks / 12.0f);
        DrawRectangleGradientH(0, 0, sw / 6, sh, (Color){120, 0, 0, k}, (Color){120, 0, 0, 0});
        DrawRectangleGradientH(sw - sw / 6, 0, sw / 6, sh, (Color){120, 0, 0, 0}, (Color){120, 0, 0, k});
    }
    if (!a->alive) {
        DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 120});
        const char* t = "You died";
        text(h, true, t, sw * 0.5f - text_w(h, true, t, 54) * 0.5f, sh * 0.4f, 54, INK);
        int left = (sim->cfg.respawn_ticks - a->dead_ticks + CRET_TICK_HZ - 1) / CRET_TICK_HZ;
        const char* s = TextFormat("Respawning in %d s   -   Esc for the menu", left > 0 ? left : 0);
        text(h, false, s, sw * 0.5f - text_w(h, false, s, 16) * 0.5f, sh * 0.4f + 70, 16, DIM);
    }
}

// ---------------------------------------------------------------- menu and credits
void cret_hud_draw_credits(CretHud* h) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, (Color){8, 8, 7, 215});
    float x = sw * 0.5f - 360, y = sh * 0.14f;
    text(h, true, "Credits", x, y, 44, INK); y += 70;
    label(h, "DINOSAURS", x, y, 13, DIM); y += 26;
    for (int r = 0; r < h->ncredits; r++) {
        text(h, false, h->credit[r][0], x, y, 18, INK);
        text(h, false, TextFormat("\"%s\" by %s", h->credit[r][1], h->credit[r][2]), x + 170, y, 16, INK);
        text(h, false, h->credit[r][3], x + 170, y + 21, 13, DIM);
        y += 50;
    }
    text(h, false, "Licensed under Creative Commons Attribution 4.0 (creativecommons.org/licenses/by/4.0).", x, y, 13, DIM); y += 18;
    text(h, false, "Models were rescaled, decimated, re-timed and re-textured for the game.", x, y, 13, DIM); y += 42;
    label(h, "WORLD", x, y, 13, DIM); y += 26;
    text(h, false, "Terrain textures: ambientCG and Poly Haven (CC0)", x, y, 16, INK); y += 24;
    text(h, false, "Rocks, logs and stumps: Poly Haven (CC0)", x, y, 16, INK); y += 24;
    text(h, false, "Leaf, frond and bark photographs: CC0 sources listed in assets/foliage/SOURCES.md", x, y, 16, INK); y += 24;
    text(h, false, "Thank you to Poly Haven and ambientCG for releasing their work into the public domain.", x, y, 13, DIM); y += 42;
    label(h, "BUILT WITH", x, y, 13, DIM); y += 26;
    text(h, false, "raylib, Box3D, ENet, PufferLib", x, y, 16, INK);
    const char* back = "Esc  back";
    text(h, false, back, sw * 0.5f - text_w(h, false, back, 14) * 0.5f, sh - 48, 14, DIM);
}

CretHudAction cret_hud_menu(CretHud* h, const CretSim* sim, int me) {
    if (h->species < 0) h->species = sim->agents[me].species;
    if (h->credits) {
        cret_hud_draw_credits(h);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) h->credits = false;
        return HUD_NONE;
    }
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, (Color){8, 8, 7, 150});
    enum { R_RESUME, R_SPECIES, R_RESPAWN, R_CREDITS, R_QUIT, R_COUNT };
    const char* rows[R_COUNT] = {"Resume", NULL, "Respawn", "Credits", "Quit"};
    char sp[64]; snprintf(sp, sizeof sp, "Species   <  %s  >", CRET_SPECIES_TABLE[h->species].name); rows[R_SPECIES] = sp;
    float x = sw * 0.5f - 160, y0 = sh * 0.32f, rh = 46;
    text(h, true, "Cretaceous", x, y0 - 86, 48, INK);
    CretHudAction act = HUD_NONE;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) h->sel = (h->sel + 1) % R_COUNT;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) h->sel = (h->sel + R_COUNT - 1) % R_COUNT;
    Vector2 m = GetMousePosition(); bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    for (int r = 0; r < R_COUNT; r++) {
        Rectangle box = {x - 16, y0 + r * rh - 8, 340, rh - 8};
        bool hover = CheckCollisionPointRec(m, box);
        if (hover && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) h->sel = r;
        bool on = h->sel == r, disabled = (r == R_RESPAWN || r == R_SPECIES) && !h->can_respawn;
        if (on) DrawRectangleRounded(box, 0.15f, 6, (Color){255, 255, 255, 26});
        Color c = disabled ? (Color){150, 145, 135, 120} : on ? INK : DIM;
        text(h, false, rows[r], x, y0 + r * rh, 22, c);
        if (disabled) continue;
        bool pick = on && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || (click && hover));
        if (r == R_SPECIES) {
            bool left = on && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)), right = (on && (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))) || pick;
            if (left) h->species = (h->species + CRET_SPECIES - 1) % CRET_SPECIES;
            if (right) h->species = (h->species + 1) % CRET_SPECIES;
        } else if (pick) {
            if (r == R_RESUME) act = HUD_RESUME;
            else if (r == R_RESPAWN) act = HUD_RESPAWN;
            else if (r == R_CREDITS) h->credits = true;
            else if (r == R_QUIT) act = HUD_QUIT;
        }
    }
    const char* hint = h->can_respawn ? "Respawning starts a new life as the chosen species." : "Clients respawn when the host allows it.";
    text(h, false, hint, x, y0 + R_COUNT * rh + 12, 13, DIM);
    if (IsKeyPressed(KEY_ESCAPE)) act = HUD_RESUME;
    return act;
}
