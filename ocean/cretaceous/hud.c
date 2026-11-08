#include "hud.h"
#include "rlgl.h"
#include "world.h"
#include "abilities.h"
#include "progress.h"
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
        // Mipmapped glyphs: small labels at 720p no longer lose thin strokes.
        if (h->font_ok) for (int f = 0; f < 2; f++) { Texture2D* t = f ? &h->font_big.texture : &h->font.texture; GenTextureMipmaps(t); SetTextureFilter(*t, TEXTURE_FILTER_TRILINEAR); }
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
    char pct[16]; snprintf(pct, sizeof pct, "%d", (int)roundf(v * 100));
    float pw = text_w(h, false, pct, 13);
    text(h, false, pct, x + w - pw, y - 1, 13, DIM);
    // Injuries sit right-aligned before the number, clear of the label.
    if (status) {
        float sw = text_w(h, false, status, 13), sx = x + w - pw - 14 - sw, lx = x + label_w(h, name, 12) + 14;
        text(h, false, status, sx > lx ? sx : lx, y - 1, 13, (Color){232, 120, 96, 235});
    }
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

static Color group_col(unsigned char a);
static void compass(const CretHud* h, float cam_yaw, const CretSim* sim, int me) {
    // Bearing: north is -z, east +x (the map's top and right).
    float heading = wrapd((cam_yaw + 1.5707963f) * 57.29578f);
    float sw = SW(), W = fminf(520, sw * 0.5f), cx = sw * 0.5f, y = 22, span = 75;
    // Quest targets ride the strip (pinned to its ends when behind).
    const CretAgent* a = &sim->agents[me];
    for (int q = 0; q < 2 && a->alive; q++) {
        float tx, tz; if (!cret_quest_target(sim, cret_quest_of(sim, me, q), &tx, &tz, NULL)) continue;
        float d = wrapd((atan2f(tz - a->z, tx - a->x) + 1.5707963f) * 57.29578f - heading);
        float x = cx + clampf(d / span, -1, 1) * W * 0.5f;
        DrawPoly((Vector2){x, y + 40}, 4, 6, 45, q ? group_col(230) : (Color){240, 200, 90, 235});
    }
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
    // Packmates: markers that would sit under the player's arrow or on each
    // other are pushed apart on a small ring, so each one stays visible.
    float px0 = clampf(MX(a->x), X, X + S), py0 = clampf(MY(a->z), Y, Y + S);
    float placed[CRET_MAX_AGENTS][2]; int np = 0;
    for (int i = 0; i < sim->cfg.num_agents; i++) {
        const CretAgent* b = &sim->agents[i];
        if (i == me || !b->alive || !a->group || b->group != a->group) continue;
        float bx = MX(b->x), by = MY(b->z);
        if (bx < X || bx > X + S || by < Y || by > Y + S) continue;
        for (int it = 0; it < 4; it++) {
            float ox = px0, oy = py0, mind = 11; bool clash = false;
            float dx = bx - ox, dy = by - oy; if (dx * dx + dy * dy < mind * mind) clash = true;
            for (int q = 0; q < np && !clash; q++) {
                ox = placed[q][0]; oy = placed[q][1]; mind = 9; dx = bx - ox; dy = by - oy;
                if (dx * dx + dy * dy < mind * mind) clash = true;
            }
            if (!clash) break;
            float l = sqrtf(dx * dx + dy * dy), ang = l > 0.5f ? atan2f(dy, dx) : (float)np * 2.4f;
            bx = ox + cosf(ang) * mind; by = oy + sinf(ang) * mind;
        }
        if (np < CRET_MAX_AGENTS) { placed[np][0] = bx; placed[np][1] = by; np++; }
        map_arrow(bx, by, b->yaw, 5, group_col(240));
    }
    // Named places (names on the island map), nests, predators marked by
    // an alert call from the player's group, and the quest targets.
    const CretWorld* w = sim->world;
    for (int k = 0; k < w->npois; k++) {
        const CretPoi* P = &w->pois[k]; float qx = MX(P->x), qy = MY(P->z);
        if (qx < X || qx > X + S || qy < Y || qy > Y + S) continue;
        Color c = P->kind <= CRET_POI_RIVER ? (Color){120, 186, 230, 220} : P->kind <= CRET_POI_MARSH ? (Color){150, 214, 120, 220}
                : P->kind == CRET_POI_PEAK ? (Color){220, 214, 200, 220} : (Color){226, 204, 150, 220};
        DrawCircleV((Vector2){qx, qy}, h->map_big ? 4 : 2.5f, c);
        if (h->map_big) text(h, false, P->name, qx + 7, qy - 7, 13, (Color){236, 230, 216, 210});
    }
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* n = &sim->nests[k]; if (!n->active) continue;
        float nx = MX(n->x), ny = MY(n->z); if (nx < X || nx > X + S || ny < Y || ny > Y + S) continue;
        bool mine = n->owner == me + 1 || (a->group && n->group == a->group);
        DrawCircleLinesV((Vector2){nx, ny}, h->map_big ? 6 : 4, mine ? group_col(240) : (Color){214, 190, 150, 200});
        if (h->map_big || mine) text(h, false, TextFormat("%d", n->eggs), nx + 6, ny - 6, 11, mine ? group_col(240) : DIM);
    }
    for (int i = 0; i < sim->cfg.num_agents; i++) {
        const CretAgent* b = &sim->agents[i];
        if (!b->alive || b->st[CRET_ST_MARKED] <= 0 || !(b->mark_by == (a->group ? a->group : 100 + me + 1))) continue;
        float bx = MX(b->x), by = MY(b->z); if (bx < X || bx > X + S || by < Y || by > Y + S) continue;
        DrawCircleV((Vector2){bx, by}, 4, (Color){226, 80, 64, 240});
    }
    for (int q = 0; q < 2; q++) {
        float tx, tz; if (!cret_quest_target(sim, cret_quest_of(sim, me, q), &tx, &tz, NULL)) continue;
        float qx = clampf(MX(tx), X + 4, X + S - 4), qy = clampf(MY(tz), Y + 4, Y + S - 4);
        Color c = q ? group_col(240) : (Color){240, 200, 90, 245};
        DrawPoly((Vector2){qx, qy}, 4, h->map_big ? 9 : 6, 45, c); DrawPolyLines((Vector2){qx, qy}, 4, h->map_big ? 10 : 7, 45, (Color){0, 0, 0, 120});
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

// ---------------------------------------------------------------- abilities, quests, markers
static const Color C_QUEST = {240, 200, 90, 240}, C_MARK = {226, 80, 64, 240};

void cret_hud_note(CretHud* h, const char* msg) { snprintf(h->note, sizeof h->note, "%s", msg); h->note_t = 4.0f; }

// Predators an alert call from the player (or its group) marked.
static bool marked_for(const CretSim* sim, int me, const CretAgent* b) {
    const CretAgent* a = &sim->agents[me];
    return b->st[CRET_ST_MARKED] > 0 && b->mark_by == (a->group ? a->group : 100 + me + 1);
}

// Keys 1-3 as round slots along the bottom: a dark sweep while cooling down,
// a gold ring when ready, blue when short of stamina. Status effects sit
// above as chips; short notes (quest done) above those.
static void ability_bar(CretHud* h, const CretSim* sim, int me) {
    const CretAgent* a = &sim->agents[me];
    float sw = SW(), sh = SH(), r = 28, gap = 96, cx = sw * 0.5f, cy = sh - 96;
    for (int k = 0; k < CRET_AB_SLOTS; k++) {
        int ab = cret_ability_of(sim, me, k); float x = cx + (k - 1) * gap;
        DrawCircleV((Vector2){x, cy}, r + 4, PANEL);
        text(h, false, TextFormat("%d", k + 1), x - r - 6, cy - r - 8, 13, DIM);
        if (!ab) {
            DrawRing((Vector2){x, cy}, r - 1.5f, r, 0, 360, 48, (Color){255, 255, 255, 40});
            const char* l = "Esc: unlock";
            text(h, false, "-", x - text_w(h, false, "-", 20) * 0.5f, cy - 11, 20, DIM);
            text(h, false, l, x - text_w(h, false, l, 12) * 0.5f, cy + r + 8, 12, DIM);
            continue;
        }
        const CretAbility* A = &CRET_ABILITIES[ab];
        float frac = A->cooldown > 0 ? clampf(a->ab_cd[k] / A->cooldown, 0, 1) : 0;
        bool stam = a->stamina >= A->stamina, blocked = a->st[CRET_ST_STUN] > 0 || a->latch;
        DrawCircleV((Vector2){x, cy}, r, (Color){34, 31, 26, 215});
        char ini[4] = {0}; int ni = 0;   // initials: "Fear Roar" -> "FR"
        for (const char* p = A->name; *p && ni < 2; p++) if (p == A->name || p[-1] == ' ') ini[ni++] = *p;
        Color ink = frac > 0 || blocked ? DIM : INK;
        text(h, true, ini, x - text_w(h, true, ini, 22) * 0.5f, cy - 12, 22, ink);
        if (frac > 0) {
            DrawCircleSector((Vector2){x, cy}, r, -90, -90 + 360 * frac, 40, (Color){0, 0, 0, 150});
            const char* cs = TextFormat("%d", (int)ceilf(a->ab_cd[k]));
            text(h, false, cs, x - text_w(h, false, cs, 15) * 0.5f, cy + 8, 15, INK);
        }
        Color ring = frac > 0 || blocked ? (Color){255, 255, 255, 50} : stam ? C_QUEST : (Color){110, 160, 220, 220};
        DrawRing((Vector2){x, cy}, r - 2.5f, r, 0, 360, 48, ring);
        text(h, false, A->name, x - text_w(h, false, A->name, 12) * 0.5f, cy + r + 8, 12, frac > 0 ? DIM : INK);
    }
    // Status chips.
    char chip[10][40]; int nc = 0; Color cc[10];
    #define CHIP(cond, col, ...) if ((cond) && nc < 10) { snprintf(chip[nc], 40, __VA_ARGS__); cc[nc++] = (col); }
    CHIP(a->st[CRET_ST_STUN] > 0, C_MARK, "Stunned %.1f s", a->st[CRET_ST_STUN]);
    CHIP(a->st[CRET_ST_FEAR] > 0, C_MARK, "Afraid  -30%% damage");
    CHIP(a->st[CRET_ST_SLOW] > 0, C_MARK, "Slowed %d%%", (int)(a->slow_amt * 100));
    CHIP(a->latchers > 0, C_MARK, "%d raptor%s on you: turn hard to shake", a->latchers, a->latchers > 1 ? "s" : "");
    CHIP(a->latch, C_QUEST, "Holding on  -  Space lets go");
    CHIP(a->st[CRET_ST_DEFENSIVE] > 0, group_col(230), "Defensive %d s", (int)ceilf(a->st[CRET_ST_DEFENSIVE]));
    CHIP(a->st[CRET_ST_MARKED] > 0, C_MARK, "Marked  +15%% damage taken");
    CHIP(a->st[CRET_ST_BURST] > 0, C_QUEST, "Sprint burst");
    CHIP(a->st[CRET_ST_MEND] > 0, group_col(230), "Mending");
    CHIP(a->act == CRET_ACT_NEST, group_col(230), "Building a nest %d%%", (int)(100.0f * a->nest_ticks / (5 * CRET_TICK_HZ)));
    #undef CHIP
    float tw = 0; for (int k = 0; k < nc; k++) tw += text_w(h, false, chip[k], 13) + 26;
    float x = cx - tw * 0.5f, y = cy - r - 46;
    for (int k = 0; k < nc; k++) {
        float w = text_w(h, false, chip[k], 13) + 16;
        DrawRectangleRounded((Rectangle){x, y, w, 22}, 0.4f, 6, PANEL);
        DrawRectangleRec((Rectangle){x + 4, y + 8, 3, 6}, cc[k]);
        text(h, false, chip[k], x + 11, y + 4, 13, INK);
        x += w + 10;
    }
    if (h->note_t > 0) {
        h->note_t -= GetFrameTime();
        Color c = C_QUEST; c.a = (unsigned char)(240 * clampf(h->note_t, 0, 1));
        text(h, false, h->note, cx - text_w(h, false, h->note, 18) * 0.5f, y - 34, 18, c);
    }
}

// Quest tracker, top left: personal and group quests with progress,
// distance and reward, and the character's marks.
static void quest_tracker(const CretHud* h, const CretSim* sim, int me) {
    const CretAgent* a = &sim->agents[me]; const CretProgress* p = &sim->prog[me];
    float x = 28, y = 64, w = 330;
    int nq = (cret_quest_of(sim, me, 0) != NULL) + (cret_quest_of(sim, me, 1) != NULL);
    DrawRectangleRounded((Rectangle){x - 12, y - 10, w + 24, 34 + (nq ? nq : 1) * 46}, 0.08f, 6, PANEL);
    label(h, "QUESTS", x, y, 12, DIM);
    const char* mk = TextFormat("%d marks", p->marks);
    text(h, false, mk, x + w - text_w(h, false, mk, 14), y - 1, 14, C_QUEST);
    y += 24;
    if (!nq) { text(h, false, "Explore: a new quest comes soon.", x, y, 14, DIM); return; }
    for (int q = 0; q < 2; q++) {
        const CretQuest* Q = cret_quest_of(sim, me, q); if (!Q) continue;
        char title[96], prog[48]; cret_quest_text(sim, Q, title, sizeof title, prog, sizeof prog);
        Color c = q ? group_col(240) : C_QUEST;
        DrawPoly((Vector2){x + 5, y + 8}, 4, 5, 45, c);
        if (q) { label(h, "GROUP", x + 16, y + 1, 11, c); text(h, false, title, x + 16 + label_w(h, "GROUP", 11) + 8, y, 15, INK); }
        else text(h, false, title, x + 16, y, 15, INK);
        float tx, tz; char line[128];
        int marks = a->growth >= 1 ? Q->marks * 3 / 2 : Q->marks;
        if (cret_quest_target(sim, Q, &tx, &tz, NULL)) {
            float d = sqrtf((tx - a->x) * (tx - a->x) + (tz - a->z) * (tz - a->z));
            snprintf(line, sizeof line, "%s   -   %d m   -   +%d marks", prog, (int)d, marks);
        } else snprintf(line, sizeof line, "%s   -   +%d marks", prog, marks);
        if (a->growth < 1) snprintf(line + strlen(line), sizeof line - strlen(line), ", +%d%% growth", (int)roundf(Q->xp * 100));
        text(h, false, line, x + 16, y + 20, 13, DIM);
        y += 46;
    }
}

// In-world markers: the quest place (gold, group quest green), nests nearby.
static void world_markers(const CretHud* h, const CretSim* sim, int me, const float* vp) {
    const CretAgent* a = &sim->agents[me];
    if (!a->alive || !sim->world) return;
    for (int q = 0; q < 2; q++) {
        const CretQuest* Q = cret_quest_of(sim, me, q); float tx, tz, r, px, py;
        if (!cret_quest_target(sim, Q, &tx, &tz, &r)) continue;
        float d = sqrtf((tx - a->x) * (tx - a->x) + (tz - a->z) * (tz - a->z));
        float ty = cret_height(sim->world, tx, tz) + 6 + fminf(d * 0.02f, 30);
        if (!project(vp, tx, ty, tz, &px, &py)) continue;
        Color c = q ? group_col(235) : C_QUEST;
        DrawPoly((Vector2){px, py}, 4, 9, 45, c); DrawPolyLines((Vector2){px, py}, 4, 12, 45, (Color){0, 0, 0, 100});
        const CretWorld* w = sim->world; int pi = Q->poi[Q->step < Q->steps ? Q->step : 0];
        const char* t = d < r ? TextFormat("%s  -  here", pi < w->npois ? w->pois[pi].name : "") : TextFormat("%s  %d m", pi < w->npois ? w->pois[pi].name : "", (int)d);
        text(h, false, t, px - text_w(h, false, t, 13) * 0.5f + 1, py + 15, 13, (Color){0, 0, 0, 120});
        text(h, false, t, px - text_w(h, false, t, 13) * 0.5f, py + 14, 13, c);
    }
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* n = &sim->nests[k]; if (!n->active) continue;
        float d = sqrtf((n->x - a->x) * (n->x - a->x) + (n->z - a->z) * (n->z - a->z)), px, py;
        if (d > 150 || !project(vp, n->x, n->y + 1.5f, n->z, &px, &py)) continue;
        bool mine = n->owner == me + 1 || (a->group && n->group == a->group);
        const char* t = TextFormat("%s nest  -  %d egg%s", mine ? "Your" : CRET_SPECIES_TABLE[n->species].name, n->eggs, n->eggs == 1 ? "" : "s");
        Color c = mine ? group_col(230) : (Color){226, 204, 150, 220};
        DrawCircleLinesV((Vector2){px, py}, 7, c);
        text(h, false, t, px - text_w(h, false, t, 13) * 0.5f, py + 10, 13, c);
    }
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
        if (d > (marked_for(sim, me, b) ? 2 * range : range)) continue;
        float px, py; if (!project(viewproj, b->x, b->y + b->height * 1.05f + 0.4f, b->z, &px, &py)) continue;
        float al = clampf((range - d) / (range * 0.25f), 0, 1);
        const char* nm = b->growth < 0.6f ? TextFormat("Young %s", S->name) : S->name;
        bool marked = marked_for(sim, me, b);
        if (marked) { range *= 2; al = clampf((range - d) / (range * 0.25f), 0, 1); nm = TextFormat("%s  -  Marked %d m", nm, (int)d); }
        else if (mate && d > 25) nm = TextFormat("%s  %d m", nm, (int)d);
        float tw = text_w(h, false, nm, 14);
        Color c = marked ? C_MARK : mate ? group_col((unsigned char)(220 * al)) : INK; c.a = (unsigned char)((mate || marked ? 220 : 200) * al);
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
    compass(h, cam_yaw, sim, me);
    minimap(h, sim, me);
    group_panel(h, sim, me);
    world_markers(h, sim, me, viewproj);
    if (!h->map_big) quest_tracker(h, sim, me);
    if (a->alive) ability_bar(h, sim, me);
    // Vitals, bottom left.
    float x = 28, w = 270, y = sh - 196;
    DrawRectangleRounded((Rectangle){x - 14, y - 14, w + 28, 182}, 0.08f, 6, PANEL);
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    text(h, false, h->character[0] ? TextFormat("%s  -  %s", h->character, S->name) : S->name, x, y, 20, INK);
    const char* stage = a->growth >= 0.999f ? "Adult" : a->growth < 0.45f ? "Hatchling" : a->growth < 0.6f ? "Juvenile" : "Sub-adult";
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
    text(h, false, "Sounds: CC0 field and animal recordings from Freesound, listed in assets/audio/SOURCES.md", x, y, 16, INK); y += 24;
    text(h, false, "Thank you to Poly Haven, ambientCG and the Freesound recordists for releasing their work into the public domain.", x, y, 13, DIM); y += 42;
    label(h, "BUILT WITH", x, y, 13, DIM); y += 26;
    text(h, false, "raylib, Box3D, ENet, PufferLib", x, y, 16, INK);
    const char* back = "Esc  back";
    text(h, false, back, sw * 0.5f - text_w(h, false, back, 14) * 0.5f, sh - 48, 14, DIM);
}

