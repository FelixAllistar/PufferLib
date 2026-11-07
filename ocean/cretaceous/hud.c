#include "hud.h"
#include "rlgl.h"
#include "world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CREDITS_MD "ocean/cretaceous/assets/dinos/CREDITS.md"

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float wrapd(float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }

// Layout is in 1080-line units: everything draws under a scale matrix of
// U = window height / 1080, and SW/SH are the window size in those units.
static float U = 1;
static float SW(void) { return GetScreenWidth() / U; }
static float SH(void) { return GetScreenHeight() / U; }
static void scale_begin(void) {
    U = clampf(GetScreenHeight() / 1080.0f, 0.75f, 4.0f);
    rlDrawRenderBatchActive(); rlPushMatrix(); rlScalef(U, U, 1);
}
static void scale_end(void) { rlDrawRenderBatchActive(); rlPopMatrix(); }
static Vector2 mouse(void) { Vector2 m = GetMousePosition(); return (Vector2){m.x / U, m.y / U}; }

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
static float label_w(const CretHud* h, const char* s, float size) {
    char buf[64]; int n = 0;
    for (const char* p = s; *p && n < 60; p++) { buf[n++] = *p; if (p[1]) buf[n++] = ' '; }
    buf[n] = 0; return text_w(h, false, buf, size);
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
            int len = 0;   // cells split on '|'; "\\|" is a literal bar
            for (; *p && nc < 6; p++) {
                if (p[0] == '\\' && p[1] == '|') { if (len < 159) cells[nc][len++] = '|'; p++; continue; }
                if (*p != '|') { if (len < 159) cells[nc][len++] = *p; continue; }
                cells[nc][len] = 0; strip(cells[nc]); md_unlink(cells[nc]); nc++; len = 0;
            }
            if (nc < 4) continue;
            char* tick = strstr(cells[0], " (`"); if (tick) *tick = 0;   // "Velociraptor (`raptor`)" -> "Velociraptor"
            for (int k = 0; k < 4; k++) snprintf(h->credit[h->ncredits][k], 160, "%s", cells[k]);
            h->ncredits++;
        }
        UnloadFileText(md);
    }
    if (h->ncredits == 0) {   // the file is missing: the same list, built in
        static const char* rows[6][4] = {
            {"Velociraptor", "PBR Velociraptor (Animated)", "Ferocious Industries", "CC BY 4.0"},
            {"Stegosaurus", "PBR Stegasaurus (Animated)", "Ferocious Industries", "CC BY 4.0"},
            {"Tyrannosaurus", "Tyrannosaurus rex - Animated, Science-Based", "Animal Animator", "CC BY 4.0"},
            {"Pachycephalosaurus", "PBR Pachycephalasaurus (Animated)", "Ferocious Industries", "CC BY 4.0"},
            {"Sauropelta", "Animated Sauropelta (Free)", "AneesAnimates", "CC BY 4.0"},
            {"Gallimimus", "Blender | Gallimimus", "SkinnyDoesModels", "CC BY 4.0"}};
        for (int r = 0; r < 6; r++) for (int k = 0; k < 4; k++) snprintf(h->credit[r][k], 160, "%s", rows[r][k]);
        h->ncredits = 6;
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
    if (h->map_ok) UnloadTexture(h->map_tex);
    memset(h, 0, sizeof *h);
}

// ---------------------------------------------------------------- in-game overlay
static void bar(const CretHud* h, float x, float y, float w, const char* name, const char* status, float v, Color c, bool pulse) {
    v = clampf(v, 0, 1);
    label(h, name, x, y, 12, DIM);
    if (status) text(h, false, status, x + label_w(h, name, 12) + 12, y - 1, 13, (Color){232, 120, 96, 235});
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
    *sx = (cx / cw * 0.5f + 0.5f) * SW(); *sy = (0.5f - cy / cw * 0.5f) * SH();
    return *sx > -100 && *sx < SW() + 100 && *sy > -50 && *sy < SH() + 50;
}

// Smoothstep on [0.7, 1]: compass marks fade out toward the ends.
static float sstep_edge(float t) { t = clampf((t - 0.7f) / 0.3f, 0, 1); return t * t * (3 - 2 * t); }