void cret_hud_draw_credits(CretHud* h) { scale_begin(); draw_credits(h); scale_end(); }

const Color CRET_UI_INK = {236, 230, 216, 235}, CRET_UI_DIM = {200, 192, 176, 170}, CRET_UI_PANEL = {12, 12, 10, 120}, CRET_UI_ACCENT = {226, 120, 96, 230};
void cret_ui_begin(void) { scale_begin(); }
void cret_ui_end(void) { scale_end(); }
float cret_ui_w(void) { return SW(); }
float cret_ui_h(void) { return SH(); }
Vector2 cret_ui_mouse(void) { return mouse(); }
void cret_ui_text(const CretHud* h, bool big, const char* s, float x, float y, float size, Color c) { text(h, big, s, x, y, size, c); }
float cret_ui_text_w(const CretHud* h, bool big, const char* s, float size) { return text_w(h, big, s, size); }
void cret_ui_label(const CretHud* h, const char* s, float x, float y, float size, Color c) { label(h, s, x, y, size, c); }
void cret_ui_credits(CretHud* h) { draw_credits(h); }

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
    enum { R_RESUME, R_SPECIES, R_RESPAWN, R_ABILITY, R_SETTINGS, R_CREDITS, R_QUIT, R_COUNT };
    const char* rows[R_COUNT] = {"Resume", NULL, "Respawn", NULL, "Settings", "Credits", "Quit"};
    char sp[64]; snprintf(sp, sizeof sp, "Species   <  %s  >", CRET_SPECIES_TABLE[h->species].name); rows[R_SPECIES] = sp;
    // Slot 3: one of two abilities, bought with marks (Enter buys or equips).
    const CretAgent* me_a = &sim->agents[me]; const CretSpeciesAbilities* P = &CRET_SPECIES_ABILITIES[me_a->species % CRET_SPECIES];
    h->choice = h->choice & 1; int ab3 = P->ab[2 + h->choice];
    bool owned = cret_choice_unlocked(sim, me, h->choice), equipped = owned && sim->prog[me].loadout[2] == ab3;
    char abr[96]; snprintf(abr, sizeof abr, "Ability 3   <  %s  %s  >", CRET_ABILITIES[ab3].name,
                           equipped ? "(equipped)" : owned ? "(owned)" : TextFormat("(%d marks)", P->cost[h->choice]));
    rows[R_ABILITY] = abr;
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
        bool on = h->sel == r, disabled = ((r == R_RESPAWN || r == R_SPECIES) && !h->can_respawn) || (r == R_ABILITY && !me_a->alive);
        if (on) DrawRectangleRounded(box, 0.15f, 6, (Color){255, 255, 255, 26});
        Color c = disabled ? (Color){150, 145, 135, 120} : on ? INK : DIM;
        text(h, false, rows[r], x, y0 + r * rh, 22, c);
        if (disabled) continue;
        bool pick = on && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || (click && hover));
        if (r == R_SPECIES) {
            bool left = on && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)), right = (on && (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))) || pick;
            if (left) h->species = (h->species + CRET_SPECIES - 1) % CRET_SPECIES;
            if (right) h->species = (h->species + 1) % CRET_SPECIES;
        } else if (r == R_ABILITY) {
            if (on && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))) h->choice ^= 1;
            if (pick && !equipped) act = HUD_ABILITY;
        } else if (pick) {
            if (r == R_RESUME) act = HUD_RESUME;
            else if (r == R_RESPAWN) act = HUD_RESPAWN;
            else if (r == R_SETTINGS) act = HUD_SETTINGS;
            else if (r == R_CREDITS) h->credits = true;
            else if (r == R_QUIT) act = HUD_QUIT;
        }
    }
    const char* hint = "Respawning starts a new life as the chosen species.";
    text(h, false, hint, x, y0 + R_COUNT * rh + 12, 13, DIM);
    text(h, false, TextFormat("%s:  %s   -   %d marks", CRET_ABILITIES[ab3].name, CRET_ABILITIES[ab3].desc, sim->prog[me].marks), x, y0 + R_COUNT * rh + 32, 13, DIM);
    if (IsKeyPressed(KEY_ESCAPE)) act = HUD_RESUME;
    return act;
}