static void compass(const CretHud* h, float cam_yaw) {
    // Bearing: north is -z, east +x (the map's top and right).
    float heading = wrapd((cam_yaw + 1.5707963f) * 57.29578f);
    float sw = SW(), W = fminf(520, sw * 0.5f), cx = sw * 0.5f, y = 22, span = 75;
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

// ---------------------------------------------------------------- minimap
// North-up shaded relief from the world map: splat colours, darker under
// trees, water blue, lit from the north-west. Built once, on first use.
static void build_map(CretHud* h, const CretWorld* w) {
    const TerraMap* m = &w->map; int n = m->n, R = n > 1024 ? 1024 : n;
    static const float MAT[TERRA_MAT_COUNT][3] = {
        {118, 114, 106}, {140, 132, 120}, {150, 126, 94}, {72, 84, 54}, {98, 116, 64}, {100, 88, 66}, {196, 182, 142}, {82, 100, 62}};
    Color* px = malloc(sizeof(Color) * R * R);
    for (int y = 0; y < R; y++) for (int x = 0; x < R; x++) {
        int gx = x * (n - 1) / (R - 1), gz = y * (n - 1) / (R - 1); size_t i = (size_t)gz * n + gx;
        float c[3] = {0, 0, 0};
        for (int k = 0; k < TERRA_MAT_COUNT; k++) for (int j = 0; j < 3; j++) c[j] += MAT[k][j] * m->splat[i * TERRA_MAT_COUNT + k] / 255.0f;
        const uint8_t* v = &m->veg[i * TERRA_VEG_COUNT];
        float trees = (v[TERRA_VEG_CONIFER] + v[TERRA_VEG_BROADLEAF] + v[TERRA_VEG_TREEFERN] + v[TERRA_VEG_PALM]) / 255.0f;
        float shade = 1 - 0.45f * clampf(trees, 0, 1);
        int x1 = gx < n - 1 ? gx + 1 : gx, z1 = gz < n - 1 ? gz + 1 : gz;
        float hx = m->height[(size_t)gz * n + x1] - m->height[i], hz = m->height[(size_t)z1 * n + gx] - m->height[i];
        shade *= clampf(1.0f + (hx + hz) / m->cell * 0.5f, 0.55f, 1.35f);   // lit from the north-west
        float wd = m->water[i] - m->height[i];
        for (int j = 0; j < 3; j++) c[j] *= shade;
        if (m->water[i] > TERRA_DRY + 1 && wd > 0.05f) {
            float d = clampf(wd / 12.0f, 0, 1);
            c[0] = 70 - 30 * d; c[1] = 112 - 40 * d; c[2] = 128 - 30 * d;
        }
        px[y * R + x] = (Color){(unsigned char)clampf(c[0], 0, 255), (unsigned char)clampf(c[1], 0, 255), (unsigned char)clampf(c[2], 0, 255), 255};
    }
    Image img = {px, R, R, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    h->map_tex = LoadTextureFromImage(img); free(px);
    SetTextureFilter(h->map_tex, TEXTURE_FILTER_BILINEAR); SetTextureWrap(h->map_tex, TEXTURE_WRAP_CLAMP);
    h->map_ok = h->map_tex.id != 0;
}

static Color group_col(unsigned char a) { return (Color){150, 214, 120, a}; }

// Marker: an arrow along yaw (world: x east, z south).
static void map_arrow(float x, float y, float yaw, float r, Color c) {
    float fx = cosf(yaw), fy = sinf(yaw);
    Vector2 tip = {x + fx * r, y + fy * r}, l = {x - fx * r * 0.6f - fy * r * 0.6f, y - fy * r * 0.6f + fx * r * 0.6f};
    Vector2 rr = {x - fx * r * 0.6f + fy * r * 0.6f, y - fy * r * 0.6f - fx * r * 0.6f};
    DrawTriangle(tip, rr, l, c); DrawTriangle(tip, l, rr, c);
}

// Corner minimap (radius `span` m around the player) or the whole island.
static void minimap(CretHud* h, const CretSim* sim, int me) {
    if (!sim->world) return;
    if (!h->map_ok) build_map(h, sim->world);
    if (!h->map_ok) return;
    const CretAgent* a = &sim->agents[me]; float size = sim->world->size;
    float S, X, Y, span, cx, cz;
    if (h->map_big) { S = fminf(SH() - 160, SW() - 160); X = SW() * 0.5f - S * 0.5f; Y = SH() * 0.5f - S * 0.5f; span = size * 0.5f; cx = cz = size * 0.5f; }
    else { S = 220; X = SW() - S - 28; Y = 24; span = 450; cx = a->x; cz = a->z; }
    float tw = (float)h->map_tex.width / size;
    DrawRectangleRec((Rectangle){X - 4, Y - 4, S + 8, S + 8}, PANEL);
    // Clamp the window to the island; the player marker moves off-centre at the edges.
    if (!h->map_big) { cx = clampf(cx, span, size - span); cz = clampf(cz, span, size - span); }
    Rectangle src = {(cx - span) * tw, (cz - span) * tw, 2 * span * tw, 2 * span * tw};
    DrawTexturePro(h->map_tex, src, (Rectangle){X, Y, S, S}, (Vector2){0, 0}, 0, (Color){255, 255, 255, 230});
    DrawRectangleLinesEx((Rectangle){X - 1, Y - 1, S + 2, S + 2}, 1, (Color){236, 230, 216, 90});
    float k = S / (2 * span);
    #define MX(wx) (X + ((wx) - (cx - span)) * k)
    #define MY(wz) (Y + ((wz) - (cz - span)) * k)
    if (a->rally_ticks > 0) {
        float rx = clampf(MX(a->rally_x), X, X + S), ry = clampf(MY(a->rally_z), Y, Y + S);
        DrawPoly((Vector2){rx, ry}, 4, 6, 45, (Color){232, 196, 96, 240});
    }
    for (int i = 0; i < sim->cfg.num_agents; i++) {
        const CretAgent* b = &sim->agents[i];
        if (i == me || !b->alive || !a->group || b->group != a->group) continue;
        float bx = MX(b->x), by = MY(b->z);
        if (bx < X || bx > X + S || by < Y || by > Y + S) continue;
        map_arrow(bx, by, b->yaw, 5, group_col(240));
    }
    if (a->alive) map_arrow(clampf(MX(a->x), X, X + S), clampf(MY(a->z), Y, Y + S), a->yaw, h->map_big ? 10 : 7, INK);
    #undef MX
    #undef MY
    text(h, false, "N", X + S * 0.5f - text_w(h, false, "N", 13) * 0.5f, Y + 3, 13, (Color){226, 120, 96, 230});
    const char* key = h->map_big ? "M  close map" : "M  map";
    text(h, false, key, X + S - text_w(h, false, key, 12), Y + S + 8, 12, DIM);
    if (!h->map_big) {
        const char* sc = TextFormat("%d m", (int)(span * 2));
        text(h, false, sc, X, Y + S + 8, 12, DIM);
    }
}

// Group panel: each member's species, health and distance, under the minimap.
static void group_panel(const CretHud* h, const CretSim* sim, int me) {
    const CretAgent* a = &sim->agents[me];
    if (!a->group || h->map_big) return;
    int n = 0, idx[CRET_MAX_AGENTS];
    for (int i = 0; i < sim->cfg.num_agents; i++) if (i != me && sim->agents[i].alive && sim->agents[i].group == a->group) idx[n++] = i;
    if (!n) return;
    float w = 220, x = SW() - w - 28, y = 24 + 220 + 34, rh = 34;
    int show = n < 6 ? n : 6;
    DrawRectangleRounded((Rectangle){x - 12, y - 10, w + 24, 30 + show * rh}, 0.08f, 6, PANEL);
    label(h, TextFormat("GROUP  %d", n + 1), x, y, 12, group_col(220));
    if (a->rally_ticks > 0) { const char* r = "RALLY"; text(h, false, r, x + w - text_w(h, false, r, 12), y, 12, (Color){232, 196, 96, 230}); }
    y += 24;
    for (int r = 0; r < show; r++) {
        const CretAgent* b = &sim->agents[idx[r]]; const CretSpecies* S = &CRET_SPECIES_TABLE[b->species];
        float dx = b->x - a->x, dz = b->z - a->z, d = sqrtf(dx * dx + dz * dz);
        const char* nm = b->growth < 0.6f ? TextFormat("Young %s", S->name) : S->name;
        text(h, false, nm, x, y, 14, INK);
        const char* ds = TextFormat("%d m", (int)d);
        text(h, false, ds, x + w - text_w(h, false, ds, 13), y + 1, 13, DIM);
        float hp = b->max_health > 0 ? clampf(b->health / b->max_health, 0, 1) : 0;
        DrawRectangleRec((Rectangle){x, y + 19, w, 3}, (Color){255, 255, 255, 34});
        DrawRectangleRec((Rectangle){x, y + 19, w * hp, 3}, b->broken > 0 ? (Color){214, 160, 80, 230} : C_HEALTH);
        y += rh;
    }
    if (n > show) text(h, false, TextFormat("+%d more", n - show), x, y - 6, 12, DIM);
}

void cret_hud_draw(CretHud* h, const CretSim* sim, int me, const float* viewproj, float cam_yaw) {
    scale_begin();
    float sw = SW(), sh = SH();
    const CretAgent* a = &sim->agents[me];
    // Nameplates over other animals within 40-110 m (larger ones further); group
    // members in green, further out, with their distance.
    for (int i = 0; i < sim->cfg.num_agents; i++) {
        const CretAgent* b = &sim->agents[i];
        if (i == me || !b->alive) continue;
        const CretSpecies* S = &CRET_SPECIES_TABLE[b->species];
        bool mate = a->group && b->group == a->group;
        float dx = b->x - a->x, dz = b->z - a->z, d = sqrtf(dx * dx + dz * dz), range = mate ? 300 : 40 + 6 * S->length * cret_growth_scale(b->growth);
        if (d > range) continue;
        float px, py; if (!project(viewproj, b->x, b->y + b->height * 1.05f + 0.4f, b->z, &px, &py)) continue;
        float al = clampf((range - d) / (range * 0.25f), 0, 1);
        const char* nm = b->growth < 0.6f ? TextFormat("Young %s", S->name) : S->name;
        if (mate && d > 25) nm = TextFormat("%s  %d m", nm, (int)d);
        float tw = text_w(h, false, nm, 14);
        Color c = mate ? group_col((unsigned char)(220 * al)) : INK; c.a = (unsigned char)((mate ? 220 : 200) * al);
        Color sh2 = {0, 0, 0, (unsigned char)(120 * al)};
        text(h, false, nm, px - tw * 0.5f + 1, py + 1, 14, sh2); text(h, false, nm, px - tw * 0.5f, py, 14, c);
        float hp = b->max_health > 0 ? b->health / b->max_health : 1;
        if (hp < 0.995f) {
            float w = fmaxf(tw, 44);
            DrawRectangleRec((Rectangle){px - w * 0.5f, py + 18, w, 3}, (Color){0, 0, 0, (unsigned char)(110 * al)});
            DrawRectangleRec((Rectangle){px - w * 0.5f, py + 18, w * hp, 3}, (Color){C_HEALTH.r, C_HEALTH.g, C_HEALTH.b, (unsigned char)(220 * al)});
        }
    }
    // Rally point: where a group member called from.
    if (a->alive && a->rally_ticks > 0 && sim->world) {
        float ry = cret_height(sim->world, a->rally_x, a->rally_z) + 3, px, py;
        float dx = a->rally_x - a->x, dz = a->rally_z - a->z, d = sqrtf(dx * dx + dz * dz);
        if (d > 6 && project(viewproj, a->rally_x, ry, a->rally_z, &px, &py)) {
            Color c = {232, 196, 96, 230};
            DrawPoly((Vector2){px, py}, 4, 8, 45, c); DrawPolyLines((Vector2){px, py}, 4, 11, 45, (Color){0, 0, 0, 90});
            const char* t = TextFormat("Rally  %d m", (int)d);
            text(h, false, t, px - text_w(h, false, t, 13) * 0.5f, py + 13, 13, c);
        }
    }
    compass(h, cam_yaw);
    minimap(h, sim, me);
    group_panel(h, sim, me);
    // Vitals, bottom left.
    float x = 28, w = 230, y = sh - 196;
    DrawRectangleRounded((Rectangle){x - 14, y - 14, w + 28, 182}, 0.08f, 6, PANEL);
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    text(h, false, S->name, x, y, 20, INK);
    const char* stage = a->growth >= 0.999f ? "Adult" : a->growth < 0.6f ? "Juvenile" : "Sub-adult";
    char gr[48]; snprintf(gr, sizeof gr, "%s  %d%%", stage, (int)floorf(a->growth * 100));
    text(h, false, gr, x + w - text_w(h, false, gr, 13), y + 5, 13, DIM);
    float hp = a->max_health > 0 ? a->health / a->max_health : 0;
    bool bleeding = a->bleed > 0.002f * a->max_health, broken = a->broken > 0;
    const char* hl = bleeding && broken ? "Bleeding, broken leg" : bleeding ? "Bleeding" : broken ? "Broken leg" : NULL;
    bar(h, x, y + 32, w, "HEALTH", hl, hp, C_HEALTH, bleeding || hp < 0.25f);
    bar(h, x, y + 64, w, "STAMINA", broken ? "No sprint" : NULL, a->stamina, C_STAMINA, false);
    bar(h, x, y + 96, w, "HUNGER", NULL, a->food, C_FOOD, a->food < 0.15f);
    bar(h, x, y + 128, w, "THIRST", NULL, a->water, C_WATER, a->water < 0.15f);
    // Damage flash at the screen edges.
    if (a->alive && a->hurt_ticks > 0) {
        unsigned char k = (unsigned char)(90 * clampf(a->hurt_ticks / 12.0f, 0, 1));
        DrawRectangleGradientH(0, 0, (int)(sw / 6), (int)sh, (Color){120, 0, 0, k}, (Color){120, 0, 0, 0});
        DrawRectangleGradientH((int)(sw - sw / 6), 0, (int)(sw / 6), (int)sh, (Color){120, 0, 0, 0}, (Color){120, 0, 0, k});
    }
    if (!a->alive) {
        DrawRectangle(0, 0, (int)sw + 1, (int)sh + 1, (Color){0, 0, 0, 120});
        const char* t = "You died";
        text(h, true, t, sw * 0.5f - text_w(h, true, t, 54) * 0.5f, sh * 0.4f, 54, INK);
        int left = (sim->cfg.respawn_ticks - a->dead_ticks + CRET_TICK_HZ - 1) / CRET_TICK_HZ;
        const char* s = h->can_respawn ? TextFormat("Respawning in %d s   -   Enter to respawn now   -   Esc for the menu", left > 0 ? left : 0)
                                       : TextFormat("Respawning in %d s   -   Esc for the menu", left > 0 ? left : 0);
        text(h, false, s, sw * 0.5f - text_w(h, false, s, 16) * 0.5f, sh * 0.4f + 70, 16, DIM);
    }
    scale_end();
}

// ---------------------------------------------------------------- menu and credits
static CretHudAction menu(CretHud* h, const CretSim* sim, int me);
static void draw_credits(CretHud* h) {
    float sw = SW(), sh = SH();
    DrawRectangle(0, 0, (int)sw + 1, (int)sh + 1, (Color){8, 8, 7, 215});
    float x = sw * 0.5f - 360, y = sh * 0.14f;
    text(h, true, "Credits", x, y, 44, INK); y += 70;
    label(h, "DINOSAURS", x, y, 13, DIM); y += 26;
    for (int r = 0; r < h->ncredits; r++) {
        text(h, false, h->credit[r][0], x, y, 18, INK);
        text(h, false, TextFormat("\"%s\" by %s", h->credit[r][1], h->credit[r][2]), x + 200, y, 16, INK);
        text(h, false, h->credit[r][3], x + 200, y + 21, 13, DIM);
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

void cret_hud_draw_credits(CretHud* h) { scale_begin(); draw_credits(h); scale_end(); }

CretHudAction cret_hud_menu(CretHud* h, const CretSim* sim, int me) {
    scale_begin();
    CretHudAction act = menu(h, sim, me);
    scale_end();
    return act;
}

static CretHudAction menu(CretHud* h, const CretSim* sim, int me) {
    if (h->species < 0) h->species = sim->agents[me].species;
    if (h->credits) {
        draw_credits(h);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) h->credits = false;
        return HUD_NONE;
    }
    float sw = SW(), sh = SH();
    DrawRectangle(0, 0, (int)sw + 1, (int)sh + 1, (Color){8, 8, 7, 150});
    enum { R_RESUME, R_SPECIES, R_RESPAWN, R_CREDITS, R_QUIT, R_COUNT };
    const char* rows[R_COUNT] = {"Resume", NULL, "Respawn", "Credits", "Quit"};
    char sp[64]; snprintf(sp, sizeof sp, "Species   <  %s  >", CRET_SPECIES_TABLE[h->species].name); rows[R_SPECIES] = sp;
    float x = sw * 0.5f - 160, y0 = sh * 0.32f, rh = 46;
    text(h, true, "Cretaceous", x, y0 - 86, 48, INK);
    CretHudAction act = HUD_NONE;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) h->sel = (h->sel + 1) % R_COUNT;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) h->sel = (h->sel + R_COUNT - 1) % R_COUNT;
    Vector2 m = mouse(); bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
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
    const char* hint = "Respawning starts a new life as the chosen species.";
    text(h, false, hint, x, y0 + R_COUNT * rh + 12, 13, DIM);
    if (IsKeyPressed(KEY_ESCAPE)) act = HUD_RESUME;
    return act;
}
