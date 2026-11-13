#include "hud.h"
#include "touch.h"
#include "rlgl.h"
#include "world.h"
#include "abilities.h"
#include "progress.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CREDITS_MD "ocean/cretaceous/assets/dinos/CREDITS.md"

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float wrapd(float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }

// Layout is in 1080-line units: everything draws under a scale matrix of
// U = window height / 1080 (1 at 1080p) times the player's ui_scale, and SW/SH
// are the window size in those units. A ui_scale above 1 is capped so the
// page keeps at least min_lines (the HUD 600; the settings and credits pages,
// and so every menu.c screen, 880).
static float U = 1;
static float SW(void) { return GetScreenWidth() / U; }
static float SH(void) { return GetScreenHeight() / U; }
static float scale_for(const CretHud* h, float min_lines) {
    float H = (float)GetScreenHeight(), base = clampf(H / 1080.0f, 0.75f, 4.0f);
    float user = clampf(h->ui_scale > 0 ? h->ui_scale : 1, 0.5f, 2.0f);
    if (user > 1) user = fminf(user, fmaxf(1, H / (base * min_lines)));
    return fmaxf(base * user, 0.5f);
}
float cret_hud_scale(const CretHud* h) { return scale_for(h, 600); }

// Fonts: rasterised at 36 and 96 px times k, k in half steps from the scale
// (1 up to 1080 lines, at most 3), so text stays sharp at 1440p and 4K.
// The small font's atlas gets a 3x3 white block in a free corner: the HUD
// draws its shapes from there (SetShapesTexture), so shapes and text share
// one texture and raylib batches a whole screen into a few draw calls
// instead of starting a new one at every change between text and shape.
static float font_k_for(float u) { return u <= 1 ? 1 : fminf(ceilf(u * 2) * 0.5f, 3); }
static Rectangle g_white;   // the white block in h->font's atlas (width 0: none, shapes keep raylib's texture)
static Font load_font(const char* path, int px, bool white) {
    Font f = {0}; int n = 0;
    unsigned char* data = LoadFileData(path, &n);
    if (!data) return f;
    f.baseSize = px; f.glyphCount = 95; f.glyphPadding = 4;
    f.glyphs = LoadFontData(data, n, px, NULL, 95, FONT_DEFAULT);
    UnloadFileData(data);
    if (!f.glyphs) return f;
    Image atlas = GenImageFontAtlas(f.glyphs, &f.recs, f.glyphCount, px, f.glyphPadding, 0);
    if (white) {   // only where no glyph reaches (the packer fills rows from the top)
        bool clear = true;
        for (int y = atlas.height - 3; y < atlas.height; y++) for (int x = atlas.width - 3; x < atlas.width; x++) clear &= GetImageColor(atlas, x, y).a == 0;
        if (clear) { ImageDrawRectangle(&atlas, atlas.width - 3, atlas.height - 3, 3, 3, WHITE); g_white = (Rectangle){atlas.width - 2.0f, atlas.height - 2.0f, 1, 1}; }
        else g_white = (Rectangle){0};
    }
    f.texture = LoadTextureFromImage(atlas); UnloadImage(atlas);
    return f;
}
static void load_fonts(CretHud* h, float k) {
    if (h->font_ok) { UnloadFont(h->font); UnloadFont(h->font_big); h->font_ok = false; }
    h->font = load_font(h->font_path, (int)(36 * k), true); h->font_big = load_font(h->font_path, (int)(96 * k), false);
    h->font_ok = h->font.texture.id && h->font_big.texture.id; h->font_k = k;
    // Mipmapped glyphs: small labels at 720p no longer lose thin strokes.
    if (h->font_ok) for (int f = 0; f < 2; f++) { Texture2D* t = f ? &h->font_big.texture : &h->font.texture; GenTextureMipmaps(t); SetTextureFilter(*t, TEXTURE_FILTER_TRILINEAR); }
}
// The page's fade (cret_ui_fade): menus fade in over UI_FADE; the HUD draws at 1.
static float A = 1;
static Texture2D g_shapes_was; static Rectangle g_shapes_was_r;   // raylib's shapes texture, put back after the UI
static void scale_begin(CretHud* h, float min_lines) {
    U = scale_for(h, min_lines); A = 1;
    rlDrawRenderBatchActive();   // flushes text drawn with the current atlases before they can be replaced
    float k = font_k_for(U);
    if (h->font_ok && h->font_path[0] && k > h->font_k) load_fonts(h, k);   // grows only, so a resize drag does not thrash
    g_shapes_was = GetShapesTexture(); g_shapes_was_r = GetShapesTextureRectangle();
    if (h->font_ok && g_white.width > 0) SetShapesTexture(h->font.texture, g_white);
    rlPushMatrix(); rlScalef(U, U, 1);
}
static void scale_end(void) { rlDrawRenderBatchActive(); rlPopMatrix(); SetShapesTexture(g_shapes_was, g_shapes_was_r); A = 1; }
static Vector2 mouse(void) { Vector2 m = GetMousePosition(); return (Vector2){m.x / U, m.y / U}; }

// Colours are ui_style.h's roles. fa() applies the page's fade, with() scales a colour's alpha.
static Color fa(Color c) { c.a = (unsigned char)(c.a * A); return c; }
static Color with(Color c, float k) { c.a = (unsigned char)clampf(c.a * k, 0, 255); return c; }
// A calm pulse for warnings: 0.55..1 at UI_PULSE cycles a second.
static float pulse(void) { return 0.775f + 0.225f * sinf((float)GetTime() * UI_PULSE * 6.2831853f); }

// ---------------------------------------------------------------- text
static void text(const CretHud* h, bool big, const char* s, float x, float y, float size, Color c) {
    // Snapped to whole screen pixels (not layout units), so text stays sharp at any scale.
    c = fa(c);
    if (h->font_ok) DrawTextEx(big ? h->font_big : h->font, s, (Vector2){roundf(x * U) / U, roundf(y * U) / U}, size, size * 0.04f, c);
    else DrawText(s, (int)x, (int)y, (int)size, c);
}
static float text_w(const CretHud* h, bool big, const char* s, float size) {
    if (h->font_ok) return MeasureTextEx(big ? h->font_big : h->font, s, size, size * 0.04f).x;
    return (float)MeasureText(s, (int)size);
}
// Loose text over the world (nameplates, markers, captions): a small dark
// pill under it keeps it legible over sunlit ferns and bright sky, and costs
// less than a shadow copy of every glyph.
static void text_sh(const CretHud* h, const char* s, float x, float y, float size, Color c) {
    float w = text_w(h, false, s, size), k = c.a / 255.0f;
    DrawRectangleRounded((Rectangle){x - 5, y - 2.5f, w + 10, size + 5}, 1, 4, fa((Color){12, 13, 11, (unsigned char)(150 * k)}));
    text(h, false, s, x, y, size, c);
}
// Centred on cx.
static void text_c(const CretHud* h, bool big, const char* s, float cx, float y, float size, Color c) { text(h, big, s, cx - text_w(h, big, s, size) * 0.5f, y, size, c); }
// Text cut to w units with an ellipsis (a static buffer: use it at once).
static const char* fit(const CretHud* h, const char* s, float size, float w) {
    static char buf[160];
    if (text_w(h, false, s, size) <= w) return s;
    int n = (int)strlen(s); if (n > 150) n = 150;
    for (; n > 0; n--) { snprintf(buf, sizeof buf, "%.*s...", n, s); if (text_w(h, false, buf, size) <= w) break; }
    return buf;
}
// Spaced capitals for labels: the font's own letter spacing, widened.
static void caps(const char* s, char* out, int n) { int i = 0; for (; s[i] && i < n - 1; i++) out[i] = (char)toupper((unsigned char)s[i]); out[i] = 0; }
static void label(const CretHud* h, const char* s, float x, float y, float size, Color c) {
    char b[96]; caps(s, b, sizeof b); c = fa(c);
    if (h->font_ok) DrawTextEx(h->font, b, (Vector2){roundf(x * U) / U, roundf(y * U) / U}, size, size * UI_TRACK, c);
    else DrawText(b, (int)x, (int)y, (int)size, c);
}
static float label_w(const CretHud* h, const char* s, float size) {
    char b[96]; caps(s, b, sizeof b);
    return h->font_ok ? MeasureTextEx(h->font, b, size, size * UI_TRACK).x : (float)MeasureText(b, (int)size);
}

// ---------------------------------------------------------------- panels
static float round_k(Rectangle r, float rad) { float m = fminf(r.width, r.height); return m > 0 ? clampf(2 * rad / m, 0, 1) : 0; }
// The soft dark backing every panel shares: the fill and a faint light edge. k scales its alpha (fades).
static void panel_k(Rectangle r, float k) {
    DrawRectangleRounded(r, round_k(r, UI_RADIUS), 3, fa(with(UI_PANEL, k)));
    DrawRectangleRoundedLinesEx(r, round_k(r, UI_RADIUS), 3, UI_EDGE, fa(with(UI_PANEL_EDGE, k)));   // thicker than 1: quads, not lines
}
static void panel(Rectangle r) { panel_k(r, 1); }
// A meter: a dark track and a fill, UI_BAR thick unless t says otherwise.
static void meter(float x, float y, float w, float t, float v, Color c) {
    v = clampf(v, 0, 1);
    DrawRectangleRounded((Rectangle){x, y, w, t}, 1, 4, fa((Color){255, 255, 255, 30}));
    if (v > 0) DrawRectangleRounded((Rectangle){x, y, fmaxf(w * v, t), t}, 1, 4, fa(c));
}

// A thick line as one quad (DrawLineEx draws triangles, which raylib cannot
// batch with the quads of text and the other shapes).
static void seg(Vector2 a, Vector2 b, float w, Color c) {
    float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    Vector2 p[4] = {{a.x + nx, a.y + ny}, {a.x - nx, a.y - ny}, {b.x - nx, b.y - ny}, {b.x + nx, b.y + ny}};
    if ((p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x) > 0) { Vector2 t = p[1]; p[1] = p[3]; p[3] = t; }   // raylib's winding
    Texture2D t = GetShapesTexture(); Rectangle r = GetShapesTextureRectangle();
    float u = (r.x + r.width * 0.5f) / t.width, v = (r.y + r.height * 0.5f) / t.height;
    rlSetTexture(t.id); rlBegin(RL_QUADS); rlColor4ub(c.r, c.g, c.b, c.a); rlNormal3f(0, 0, 1);
    for (int i = 0; i < 4; i++) { rlTexCoord2f(u, v); rlVertex2f(p[i].x, p[i].y); }
    rlEnd(); rlSetTexture(0);
}

// ---------------------------------------------------------------- icons
// Drawn in a unit box (-0.5..0.5 of the size) with one stroke weight and
// round ends, so every icon has the same hand.
typedef struct { Vector2 c; float s, w; Color col; } Pen;
static Vector2 pt(const Pen* p, float u, float v) { return (Vector2){p->c.x + u * p->s, p->c.y + v * p->s}; }
// Lines run a little past their ends, so joints close without round caps
// (at icon sizes the difference is under a pixel, and it saves the vertices).
static void ln(const Pen* p, float u0, float v0, float u1, float v1) {
    Vector2 a = pt(p, u0, v0), b = pt(p, u1, v1); float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    float e = p->w * 0.4f / l; dx *= e; dy *= e;
    seg((Vector2){a.x - dx, a.y - dy}, (Vector2){b.x + dx, b.y + dy}, p->w, p->col);
}
// Arc of radius r round (u, v), degrees clockwise from +x (raylib's sense).
static void arc(const Pen* p, float u, float v, float r, float a0, float a1, bool ends) {
    float R = r * p->s, ext = ends ? p->w * 0.4f / fmaxf(R, 1) * RAD2DEG : 0;   // open arcs run a little past their ends too
    if (a1 < a0) { float t = a0; a0 = a1; a1 = t; }
    int n = (int)clampf((a1 - a0) / 30, 3, 12);
    DrawRing(pt(p, u, v), R - p->w * 0.5f, R + p->w * 0.5f, a0 - ext, a1 + ext, n, p->col);
}
static void dot(const Pen* p, float u, float v, float r) { DrawPoly(pt(p, u, v), 6, r * p->s, 0, p->col); }
static void tri(const Pen* p, float u0, float v0, float u1, float v1, float u2, float v2) {
    Vector2 a = pt(p, u0, v0), b = pt(p, u1, v1), c = pt(p, u2, v2);
    if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) > 0) { Vector2 t = b; b = c; c = t; }   // raylib wants them counter-clockwise
    DrawTriangle(a, b, c, p->col);
}
// A quadratic curve as four quad lines.
static void curve(const Pen* p, float u0, float v0, float cu, float cv, float u1, float v1) {
    float lu = u0, lv = v0;
    for (int i = 1; i <= 4; i++) {
        float t = i / 4.0f, a = (1 - t) * (1 - t), b = 2 * (1 - t) * t, c = t * t, u = a * u0 + b * cu + c * u1, v = a * v0 + b * cv + c * v1;
        ln(p, lu, lv, u, v); lu = u; lv = v;
    }
}
static void icon_draw(int id, Vector2 c, float s, Color col);
// A small icon riding the corner of a bigger one (the bite family's badges).
static void badge(const Pen* p, int id, float u, float v, float k) { icon_draw(id, pt(p, u, v), p->s * k, p->col); }
static void bite(const Pen* p) {
    arc(p, 0, 0.22f, 0.42f, 215, 325, true); arc(p, 0, -0.16f, 0.42f, 35, 145, true);
    for (int k = -1; k <= 1; k++) { float x = k * 0.16f, y = 0.22f - sqrtf(0.1764f - x * x); tri(p, x - 0.06f, y, x + 0.06f, y, x, y + 0.15f); }
    for (int k = -1; k <= 1; k += 2) { float x = k * 0.08f, y = -0.16f + sqrtf(0.1764f - x * x); tri(p, x - 0.06f, y, x + 0.06f, y, x, y - 0.15f); }
}
static void tail(const Pen* p) { curve(p, -0.42f, 0.3f, 0.0f, -0.5f, 0.42f, 0.18f); ln(p, 0.21f, -0.07f, 0.2f, -0.28f); ln(p, 0.34f, 0.06f, 0.44f, -0.12f); }
static void arrow_head(const Pen* p, float u, float v, float du, float dv, float k) {
    float l = sqrtf(du * du + dv * dv); du /= l; dv /= l;
    ln(p, u, v, u - du * k + dv * k * 0.7f, v - dv * k - du * k * 0.7f); ln(p, u, v, u - du * k - dv * k * 0.7f, v - dv * k + du * k * 0.7f);
}
static void icon_draw(int id, Vector2 c, float s, Color col) {
    Pen P = {c, s, fmaxf(1.4f, s * UI_STROKE / UI_ICON * 0.92f), col}; const Pen* p = &P;
    switch (id) {
    case UI_I_HEART: arc(p, -0.19f, -0.12f, 0.19f, 140, 360, false); arc(p, 0.19f, -0.12f, 0.19f, 180, 400, false); ln(p, -0.336f, 0, 0, 0.36f); ln(p, 0.336f, 0, 0, 0.36f); break;
    case UI_I_LUNGS:
        ln(p, 0, -0.42f, 0, -0.1f); ln(p, 0, -0.1f, -0.09f, 0); ln(p, 0, -0.1f, 0.09f, 0);
        arc(p, -0.2f, 0.08f, 0.22f, 60, 300, false); ln(p, -0.09f, -0.11f, -0.09f, 0.27f);
        arc(p, 0.2f, 0.08f, 0.22f, -120, 120, false); ln(p, 0.09f, -0.11f, 0.09f, 0.27f); break;
    case UI_I_MEAT: arc(p, 0.1f, -0.1f, 0.24f, 0, 360, false); ln(p, -0.07f, 0.07f, -0.29f, 0.29f); dot(p, -0.36f, 0.25f, 0.08f); dot(p, -0.25f, 0.36f, 0.08f); break;
    case UI_I_LEAF: arc(p, -0.21f, -0.21f, 0.42f, 0, 90, false); arc(p, 0.21f, 0.21f, 0.42f, 180, 270, false); ln(p, -0.36f, 0.36f, 0.1f, -0.1f); break;
    case UI_I_DROP: arc(p, 0, 0.1f, 0.25f, -30, 210, false); ln(p, 0.2165f, -0.025f, 0, -0.42f); ln(p, -0.2165f, -0.025f, 0, -0.42f); break;
    case UI_I_BITE: bite(p); break;
    case UI_I_RAM: ln(p, -0.42f, 0, 0.12f, 0); arrow_head(p, 0.12f, 0, 1, 0, 0.18f); ln(p, 0.32f, -0.32f, 0.32f, 0.32f); break;
    case UI_I_TAIL: tail(p); break;
    case UI_I_SLASH: ln(p, -0.32f, 0.28f, 0.06f, -0.36f); ln(p, -0.12f, 0.36f, 0.24f, -0.26f); ln(p, 0.08f, 0.42f, 0.38f, -0.12f); break;
    case UI_I_JUMP: ln(p, -0.3f, 0.02f, 0, -0.28f); ln(p, 0, -0.28f, 0.3f, 0.02f); ln(p, -0.3f, 0.32f, 0, 0.02f); ln(p, 0, 0.02f, 0.3f, 0.32f); break;
    case UI_I_CARRY:
        ln(p, -0.24f, 0.28f, 0.24f, 0.28f); dot(p, -0.3f, 0.21f, 0.08f); dot(p, -0.3f, 0.35f, 0.08f); dot(p, 0.3f, 0.21f, 0.08f); dot(p, 0.3f, 0.35f, 0.08f);
        ln(p, 0, 0.08f, 0, -0.38f); arrow_head(p, 0, -0.38f, 0, -1, 0.16f); break;
    case UI_I_SNIFF:
        for (int k = -1; k <= 1; k++) { float y = k * 0.25f; ln(p, -0.38f, y, -0.19f, y - 0.11f); ln(p, -0.19f, y - 0.11f, 0, y); ln(p, 0, y, 0.19f, y + 0.11f); ln(p, 0.19f, y + 0.11f, 0.38f, y); }
        break;
    case UI_I_NEST: arc(p, 0, -0.08f, 0.42f, 25, 155, false); ln(p, -0.44f, 0.1f, 0.44f, 0.1f); dot(p, -0.14f, -0.03f, 0.12f); dot(p, 0.14f, -0.03f, 0.12f); dot(p, 0, -0.17f, 0.12f); break;
    case UI_I_EGG: dot(p, 0, 0.08f, 0.3f); dot(p, 0, -0.1f, 0.24f); break;
    case UI_I_CALL: dot(p, -0.3f, 0, 0.09f); arc(p, -0.3f, 0, 0.24f, -40, 40, true); arc(p, -0.3f, 0, 0.44f, -40, 40, true); arc(p, -0.3f, 0, 0.64f, -32, 32, true); break;
    case UI_I_MENU: for (int k = -1; k <= 1; k++) ln(p, -0.3f, k * 0.22f, 0.3f, k * 0.22f); break;
    case UI_I_MAP: {
        static const float M[8][2] = {{-0.42f, -0.3f}, {-0.14f, -0.4f}, {0.14f, -0.3f}, {0.42f, -0.4f}, {0.42f, 0.3f}, {0.14f, 0.4f}, {-0.14f, 0.3f}, {-0.42f, 0.4f}};
        for (int k = 0; k < 8; k++) { int j = k == 3 ? 4 : k == 7 ? 0 : k + 1; if (k == 3 || k == 7) { ln(p, M[k][0], M[k][1], M[j][0], M[j][1]); continue; } ln(p, M[k][0], M[k][1], M[j][0], M[j][1]); }
        ln(p, -0.14f, -0.4f, -0.14f, 0.3f); ln(p, 0.14f, -0.3f, 0.14f, 0.4f); break; }
    case UI_I_HAMSTRING: { Pen q = P; q.c = pt(p, -0.1f, -0.1f); q.s = s * 0.78f; bite(&q); ln(p, 0.2f, 0.24f, 0.32f, 0.38f); ln(p, 0.32f, 0.38f, 0.44f, 0.24f); break; }
    case UI_I_CRUSH: { Pen q = P; q.c = pt(p, -0.1f, -0.1f); q.s = s * 0.78f; bite(&q); ln(p, 0.22f, 0.18f, 0.34f, 0.3f); ln(p, 0.34f, 0.3f, 0.26f, 0.38f); ln(p, 0.26f, 0.38f, 0.42f, 0.46f); break; }
    case UI_I_REND: { Pen q = P; q.c = pt(p, -0.1f, -0.1f); q.s = s * 0.78f; bite(&q); badge(p, UI_I_DROP, 0.33f, 0.33f, 0.4f); break; }
    case UI_I_SPIKES: tail(p); ln(p, 0.3f, 0.34f, 0.46f, 0.42f); ln(p, 0.48f, 0.3f, 0.5f, 0.44f); break;
    case UI_I_SWEEP: arc(p, 0, 0, 0.36f, 150, 400, true); arrow_head(p, 0.36f * cosf(40 * DEG2RAD), 0.36f * sinf(40 * DEG2RAD), -sinf(40 * DEG2RAD), cosf(40 * DEG2RAD), 0.16f); break;
    case UI_I_CHARGE: arc(p, 0.08f, 0.14f, 0.3f, 180, 360, false); ln(p, -0.22f, 0.14f, 0.38f, 0.14f); ln(p, -0.46f, -0.04f, -0.32f, -0.04f); ln(p, -0.5f, 0.08f, -0.34f, 0.08f); break;
    case UI_I_SHOVE: ln(p, -0.3f, -0.28f, -0.04f, 0); ln(p, -0.04f, 0, -0.3f, 0.28f); ln(p, 0.04f, -0.28f, 0.3f, 0); ln(p, 0.3f, 0, 0.04f, 0.28f); break;
    case UI_I_POUNCE: case UI_I_DODGE: {
        float a0 = id == UI_I_POUNCE ? 200 : 210, a1 = id == UI_I_POUNCE ? 320 : 330, r = id == UI_I_POUNCE ? 0.4f : 0.34f, cy = id == UI_I_POUNCE ? 0.32f : 0.22f;
        arc(p, 0, cy, r, a0, a1, true);
        float ex = r * cosf(a1 * DEG2RAD), ey = cy + r * sinf(a1 * DEG2RAD);
        arrow_head(p, ex, ey, -sinf(a1 * DEG2RAD), cosf(a1 * DEG2RAD), 0.16f);
        ln(p, -0.44f, 0.4f, 0.44f, 0.4f);
        if (id == UI_I_DODGE) dot(p, r * cosf(a0 * DEG2RAD), cy + r * sinf(a0 * DEG2RAD), 0.08f);
        break; }
    case UI_I_ROAR: ln(p, -0.42f, -0.02f, -0.02f, -0.3f); ln(p, -0.42f, 0.02f, -0.02f, 0.3f); arc(p, -0.12f, 0, 0.3f, -35, 35, true); arc(p, -0.12f, 0, 0.5f, -32, 32, true); break;
    case UI_I_ALERT: ln(p, 0, -0.3f, 0, 0.08f); dot(p, 0, 0.27f, 0.07f); arc(p, 0, 0, 0.38f, -45, 45, true); arc(p, 0, 0, 0.38f, 135, 225, true); break;
    case UI_I_BELLOW: dot(p, 0, 0, 0.09f); arc(p, 0, 0, 0.24f, -40, 40, true); arc(p, 0, 0, 0.24f, 140, 220, true); arc(p, 0, 0, 0.44f, -38, 38, true); arc(p, 0, 0, 0.44f, 142, 218, true); break;
    case UI_I_SPRINT: ln(p, 0.12f, -0.42f, -0.16f, 0.05f); ln(p, -0.16f, 0.05f, 0.1f, 0.05f); ln(p, 0.1f, 0.05f, -0.12f, 0.42f); break;
    case UI_I_SHIELD: ln(p, -0.3f, -0.34f, 0.3f, -0.34f); ln(p, -0.3f, -0.34f, -0.3f, 0); ln(p, 0.3f, -0.34f, 0.3f, 0); curve(p, -0.3f, 0, -0.28f, 0.3f, 0, 0.42f); curve(p, 0.3f, 0, 0.28f, 0.3f, 0, 0.42f); break;
    case UI_I_HUNKER: arc(p, 0, 0.3f, 0.34f, 180, 360, false); ln(p, -0.44f, 0.3f, 0.44f, 0.3f); ln(p, -0.16f, -0.42f, 0, -0.28f); ln(p, 0, -0.28f, 0.16f, -0.42f); break;
    case UI_I_MEND: arc(p, 0, 0, 0.4f, 0, 360, false); ln(p, 0, -0.2f, 0, 0.2f); ln(p, -0.2f, 0, 0.2f, 0); break;
    case UI_I_LOCK:
        arc(p, 0, -0.08f, 0.18f, 180, 360, false); ln(p, -0.18f, -0.08f, -0.18f, 0.02f); ln(p, 0.18f, -0.08f, 0.18f, 0.02f);
        ln(p, -0.28f, 0.02f, 0.28f, 0.02f); ln(p, 0.28f, 0.02f, 0.28f, 0.38f); ln(p, 0.28f, 0.38f, -0.28f, 0.38f); ln(p, -0.28f, 0.38f, -0.28f, 0.02f); dot(p, 0, 0.19f, 0.05f); break;
    case UI_I_DIAMOND:
        DrawPoly(c, 4, s * 0.5f + 1.2f, 0, (Color){0, 0, 0, (unsigned char)(col.a * 0.5f)}); DrawPoly(c, 4, s * 0.5f, 0, col); break;
    case UI_I_PREY: tri(p, -0.38f, -0.3f, 0.38f, -0.3f, 0, 0.32f); break;
    case UI_I_BONE: ln(p, -0.24f, 0.24f, 0.24f, -0.24f); dot(p, -0.34f, 0.22f, 0.09f); dot(p, -0.22f, 0.34f, 0.09f); dot(p, 0.34f, -0.22f, 0.09f); dot(p, 0.22f, -0.34f, 0.09f); break;
    case UI_I_STUN: arc(p, 0, 0, 0.36f, 0, 280, true); arc(p, 0, 0, 0.16f, 100, 360, true); break;
    case UI_I_SLOW: ln(p, -0.24f, -0.36f, 0.24f, -0.36f); ln(p, 0.24f, -0.36f, -0.24f, 0.36f); ln(p, -0.24f, 0.36f, 0.24f, 0.36f); ln(p, 0.24f, 0.36f, -0.24f, -0.36f); break;
    case UI_I_FEAR: ln(p, 0, -0.38f, 0.4f, 0.34f); ln(p, 0.4f, 0.34f, -0.4f, 0.34f); ln(p, -0.4f, 0.34f, 0, -0.38f); ln(p, 0, -0.1f, 0, 0.1f); dot(p, 0, 0.22f, 0.05f); break;
    case UI_I_MARK: arc(p, 0, 0, 0.28f, 0, 360, false); ln(p, 0, -0.46f, 0, -0.16f); ln(p, 0, 0.16f, 0, 0.46f); ln(p, -0.46f, 0, -0.16f, 0); ln(p, 0.16f, 0, 0.46f, 0); break;
    case UI_I_WIND: ln(p, -0.42f, -0.14f, 0.22f, -0.14f); arc(p, 0.22f, -0.26f, 0.12f, -180, 90, true); ln(p, -0.42f, 0.14f, 0.3f, 0.14f); ln(p, -0.3f, 0.34f, 0.14f, 0.34f); break;
    default: break;
    }
}
void cret_ui_icon(int icon, Vector2 c, float s, Color col) { icon_draw(icon, c, s, fa(col)); }
static void icon(int id, Vector2 c, float s, Color col) { icon_draw(id, c, s, fa(col)); }
// The icon of an ability (CRET_AB_*).
static int ability_icon(int ab) {
    switch (ab) {
    case CRET_AB_HAMSTRING: return UI_I_HAMSTRING; case CRET_AB_CRUSH_BITE: return UI_I_CRUSH; case CRET_AB_REND_BITE: return UI_I_REND;
    case CRET_AB_TAIL_SLAM: return UI_I_SPIKES; case CRET_AB_SPIKE_SWEEP: return UI_I_SWEEP; case CRET_AB_CHARGE: return UI_I_CHARGE;
    case CRET_AB_SHOVE: return UI_I_SHOVE; case CRET_AB_POUNCE: return UI_I_POUNCE; case CRET_AB_DODGE_LEAP: return UI_I_DODGE;
    case CRET_AB_FEAR_ROAR: return UI_I_ROAR; case CRET_AB_ALERT_CALL: return UI_I_ALERT; case CRET_AB_HERD_BELLOW: return UI_I_BELLOW;
    case CRET_AB_SPRINT_BURST: return UI_I_SPRINT; case CRET_AB_DEF_STANCE: return UI_I_SHIELD; case CRET_AB_HUNKER: return UI_I_HUNKER;
    case CRET_AB_MEND: return UI_I_MEND; default: return UI_I_NONE;
    }
}
// The species' primary and secondary attacks: their names and icons.
static const char* attack_name(const CretSpecies* S, int k) { return k ? (S->alt.kind == CRET_ALT_SLASH ? "Slash" : "Tail swipe") : S->tail ? "Tail" : S->knock > 1.5f ? "Ram" : "Bite"; }
static int attack_icon(const CretSpecies* S, int k) { return k ? (S->alt.kind == CRET_ALT_SLASH ? UI_I_SLASH : UI_I_TAIL) : S->tail ? UI_I_TAIL : S->knock > 1.5f ? UI_I_RAM : UI_I_BITE; }

// ---------------------------------------------------------------- key caps
#define KEY_H (UI_T_SMALL + 2 * UI_SP + 2)
static float keycap1(const CretHud* h, const char* k, float x, float y) {
    bool mouse_l = !strcmp(k, "LMB"), mouse_r = !strcmp(k, "RMB"), mouse = mouse_l || mouse_r;
    int arrow = !strcmp(k, "Up") ? 1 : !strcmp(k, "Down") ? 2 : !strcmp(k, "Left") ? 3 : !strcmp(k, "Right") ? 4 : 0;
    float w = mouse || arrow ? KEY_H : fmaxf(KEY_H, text_w(h, false, k, UI_T_SMALL) + 2 * UI_SP + 2);
    Rectangle r = {x, y, w, KEY_H};
    DrawRectangleRounded(r, round_k(r, 4), 2, fa((Color){44, 43, 39, 225}));   // a cap a shade lighter than the panels, solid enough over the world
    DrawRectangleRec((Rectangle){x + 3, y + KEY_H - 2, w - 6, 2}, fa(with(UI_INK, 0.4f)));   // the key's lower lip
    float cx = x + w * 0.5f, cy = y + KEY_H * 0.5f - 0.5f;
    if (mouse) {   // a mouse with the button that does it lit
        Rectangle b = {cx - 5, cy - 7, 10, 14};
        DrawRectangleRoundedLinesEx(b, 0.9f, 2, 1.2f, fa(UI_INK));
        DrawRectangleRec((Rectangle){mouse_l ? cx - 4 : cx + 0.5f, cy - 6, 3.5f, 5}, fa(UI_ACCENT));
        DrawRectangleRec((Rectangle){cx - 0.5f, cy - 6.5f, 1, 6}, fa(UI_INK));
    } else if (arrow) {
        Pen p = {{cx, cy}, 10, 1.6f, fa(UI_INK)}; float d = arrow == 1 ? -1 : 1;
        if (arrow <= 2) { ln(&p, -0.35f, 0.18f * d, 0, -0.18f * d); ln(&p, 0, -0.18f * d, 0.35f, 0.18f * d); }
        else { d = arrow == 3 ? -1 : 1; ln(&p, 0.18f * d, -0.35f, -0.18f * d, 0); ln(&p, -0.18f * d, 0, 0.18f * d, 0.35f); }
    } else text(h, false, k, cx - text_w(h, false, k, UI_T_SMALL) * 0.5f, cy - UI_T_SMALL * 0.5f, UI_T_SMALL, UI_INK);
    return w;
}
// Several keys ("W A S D", "Up Down") as caps side by side.
static float keycaps(const CretHud* h, const char* keys, float x, float y) {
    char b[64]; snprintf(b, sizeof b, "%s", keys); float x0 = x;
    for (char* t = strtok(b, " "); t; t = strtok(NULL, " ")) x += keycap1(h, t, x, y) + 3;
    return x - x0 - 3;
}
static float keycaps_w(const CretHud* h, const char* keys) {
    char b[64]; snprintf(b, sizeof b, "%s", keys); float w = 0; int n = 0;
    for (char* t = strtok(b, " "); t; t = strtok(NULL, " "), n++) {
        bool g = !strcmp(t, "LMB") || !strcmp(t, "RMB") || !strcmp(t, "Up") || !strcmp(t, "Down") || !strcmp(t, "Left") || !strcmp(t, "Right");
        w += g ? KEY_H : fmaxf(KEY_H, text_w(h, false, t, UI_T_SMALL) + 2 * UI_SP + 2);
    }
    return w + (n > 1 ? 3 * (n - 1) : 0);
}
float cret_ui_keycap(const CretHud* h, const char* key, float x, float y) { return keycaps(h, key, x, y); }
// A row of key caps and verbs, centred on x or starting there.
static void footer_at(const CretHud* h, const char* const* keys, const char* const* verbs, int n, float x, float y, bool centre) {
    float w = 0;
    for (int i = 0; i < n; i++) w += (keys[i] && keys[i][0] ? keycaps_w(h, keys[i]) + UI_GAP : 0) + text_w(h, false, verbs[i], UI_T_SMALL + 1) + (i + 1 < n ? 6 * UI_SP : 0);
    if (centre) x -= w * 0.5f;
    for (int i = 0; i < n; i++) {
        if (keys[i] && keys[i][0]) x += keycaps(h, keys[i], x, y) + UI_GAP;
        text(h, false, verbs[i], x, y + (KEY_H - UI_T_SMALL - 1) * 0.5f, UI_T_SMALL + 1, UI_DIM);
        x += text_w(h, false, verbs[i], UI_T_SMALL + 1) + 6 * UI_SP;
    }
}
static void footer(const CretHud* h, const char* const* keys, const char* const* verbs, int n, float cx, float y) { footer_at(h, keys, verbs, n, cx, y, true); }

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
    h->ui_scale = 1;   // the caller sets the player's setting
    float k = font_k_for(scale_for(h, 600));
    for (int i = 0; i < 6 && !h->font_ok; i++) {
        if (!tries[i] || !FileExists(tries[i])) continue;
        snprintf(h->font_path, sizeof h->font_path, "%s", tries[i]);
        load_fonts(h, k);
    }
    if (!h->font_ok) h->font_path[0] = 0;
    load_credits(h);
    h->species = -1; h->can_respawn = true; h->prey = -1;
}

void cret_hud_notice(CretHud* h, const char* msg, float seconds) {
    snprintf(h->notice, sizeof h->notice, "%s", msg); h->notice_until = GetTime() + seconds;
}

void cret_hud_close(CretHud* h) {
    if (h->font_ok) { UnloadFont(h->font); UnloadFont(h->font_big); }
    if (h->map_ok) UnloadTexture(h->map_tex);
    memset(h, 0, sizeof *h);
}

// ---------------------------------------------------------------- in-game overlay
static bool project(const float* vp, float x, float y, float z, float* sx, float* sy) {
    float cx = vp[0] * x + vp[4] * y + vp[8] * z + vp[12], cy = vp[1] * x + vp[5] * y + vp[9] * z + vp[13];
    float cw = vp[3] * x + vp[7] * y + vp[11] * z + vp[15];
    if (cw < 0.1f) return false;
    *sx = (cx / cw * 0.5f + 0.5f) * SW(); *sy = (0.5f - cy / cw * 0.5f) * SH();
    return *sx > -100 && *sx < SW() + 100 && *sy > -50 && *sy < SH() + 50;
}

// Smoothstep on [0.7, 1]: compass marks fade out toward the ends.
static float sstep_edge(float t) { t = clampf((t - 0.7f) / 0.3f, 0, 1); return t * t * (3 - 2 * t); }

// Predators an alert call from the player (or its group) marked.
static bool marked_for(const CretSim* sim, int me, const CretAgent* b) {
    const CretAgent* a = &sim->agents[me];
    return b->st[CRET_ST_MARKED] > 0 && b->mark_by == (a->group ? a->group : 100 + me + 1);
}
static bool own_nest(const CretSim* sim, int me, const CretNest* n) { const CretAgent* a = &sim->agents[me]; return n->owner == me + 1 || (a->group && n->group == a->group); }

static void hunt_compass(const CretHud* h, const CretSim* sim, int me, float cx, float W, float y, float span, float heading);
// Compass strip, top centre: 15-degree ticks, the cardinal and half-cardinal
// points, the heading in degrees under the centre, and markers below the
// band: quests (amber, the group's green), the rally point, your group, your
// nest and others' near by, and small prey you noticed (hunt_hud.c).
static void compass(const CretHud* h, float cam_yaw, const CretSim* sim, int me) {
    // Bearing: north is -z, east +x (the map's top and right).
    float heading = wrapd((cam_yaw + 1.5707963f) * 57.29578f);
    float sw = SW(), W = fminf(520, sw * 0.5f), cx = sw * 0.5f, y = 22, span = 75, bh = 26;
    // The band: dark through the middle, fading out at the ends.
    Color pc = with(UI_PANEL, 0.95f), z = with(UI_PANEL, 0); float fw = W * 0.22f;
    DrawRectangleRec((Rectangle){cx - W * 0.5f + fw, y - 4, W - 2 * fw, bh}, fa(pc));
    DrawRectangleGradientH((int)(cx - W * 0.5f - 24), (int)(y - 4), (int)(fw + 24), (int)bh, fa(z), fa(pc));
    DrawRectangleGradientH((int)(cx + W * 0.5f - fw), (int)(y - 4), (int)(fw + 24), (int)bh, fa(pc), fa(z));
    DrawRectangleGradientH((int)(cx - W * 0.5f), (int)(y - 4 + bh), (int)(W * 0.5f), 1, fa(with(UI_PANEL_EDGE, 0)), fa(with(UI_INK, 0.25f)));
    DrawRectangleGradientH((int)cx, (int)(y - 4 + bh), (int)(W * 0.5f), 1, fa(with(UI_INK, 0.25f)), fa(with(UI_PANEL_EDGE, 0)));
    static const char* names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    for (int b = 0; b < 360; b += 15) {
        float d = wrapd(b - heading); if (fabsf(d) > span) continue;
        float x = cx + d / span * W * 0.5f, al = 1 - sstep_edge(fabsf(d) / span);
        if (b % 45 == 0) {
            bool card = b % 90 == 0; const char* s = names[b / 45]; float sz = card ? UI_T_BODY : UI_T_SMALL;
            Color c = b == 0 ? UI_ACCENT : card ? UI_INK : UI_DIM;
            text(h, false, s, x - text_w(h, false, s, sz) * 0.5f, y + (card ? 0 : 2), sz, with(c, al));
        } else DrawRectangleRec((Rectangle){x - 0.5f, y + 5, 1, 8}, fa(with(UI_FAINT, al)));
    }
    // Markers ride just under the band, pinned to its ends when behind.
    float my = y + bh + 6;
    #define CMX(wx, wz) (cx + clampf(wrapd((atan2f((wz) - a->z, (wx) - a->x) + 1.5707963f) * 57.29578f - heading) / span, -1, 1) * W * 0.5f)
    const CretAgent* a = &sim->agents[me];
    if (a->alive) {
        for (int i = 0; i < sim->cfg.num_agents; i++) {   // your group: small green dots on the band's edge
            const CretAgent* b = &sim->agents[i];
            if (i == me || !b->alive || !a->group || b->group != a->group) continue;
            DrawCircleV((Vector2){CMX(b->x, b->z), y - 4 + bh}, 2.5f, fa(UI_GOOD));
        }
        for (int k = 0; k < CRET_MAX_NESTS; k++) {
            const CretNest* n = &sim->nests[k]; if (!n->active) continue;
            bool mine = own_nest(sim, me, n);
            if (!mine && hypotf(n->x - a->x, n->z - a->z) > 150) continue;
            icon(UI_I_EGG, (Vector2){CMX(n->x, n->z), my + 5}, 10, mine ? UI_GOOD : UI_DIM);
        }
        if (a->rally_ticks > 0) { float x = CMX(a->rally_x, a->rally_z); DrawPolyLinesEx((Vector2){x, my + 5}, 4, 6, 0, 1.5f, fa(UI_ACCENT)); }
        for (int q = 0; q < 2; q++) {
            float tx, tz; if (!cret_quest_target(sim, cret_quest_of(sim, me, q), &tx, &tz, NULL)) continue;
            icon(UI_I_DIAMOND, (Vector2){CMX(tx, tz), my + 5}, 11, q ? UI_GOOD : UI_ACCENT);
        }
    }
    #undef CMX
    hunt_compass(h, sim, me, cx, W, my, span, heading);   // small prey it has noticed (hunt_hud.c)
    DrawTriangle((Vector2){cx - 5, y - 4 + bh}, (Vector2){cx + 5, y - 4 + bh}, (Vector2){cx, y - 9 + bh}, fa(UI_INK));
    char deg[8]; snprintf(deg, sizeof deg, "%03d", ((int)roundf(heading) + 360) % 360);
    text_sh(h, deg, cx - text_w(h, false, deg, UI_T_SMALL) * 0.5f, my + 12, UI_T_SMALL, UI_DIM);
}

// ---------------------------------------------------------------- minimap
// North-up shaded relief from the world map: splat colours, darker under
// trees, water blue, lit from the north-west. Built once, on first use.
static void build_map(CretHud* h, const CretWorld* w) {
    const TerraMap* m = &w->map; int cap = U > 1.25f ? 2048 : 1024, n = m->n, R = n > cap ? cap : n;   // finer above 1350 lines
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

// A carcass slot in words: "the carcass" (the dead animal's species), "the fish", "the Gallimimus egg".
static const char* item_name(const CretCarcass* k, bool article) {
    const char* sp = CRET_SPECIES_TABLE[k->species < CRET_SPECIES ? k->species : 0].name;
    if (k->item == CRET_ITEM_EGG) return TextFormat("%s%s egg", article ? "the " : "", sp);
    if (k->item == CRET_ITEM_MEAT) return TextFormat("%spiece of %s", article ? "the " : "a ", sp);
    if (k->item == CRET_ITEM_CRITTER) return TextFormat("%s%s", article ? "the " : "", CRET_CRITTER_KINDS[k->critter < CRET_CR_KINDS ? k->critter : 0].name);
    return TextFormat("%s%s carcass", article ? "the " : "", sp);
}

// Marker: an arrow along yaw (world: x east, z south), with a dark rim.
static void map_arrow(float x, float y, float yaw, float r, Color c) {
    float fx = cosf(yaw), fy = sinf(yaw);
    for (int pass = 0; pass < 2; pass++) {
        float R = pass ? r : r + 1.5f; Color col = pass ? c : (Color){0, 0, 0, (unsigned char)(c.a * 0.55f)};
        Vector2 tip = {x + fx * R, y + fy * R}, l = {x - fx * R * 0.6f - fy * R * 0.6f, y - fy * R * 0.6f + fx * R * 0.6f};
        Vector2 rr = {x - fx * R * 0.6f + fy * R * 0.6f, y - fy * R * 0.6f - fx * R * 0.6f};
        DrawTriangle(tip, rr, l, fa(col)); DrawTriangle(tip, l, rr, fa(col));
    }
}

// The corner minimap's box: smaller under touch controls, which put the menu
// button and the right-hand buttons below and beside it.
static Rectangle map_box(const CretHud* h) { float S = h->touch ? 180 : 220; return (Rectangle){SW() - S - 28, 24, S, S}; }
// Corner minimap (radius `span` m around the player) or the whole island.
static void minimap(CretHud* h, const CretSim* sim, int me) {
    if (!sim->world) return;
    if (!h->map_ok) build_map(h, sim->world);
    if (!h->map_ok) return;
    const CretAgent* a = &sim->agents[me]; float size = sim->world->size;
    float S, X, Y, span, cx, cz;
    if (h->map_big) { S = fminf(SH() - 160, SW() - 160); X = SW() * 0.5f - S * 0.5f; Y = SH() * 0.5f - S * 0.5f; span = size * 0.5f; cx = cz = size * 0.5f; }
    else { Rectangle b = map_box(h); S = b.width; X = b.x; Y = b.y; span = 450; cx = a->x; cz = a->z; }
    float tw = (float)h->map_tex.width / size;
    panel((Rectangle){X - 5, Y - 5, S + 10, S + 10});
    // Clamp the window to the island; the player marker moves off-centre at the edges.
    if (!h->map_big) { cx = clampf(cx, span, size - span); cz = clampf(cz, span, size - span); }
    Rectangle src = {(cx - span) * tw, (cz - span) * tw, 2 * span * tw, 2 * span * tw};
    DrawTexturePro(h->map_tex, src, (Rectangle){X, Y, S, S}, (Vector2){0, 0}, 0, fa((Color){255, 255, 255, 235}));
    DrawRectangleLinesEx((Rectangle){X, Y, S, S}, 1, fa((Color){0, 0, 0, 90}));
    float k = S / (2 * span);
    #define MX(wx) (X + ((wx) - (cx - span)) * k)
    #define MY(wz) (Y + ((wz) - (cz - span)) * k)
    if (a->rally_ticks > 0) {
        float rx = clampf(MX(a->rally_x), X, X + S), ry = clampf(MY(a->rally_z), Y, Y + S);
        DrawPolyLinesEx((Vector2){rx, ry}, 4, 6, 0, 2, fa(UI_ACCENT));
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
        map_arrow(bx, by, b->yaw, 5, UI_GOOD);
    }
    // Named places (names on the island map), nests, predators marked by
    // an alert call from the player's group, and the quest targets.
    const CretWorld* w = sim->world;
    for (int k = 0; k < w->npois; k++) {
        const CretPoi* P = &w->pois[k]; float qx = MX(P->x), qy = MY(P->z);
        if (qx < X || qx > X + S || qy < Y || qy > Y + S) continue;
        Color c = P->kind <= CRET_POI_RIVER ? (Color){120, 186, 230, 220} : P->kind <= CRET_POI_MARSH ? (Color){150, 214, 120, 220}
                : P->kind == CRET_POI_PEAK ? (Color){220, 214, 200, 220} : (Color){226, 204, 150, 220};
        // Places the character has seen are solid and named in full; the rest are faint until visited, so the map fills in.
        bool seen = sim->prog[me].visited >> k & 1;
        if (!seen) c.a = 110;
        DrawCircleV((Vector2){qx, qy}, h->map_big ? 4 : 2.5f, fa(c));
        if (h->map_big) text(h, false, P->name, qx + 7, qy - 7, UI_T_SMALL + 1, seen ? UI_INK : UI_FAINT);
    }
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* n = &sim->nests[k]; if (!n->active) continue;
        float nx = MX(n->x), ny = MY(n->z); if (nx < X || nx > X + S || ny < Y || ny > Y + S) continue;
        bool mine = own_nest(sim, me, n);
        icon(UI_I_NEST, (Vector2){nx, ny}, h->map_big ? 14 : 10, mine ? UI_GOOD : UI_DIM);
        if (h->map_big || mine) text(h, false, TextFormat("%d", n->eggs), nx + 7, ny - 6, UI_T_SMALL - 1, mine ? UI_GOOD : UI_DIM);
    }
    for (int i = 0; i < sim->cfg.num_agents; i++) {
        const CretAgent* b = &sim->agents[i];
        if (!b->alive || !marked_for(sim, me, b)) continue;
        float bx = MX(b->x), by = MY(b->z); if (bx < X || bx > X + S || by < Y || by > Y + S) continue;
        DrawCircleV((Vector2){bx, by}, 4, fa(UI_DANGER));
    }
    for (int q = 0; q < 2; q++) {
        float tx, tz; if (!cret_quest_target(sim, cret_quest_of(sim, me, q), &tx, &tz, NULL)) continue;
        float qx = clampf(MX(tx), X + 5, X + S - 5), qy = clampf(MY(tz), Y + 5, Y + S - 5);
        icon(UI_I_DIAMOND, (Vector2){qx, qy}, h->map_big ? 18 : 12, q ? UI_GOOD : UI_ACCENT);
    }
    if (a->alive) map_arrow(clampf(MX(a->x), X, X + S), clampf(MY(a->z), Y, Y + S), a->yaw, h->map_big ? 10 : 7, UI_INK);
    #undef MX
    #undef MY
    text_sh(h, "N", X + S * 0.5f - text_w(h, false, "N", UI_T_SMALL + 1) * 0.5f, Y + 3, UI_T_SMALL + 1, UI_ACCENT);
    if (h->map_big) {
        label(h, "The island", X, Y - 30, UI_T_SMALL, UI_DIM);
        const char* keys[1] = {"M"}; const char* verbs[1] = {"Close map"};
        if (!h->touch) footer(h, keys, verbs, 1, X + S * 0.5f, Y + S + 14);
        return;
    }
    text_sh(h, TextFormat("%d m", (int)(span * 2)), X, Y + S + 10, UI_T_SMALL, UI_DIM);
    if (h->touch) return;   // the map opens with a tap on it
    float kw = keycaps_w(h, "M"), vw = text_w(h, false, "Map", UI_T_SMALL);
    keycaps(h, "M", X + S - vw - kw - 6, Y + S + 6);
    text_sh(h, "Map", X + S - vw, Y + S + 10, UI_T_SMALL, UI_DIM);
}

// Group panel: each member's species, health and distance, under the
// minimap. Returns the bottom of what it drew (the scent panel goes below).
static float group_panel(const CretHud* h, const CretSim* sim, int me) {
    const CretAgent* a = &sim->agents[me];
    float top = map_box(h).y + map_box(h).height + 34;
    if (!a->group || h->map_big || h->touch) return top - 8;   // touch: the right-hand buttons sit there (the minimap shows the group)
    int n = 0, idx[CRET_MAX_AGENTS];
    for (int i = 0; i < sim->cfg.num_agents; i++) if (i != me && sim->agents[i].alive && sim->agents[i].group == a->group) idx[n++] = i;
    if (!n) return top - 8;
    float w = 220, x = SW() - w - 28, y = top, rh = 32;
    int show = n < 6 ? n : 6;
    float ph = 30 + show * rh + (n > show ? 14 : 0);
    panel((Rectangle){x - UI_PAD, y - UI_PAD + 2, w + 2 * UI_PAD, ph});
    bool carn = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE;
    label(h, TextFormat("%s  %d", carn ? "Pack" : "Herd", n + 1), x, y, UI_T_SMALL, UI_GOOD);
    if (a->rally_ticks > 0) { const char* r = "Rally"; label(h, r, x + w - label_w(h, r, UI_T_SMALL), y, UI_T_SMALL, UI_ACCENT); }
    y += 24;
    for (int r = 0; r < show; r++) {
        const CretAgent* b = &sim->agents[idx[r]]; const CretSpecies* S = &CRET_SPECIES_TABLE[b->species];
        float d = hypotf(b->x - a->x, b->z - a->z);
        const char* ds = TextFormat("%d m", (int)d);
        float dw = text_w(h, false, ds, UI_T_SMALL);
        const char* nm = b->growth < 0.6f ? TextFormat("Young %s", S->name) : S->name;
        text(h, false, fit(h, nm, UI_T_BODY - 1, w - dw - 22), x, y, UI_T_BODY - 1, UI_INK);
        text(h, false, ds, x + w - dw, y + 1, UI_T_SMALL, UI_DIM);
        float hp = b->max_health > 0 ? clampf(b->health / b->max_health, 0, 1) : 0;
        if (b->broken > 0) icon(UI_I_BONE, (Vector2){x + w - dw - 10, y + 7}, 10, UI_WARN);
        meter(x, y + 19, w, 3, hp, b->broken > 0 ? UI_WARN : UI_HEALTH);
        y += rh;
    }
    if (n > show) text(h, false, TextFormat("+%d more", n - show), x, y - 6, UI_T_SMALL, UI_DIM);
    return top + ph - UI_PAD + 2;
}

// ---------------------------------------------------------------- abilities, quests, markers
void cret_hud_note(CretHud* h, const char* msg) { snprintf(h->note, sizeof h->note, "%s", msg); h->note_t = 4.0f; }

// A round slot: the dark disc, its icon, a cooldown sweep with the seconds
// left, and a ring: amber when ready, blue when short of stamina, faint
// while it cools or is blocked.
static void slot(const CretHud* h, Vector2 c, float r, int ic, float frac, float secs, bool blocked, bool stam, bool lock) {
    DrawCircleSector(c, r, 0, 360, 24, fa(with(UI_PANEL, 1.2f)));
    bool ready = !lock && frac <= 0 && !blocked;
    icon(lock ? UI_I_LOCK : ic, c, r * (lock ? 0.8f : 1.0f), lock ? UI_FAINT : ready ? UI_INK : with(UI_DIM, frac > 0 ? 0.55f : 1));
    if (frac > 0) {
        DrawCircleSector(c, r - 1, -90, -90 + 360 * frac, 16, fa((Color){0, 0, 0, 120}));
        DrawRing(c, r - 2.5f, r, -90 + 360 * frac, 270, 16, fa(with(UI_INK, 0.55f)));   // the part that has come back
        const char* cs = TextFormat("%d", (int)ceilf(secs));
        text(h, false, cs, c.x - text_w(h, false, cs, UI_T_BODY + 1) * 0.5f, c.y - (UI_T_BODY + 1) * 0.5f, UI_T_BODY + 1, UI_INK);
        return;
    }
    Color ring = lock || blocked ? UI_FAINT : stam ? UI_ACCENT : UI_WATER;
    DrawRing(c, r - 2.2f, r, 0, 360, 24, fa(ring));
}

// Keys 1-3 as round slots along the bottom, the attacks either side. Status
// effects sit above as chips; short notes (quest done) above those.
static void ability_bar(CretHud* h, const CretSim* sim, int me) {
    const CretAgent* a = &sim->agents[me];
    float sw = SW(), sh = SH(), r = 28, gap = 96, cx = sw * 0.5f, cy = sh - 96;
    // Under touch controls the buttons show the slots and the attacks (touch.c).
    for (int k = 0; k < CRET_AB_SLOTS && !h->touch; k++) {
        int ab = cret_ability_of(sim, me, k); float x = cx + (k - 1) * gap;
        keycaps(h, TextFormat("%d", k + 1), x - r - 8, cy - r - 10);
        if (!ab) {
            slot(h, (Vector2){x, cy}, r, 0, 0, 0, false, true, true);
            text_sh(h, "Esc: unlock", x - text_w(h, false, "Esc: unlock", UI_T_SMALL) * 0.5f, cy + r + 9, UI_T_SMALL, with(UI_INK, 0.7f));
            continue;
        }
        const CretAbility* A = &CRET_ABILITIES[ab];
        float frac = A->cooldown > 0 ? clampf(a->ab_cd[k] / A->cooldown, 0, 1) : 0;
        bool stam = a->stamina >= A->stamina, blocked = a->st[CRET_ST_STUN] > 0 || a->latch;
        slot(h, (Vector2){x, cy}, r, ability_icon(ab), frac, a->ab_cd[k], blocked, stam, false);
        text_sh(h, A->name, x - text_w(h, false, A->name, UI_T_SMALL) * 0.5f, cy + r + 9, UI_T_SMALL, frac > 0 ? with(UI_INK, 0.7f) : UI_INK);
    }
    // Primary and secondary attacks: cooldown radials either side of the slots, with the mouse button.
    const CretSpecies* SP = &CRET_SPECIES_TABLE[a->species];
    for (int k = 0; k < 2 && !h->touch; k++) {
        if (k == 1 && SP->alt.kind == CRET_ALT_NONE) continue;
        float x = cx + (k ? 1 : -1) * (gap * 2 - 8), rr = 22;
        float full = (k ? SP->alt.cooldown : SP->cooldown) * CRET_TICK_HZ, cd = (float)(k ? a->attack2_cd : a->attack_cd);
        float frac = full > 0 ? clampf(cd / full, 0, 1) : 0;
        Vector2 c = {x, cy};
        DrawCircleSector(c, rr, 0, 360, 20, fa(with(UI_PANEL, 1.2f)));
        icon(attack_icon(SP, k), c, rr * 0.95f, frac > 0 ? with(UI_DIM, 0.6f) : UI_INK);
        if (frac > 0) DrawCircleSector(c, rr - 1, -90, -90 + 360 * frac, 14, fa((Color){0, 0, 0, 120}));
        DrawRing(c, rr - 1.8f, rr, 0, 360, 20, fa(frac > 0 ? UI_FAINT : with(UI_INK, 0.7f)));
        keycaps(h, k ? "RMB" : "LMB", x + rr - 8, cy - rr - 10);
        text_sh(h, attack_name(SP, k), x - text_w(h, false, attack_name(SP, k), UI_T_SMALL) * 0.5f, cy + rr + 9, UI_T_SMALL, frac > 0 ? with(UI_INK, 0.7f) : UI_INK);
    }
    // Status chips: an icon in its colour and a few words.
    char chip[10][40]; int nc = 0, ci[10]; Color cc[10];
    #define CHIP(cond, ic, col, ...) if ((cond) && nc < 10) { snprintf(chip[nc], 40, __VA_ARGS__); ci[nc] = (ic); cc[nc++] = (col); }
    CHIP(a->st[CRET_ST_STUN] > 0, UI_I_STUN, UI_DANGER, "Stunned %.1f s", a->st[CRET_ST_STUN]);
    CHIP(a->st[CRET_ST_FEAR] > 0, UI_I_FEAR, UI_WARN, "Afraid  -30%% damage");
    CHIP(a->st[CRET_ST_SLOW] > 0, UI_I_SLOW, UI_WARN, "Slowed %d%%", (int)(a->slow_amt * 100));
    CHIP(a->latchers > 0, UI_I_BITE, UI_DANGER, "%d raptor%s on you: turn hard to shake", a->latchers, a->latchers > 1 ? "s" : "");
    CHIP(a->latch, UI_I_BITE, UI_ACCENT, "Holding on  -  Space lets go");
    CHIP(a->st[CRET_ST_DEFENSIVE] > 0, UI_I_SHIELD, UI_GOOD, "Defensive %d s", (int)ceilf(a->st[CRET_ST_DEFENSIVE]));
    CHIP(a->st[CRET_ST_MARKED] > 0, UI_I_MARK, UI_DANGER, "Marked  +15%% damage taken");
    CHIP(a->st[CRET_ST_BURST] > 0, UI_I_SPRINT, UI_ACCENT, "Sprint burst");
    CHIP(a->st[CRET_ST_MEND] > 0, UI_I_MEND, UI_GOOD, "Mending");
    CHIP(a->act == CRET_ACT_NEST, UI_I_NEST, UI_GOOD, "Building a nest %d%%", (int)(100.0f * a->nest_ticks / (5 * CRET_TICK_HZ)));
    #undef CHIP
    float cs = UI_T_SMALL + 1, tw = 0; for (int k = 0; k < nc; k++) tw += text_w(h, false, chip[k], cs) + 38 + UI_GAP;
    // Touch: above the food prompt, clear of the call and ability buttons.
    float x = cx - (tw - UI_GAP) * 0.5f, y = h->touch ? sh * 0.72f - 38 : cy - r - 50;
    for (int k = 0; k < nc; k++) {
        float w = text_w(h, false, chip[k], cs) + 38;
        panel((Rectangle){x, y, w, 24});
        icon(ci[k], (Vector2){x + 14, y + 12}, 12, cc[k]);
        text(h, false, chip[k], x + 26, y + 12 - cs * 0.5f, cs, UI_INK);
        x += w + UI_GAP;
    }
    if (h->note_t > 0) {
        h->note_t -= GetFrameTime();
        Color c = with(UI_ACCENT, clampf(h->note_t / UI_FADE * 0.25f, 0, 1));
        text_sh(h, h->note, cx - text_w(h, false, h->note, UI_T_ROW - 2) * 0.5f, y - 34, UI_T_ROW - 2, c);
    }
}

// Compass point of a target from the animal: north is -z, east +x (the compass strip and the map).
static const char* bearing(float dx, float dz) {
    static const char* names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    float b = atan2f(dx, -dz) * 57.29578f; if (b < 0) b += 360;
    return names[(int)((b + 22.5f) / 45) % 8];
}

// A prompt: what a key (or, on touch, a button) does here, as a key cap and
// a short verb, with a detail after it; or, without a key, an icon and a few
// words. A longer piece of guidance (hint) shows under it for a while.
typedef struct { const char* key; int icon; char verb[48], detail[96], hint[128]; } Prompt;
static void prompt_set(Prompt* p, const char* key, int ic, const char* verb, const char* detail, const char* hint) {
    p->key = key; p->icon = ic;
    snprintf(p->verb, sizeof p->verb, "%s", verb ? verb : ""); snprintf(p->detail, sizeof p->detail, "%s", detail ? detail : ""); snprintf(p->hint, sizeof p->hint, "%.127s", hint ? hint : "");
}

#include "hunt_hud.c"   // small prey: direction, markers, the scent panel

// Draws a prompt pill centred on cx; returns its height.
static float prompt_draw(const CretHud* h, const Prompt* p, float cx, float y) {
    bool cap = p->key && !h->touch;
    float vs = UI_T_BODY + 1, ds = UI_T_BODY, kw = cap ? keycaps_w(h, p->key) : p->icon ? 22 : 0;
    float vw = text_w(h, false, p->verb, vs), dw = p->detail[0] ? text_w(h, false, p->detail, ds) + UI_GAP : 0;
    float w = UI_PAD + kw + (kw > 0 ? UI_GAP + 2 : 0) + vw + dw + UI_PAD, ph = 34, x = cx - w * 0.5f;
    panel((Rectangle){x, y, w, ph});
    x += UI_PAD;
    if (cap) x += keycaps(h, p->key, x, y + (ph - KEY_H) * 0.5f) + UI_GAP + 2;
    else if (p->icon) {   // the touch button's icon in a small ring, or the prompt's own icon
        Vector2 c = {x + 11, y + ph * 0.5f};
        if (h->touch && p->key) DrawRing(c, 10.5f, 12, 0, 360, 28, fa(with(UI_INK, 0.5f)));
        icon(p->icon, c, h->touch && p->key ? 14 : 16, p->key ? UI_INK : UI_ACCENT);
        x += 22 + UI_GAP + 2;
    }
    text(h, false, p->verb, x, y + (ph - vs) * 0.5f, vs, UI_INK);
    if (p->detail[0]) text(h, false, p->detail, x + vw + UI_GAP, y + (ph - ds) * 0.5f, ds, UI_DIM);
    return ph;
}
// The hint under the prompts: shown for UI_HINT_HOLD s after it first
// appears (or changes), then faded; it comes back when the hint changes.
static void hint_draw(CretHud* h, const char* s, float cx, float y) {
    double now = GetTime();
    if (!s || !s[0]) { h->hint[0] = 0; return; }
    if (strcmp(s, h->hint)) { snprintf(h->hint, sizeof h->hint, "%s", s); h->hint_t0 = now; }
    float t = (float)(now - h->hint_t0), k = clampf(t / UI_FADE, 0, 1) * (1 - clampf((t - UI_HINT_HOLD) / 0.6f, 0, 1));
    if (k <= 0) return;
    text_sh(h, s, cx - text_w(h, false, s, UI_T_BODY - 1) * 0.5f, y, UI_T_BODY - 1, with(UI_INK, 0.9f * k));
}

// Quest tracker, top left: personal and group quests in plain words, each
// with a progress bar, the distance and compass direction (or "here") and
// the reward; the character's marks. A first-steps quest is labelled with
// its step. Returns the card's bottom.
static float quest_tracker(const CretHud* h, const CretSim* sim, int me) {
    const CretAgent* a = &sim->agents[me]; const CretProgress* p = &sim->prog[me];
    float x = 28 + UI_PAD, y = 64, w = 300, eh = 58;
    int nq = (cret_quest_of(sim, me, 0) != NULL) + (cret_quest_of(sim, me, 1) != NULL);
    char where[2][64] = {{0}}, title[2][96] = {{0}}, prog[2][48] = {{0}}, foot[2][128] = {{0}}; float frac[2] = {0}; bool here[2] = {false};
    for (int q = 0; q < 2; q++) {
        const CretQuest* Q = cret_quest_of(sim, me, q); if (!Q) continue;
        cret_quest_text(sim, Q, title[q], sizeof title[q], prog[q], sizeof prog[q]);
        frac[q] = Q->kind == CRET_Q_PATROL ? (float)Q->step / fmaxf(Q->steps, 1) : Q->goal > 0 ? clampf(Q->progress / Q->goal, 0, 1) : 0;
        float tx, tz, r;
        if (Q->kind == CRET_Q_CATCH && !q && h->prey >= 0) {   // lead to the prey it has noticed, not to the spot
            const CretCritter* c = &sim->critters[h->prey];
            snprintf(where[q], sizeof where[q], "nearest %d m %s", (int)roundf(h->prey_d), bearing(c->x - a->x, c->z - a->z));
        } else if (cret_quest_target(sim, Q, &tx, &tz, &r)) {
            float d = hypotf(tx - a->x, tz - a->z);
            if (d < r) { here[q] = true; snprintf(where[q], sizeof where[q], Q->kind == CRET_Q_CATCH && !q ? (h->touch ? "here: hold Sniff" : "here: hold V to sniff") : "here"); }
            else snprintf(where[q], sizeof where[q], "%d m %s", (int)d, bearing(tx - a->x, tz - a->z));
        }
        int marks = a->growth >= 1 ? Q->marks * 3 / 2 : Q->marks;
        snprintf(foot[q], sizeof foot[q], "+%d marks", marks);
        if (a->growth < 1) snprintf(foot[q] + strlen(foot[q]), sizeof foot[q] - strlen(foot[q]), ",  +%d%% growth", (int)roundf(Q->xp * 100));
        w = fmaxf(w, 26 + text_w(h, false, title[q], UI_T_BODY) + 16 + text_w(h, false, where[q], UI_T_SMALL));
    }
    w = fminf(w, fmaxf(300, SW() * 0.5f - 300));   // clear of the compass and the target's bar
    float bottom = y - UI_PAD + 2 + 28 + (nq ? nq * eh : 24) + 2;
    panel((Rectangle){x - UI_PAD, y - UI_PAD + 2, w + 2 * UI_PAD, bottom - (y - UI_PAD + 2)});
    label(h, "Quests", x, y, UI_T_SMALL, UI_DIM);
    const char* mk = TextFormat("%d marks", p->marks);
    text(h, false, mk, x + w - text_w(h, false, mk, UI_T_SMALL + 1), y - 1, UI_T_SMALL + 1, UI_ACCENT);
    y += 26;
    if (!nq) { text(h, false, "A new quest comes soon.", x, y, UI_T_BODY - 1, UI_DIM); return bottom; }
    for (int q = 0; q < 2; q++) {
        const CretQuest* Q = cret_quest_of(sim, me, q); if (!Q) continue;
        Color c = q ? UI_GOOD : UI_ACCENT;
        icon(UI_I_DIAMOND, (Vector2){x + 6, y + 8}, 10, c);
        float ww = text_w(h, false, where[q], UI_T_SMALL);
        text(h, false, fit(h, title[q], UI_T_BODY, w - 26 - ww - 12), x + 20, y, UI_T_BODY, UI_INK);
        text(h, false, where[q], x + w - ww, y + 2, UI_T_SMALL, here[q] ? c : UI_DIM);
        // Progress: the bar and its words.
        float pw = text_w(h, false, prog[q], UI_T_SMALL);
        meter(x + 20, y + 24, w - 20 - pw - 10, 4, frac[q], c);
        text(h, false, prog[q], x + w - pw, y + 19, UI_T_SMALL, UI_DIM);
        // The tag (group, first steps) and the reward.
        const char* tag = q ? "Group" : Q->chain ? TextFormat("First steps %d/%d", Q->chain, CRET_CHAIN_STEPS) : NULL;
        float fx = x + 20;
        if (tag) { label(h, tag, fx, y + 36, UI_T_SMALL - 1, c); fx += label_w(h, tag, UI_T_SMALL - 1) + UI_GAP; }
        text(h, false, foot[q], fx, y + 35, UI_T_SMALL, UI_DIM);
        y += eh;
    }
    return bottom;
}

// What the in-world marker of a quest is called: the place, or what is at the spot.
static const char* marker_name(const CretSim* sim, const CretQuest* Q) {
    switch (Q->kind) {
    case CRET_Q_COMPANION: return CRET_SPECIES_TABLE[Q->species % CRET_SPECIES].name;
    case CRET_Q_SCAVENGE: return "Carcass";
    case CRET_Q_RAID: return "Nest";
    case CRET_Q_GUARD: return "Your nest";
    default: break;
    }
    if (Q->spot) return Q->kind == CRET_Q_DRINK ? "Water" : Q->kind == CRET_Q_CATCH ? "Small prey" : "Plants";
    int pi = Q->poi[Q->step < Q->steps ? Q->step : 0];
    return pi < sim->world->npois ? sim->world->pois[pi].name : "";
}

// In-world markers: the quest place (amber, group quest green), nests nearby.
static void world_markers(const CretHud* h, const CretSim* sim, int me, const float* vp) {
    const CretAgent* a = &sim->agents[me];
    if (!a->alive || !sim->world) return;
    for (int q = 0; q < 2; q++) {
        const CretQuest* Q = cret_quest_of(sim, me, q); float tx, tz, r, px, py;
        if (!cret_quest_target(sim, Q, &tx, &tz, &r)) continue;
        float d = hypotf(tx - a->x, tz - a->z);
        float ty = cret_height(sim->world, tx, tz) + 6 + fminf(d * 0.02f, 30);
        if (!project(vp, tx, ty, tz, &px, &py)) continue;
        Color c = q ? UI_GOOD : UI_ACCENT;
        icon(UI_I_DIAMOND, (Vector2){px, py}, 16, c);
        const char* nm = marker_name(sim, Q);
        const char* t = d < r ? TextFormat("%s  -  here", nm) : TextFormat("%s  %d m", nm, (int)d);
        text_sh(h, t, px - text_w(h, false, t, UI_T_SMALL + 1) * 0.5f, py + 13, UI_T_SMALL + 1, c);
    }
    // Nests: an icon and a label well above the nest (render/nest.c draws it),
    // fading out once the nest itself is in plain view close by.
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* n = &sim->nests[k]; if (!n->active) continue;
        float d = hypotf(n->x - a->x, n->z - a->z), px, py, R = cret_nest_radius(n->species);
        float fade = clampf((d - (4 + 2 * R)) / 10.0f, 0.25f, 1);
        if (d > 150 || !project(vp, n->x, n->y + 2.5f + 1.2f * R + d * 0.02f, n->z, &px, &py)) continue;
        bool mine = own_nest(sim, me, n);
        const char* t = TextFormat("%s nest  -  %d egg%s", mine ? "Your" : CRET_SPECIES_TABLE[n->species].name, n->eggs, n->eggs == 1 ? "" : "s");
        Color c = with(mine ? UI_GOOD : UI_DIM, fade);
        icon(UI_I_NEST, (Vector2){px, py}, 16, c);
        text_sh(h, t, px - text_w(h, false, t, UI_T_SMALL + 1) * 0.5f, py - 28, UI_T_SMALL + 1, c);
    }
}

// Combat feedback (h->fb, filled by render/fx.c): the red vignette and arcs
// toward attackers when hit; a hit marker on the reticle and the target's
// health bar when you land a blow.
static void reticle_pos(const CretSim* sim, int me, const float* vp, float* px, float* py) {
    const CretAgent* a = &sim->agents[me]; float sx, sz;
    cret_strike_point(a, &sx, &sz);
    if (!project(vp, sx, a->y + a->height * 0.55f, sz, px, py)) { *px = SW() * 0.5f; *py = SH() * 0.5f; }
}
static void draw_feedback(const CretHud* h, const CretSim* sim, int me, const float* vp, float cam_yaw) {
    const CretHudFeedback* f = h->fb; const CretAgent* a = &sim->agents[me];
    float sw = SW(), sh = SH(), cx = sw * 0.5f, cy = sh * 0.5f;
    if (a->alive && f->vignette > 0.01f) {
        unsigned char k = (unsigned char)(150 * clampf(f->vignette, 0, 1));
        Color c = {130, 0, 0, k}, z = {130, 0, 0, 0}; float bw = sw * 0.22f, bh = sh * 0.24f;
        DrawRectangleGradientH(0, 0, (int)bw, (int)sh + 1, c, z);
        DrawRectangleGradientH((int)(sw - bw), 0, (int)bw + 1, (int)sh + 1, z, c);
        DrawRectangleGradientV(0, 0, (int)sw + 1, (int)bh, c, z);
        DrawRectangleGradientV(0, (int)(sh - bh), (int)sw + 1, (int)bh + 1, z, c);
    }
    if (a->alive) for (int i = 0; i < 6; i++) {   // arcs toward where the blows came from (0 deg: ahead)
        if (f->dir[i].t > 1.4f) continue;
        float dx = f->dir[i].x - a->x, dz = f->dir[i].z - a->z;
        float fwd = dx * cosf(cam_yaw) + dz * sinf(cam_yaw), side = -dx * sinf(cam_yaw) + dz * cosf(cam_yaw);
        float ang = atan2f(side, fwd) * 57.29578f, al = clampf(1 - f->dir[i].t / 1.4f, 0, 1) * f->dir[i].k, R = fminf(sw, sh) * 0.33f;
        float w = 8 + 8 * f->dir[i].k, spread = 22 + 10 * f->dir[i].k;
        DrawRing((Vector2){cx, cy}, R - 1, R + w + 1, -90 + ang - spread - 1, -90 + ang + spread + 1, 28, (Color){0, 0, 0, (unsigned char)(70 * al)});
        DrawRing((Vector2){cx, cy}, R, R + w, -90 + ang - spread, -90 + ang + spread, 28, with(UI_DANGER, 0.92f * al));
    }
    float life = f->marker_kill ? 0.55f : 0.3f;
    if (a->alive && f->marker_t < life && !h->map_big) {   // hit marker: four ticks round the reticle, bigger for heavier blows
        float px, py; reticle_pos(sim, me, vp, &px, &py);
        float t = f->marker_t / life, pop = 1 + 0.35f * (1 - clampf(f->marker_t / 0.08f, 0, 1)), k = f->marker_k;
        float r0 = (10 + 6 * k) * pop, r1 = (24 + 16 * k) * pop * (f->marker_kill ? 1.25f : 1), th = 3.0f + 2.0f * k;
        unsigned char al = (unsigned char)(245 * (1 - t * t));
        Color c = f->marker_kill ? (Color){236, 44, 30, al} : (Color){250, 246, 236, al};
        for (int q = 0; q < 4; q++) {
            float ux = (q & 1) ? 0.7071f : -0.7071f, uy = (q & 2) ? 0.7071f : -0.7071f;
            Vector2 p0 = {px + ux * r0, py + uy * r0}, p1 = {px + ux * r1, py + uy * r1};
            seg(p0, p1, th + 3, (Color){0, 0, 0, (unsigned char)(al * 0.6f)});
            seg(p0, p1, th, c);
        }
    }
    if (a->alive && f->whiff_t < 0.35f && !h->map_big) {   // a whiff: an open grey ring that widens and fades at the reticle
        float px, py; reticle_pos(sim, me, vp, &px, &py);
        float t = f->whiff_t / 0.35f, r = 9 + 10 * t;
        unsigned char al = (unsigned char)(170 * (1 - t));
        for (int q = 0; q < 4; q++) {
            float a0 = q * 90.0f + 20, a1 = a0 + 50;
            DrawRing((Vector2){px, py}, r - 1.5f, r + 1.5f, a0, a1, 8, (Color){0, 0, 0, (unsigned char)(al / 2)});
            DrawRing((Vector2){px, py}, r - 0.75f, r + 0.75f, a0, a1, 8, (Color){200, 196, 186, al});
        }
    }
    if (f->target >= 0 && f->target < sim->cfg.num_agents && f->target != me && f->target_t > 0) {   // the animal you hit last
        const CretAgent* b = &sim->agents[f->target]; const CretSpecies* S = &CRET_SPECIES_TABLE[b->species];
        float al = clampf(f->target_t / 0.5f, 0, 1), w = 300, x = cx - w * 0.5f, y = h->touch ? 196 : 100;   // touch: under the vitals strip
        float hp = b->alive && b->max_health > 0 ? clampf(b->health / b->max_health, 0, 1) : 0, shown = fmaxf(hp, f->target_shown);
        const char* nm = !b->alive ? TextFormat("%s  -  dead", S->name) : b->growth < 0.6f ? TextFormat("Young %s", S->name) : S->name;
        panel_k((Rectangle){x - UI_PAD, y - 8, w + 2 * UI_PAD, 44}, al);
        text_c(h, false, nm, cx, y - 1, UI_T_BODY, with(UI_INK, al));
        DrawRectangleRounded((Rectangle){x, y + 21, w, 6}, 1, 4, (Color){0, 0, 0, (unsigned char)(140 * al)});
        DrawRectangleRec((Rectangle){x + w * hp, y + 21, w * (shown - hp), 6}, with(UI_INK, 0.85f * al));
        if (hp > 0) DrawRectangleRounded((Rectangle){x, y + 21, fmaxf(w * hp, 6), 6}, 1, 4, with(UI_HEALTH, al));
    }
}

// The vitals: an icon, a bar and the number. Calm while the stat is fine;
// below `low` the icon, bar and number turn to the warning colour and pulse.
static void vital(const CretHud* h, float x, float y, float w, int ic, float v, Color c, bool low, Color warn) {
    v = clampf(v, 0, 1);
    float k = low ? pulse() : 1;
    icon(ic, (Vector2){x + 8, y + 7}, 15, low ? with(warn, k) : UI_DIM);
    char n[8]; snprintf(n, sizeof n, "%d", (int)roundf(v * 100));
    float nw = text_w(h, false, n, UI_T_SMALL);
    meter(x + 24, y + 5, w - 24 - 30, UI_BAR, v, low ? with(warn, k) : c);
    text(h, false, n, x + w - nw, y + 1, UI_T_SMALL, low ? with(warn, k) : UI_DIM);
}
static const char* stage_of(float growth) { return growth >= 0.999f ? "Adult" : growth < 0.45f ? "Hatchling" : growth < 0.6f ? "Juvenile" : "Sub-adult"; }
// Injuries in words: "Bleeding", "Broken leg", both.
static const char* injuries(const CretAgent* a) {
    bool bleeding = a->bleed > 0.002f * a->max_health, broken = a->broken > 0;
    return bleeding && broken ? "Bleeding, broken leg" : bleeding ? "Bleeding" : broken ? "Broken leg" : NULL;
}
static void vitals_rows(const CretHud* h, const CretAgent* a, float x, float y, float w, int cols, float gap) {
    float hp = a->max_health > 0 ? a->health / a->max_health : 0;
    bool bleeding = a->bleed > 0.002f * a->max_health, broken = a->broken > 0;
    bool carn = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE;
    float cw = cols > 1 ? (w - gap) * 0.5f : w;
    for (int k = 0; k < 4; k++) {
        float rx = x + (cols > 1 ? (k & 1) * (cw + gap) : 0), ry = y + (cols > 1 ? (k >> 1) : k) * 22;
        if (k == 0) vital(h, rx, ry, cw, UI_I_HEART, hp, UI_HEALTH, bleeding || hp < 0.25f, UI_DANGER);
        if (k == 1) vital(h, rx, ry, cw, UI_I_LUNGS, a->stamina, broken ? with(UI_STAMINA, 0.5f) : UI_STAMINA, a->stamina < 0.15f, UI_WARN);
        if (k == 2) {   // the gut, and under it a thin line for the fat reserve (eco.h): starving only once both are empty
            vital(h, rx, ry, cw, carn ? UI_I_MEAT : UI_I_LEAF, a->food, UI_FOOD, a->food < 0.15f, a->fat > 0 ? UI_WARN : UI_DANGER);
            meter(rx + 24, ry + 5 + UI_BAR + 2, cw - 24 - 30, 2, a->fat, with(UI_FOOD, 0.6f));
        }
        if (k == 3) vital(h, rx, ry, cw, UI_I_DROP, a->water, UI_WATER, a->water < 0.15f, UI_DANGER);
    }
}
// Bottom left: the name, stage and growth, any injuries, and the four vitals.
static void vitals(const CretHud* h, const CretAgent* a) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    float w = 250, ph = 54 + 4 * 22, x = 28 + UI_PAD, y = SH() - 28 - ph + UI_PAD;
    panel((Rectangle){x - UI_PAD, y - UI_PAD, w + 2 * UI_PAD, ph});
    const char* inj = injuries(a);
    float iw = inj ? label_w(h, inj, UI_T_SMALL - 1) : 0;
    text(h, false, fit(h, h->character[0] ? h->character : S->name, UI_T_ROW - 2, w - iw - 12), x, y - 2, UI_T_ROW - 2, UI_INK);
    if (inj) label(h, inj, x + w - iw, y + 3, UI_T_SMALL - 1, with(UI_DANGER, pulse()));
    char gr[64];
    if (h->character[0]) snprintf(gr, sizeof gr, "%s %s  %d%%", stage_of(a->growth), S->name, (int)floorf(a->growth * 100));
    else snprintf(gr, sizeof gr, "%s  %d%%", stage_of(a->growth), (int)floorf(a->growth * 100));
    text(h, false, gr, x, y + 21, UI_T_SMALL, UI_DIM);
    vitals_rows(h, a, x, y + 44, w, 1, 0);
}
// Touch play: the same, compact, under the compass (the stick takes the bottom left).
static void vitals_compact(const CretHud* h, const CretAgent* a) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[a->species];
    float w = 400, x = SW() * 0.5f - w * 0.5f, y = 100;
    panel((Rectangle){x - UI_PAD, y - UI_PAD + 2, w + 2 * UI_PAD, 30 + 2 * 22 + 4});
    const char* inj = injuries(a);
    const char* who = h->character[0] ? TextFormat("%s   %s %s  %d%%", h->character, stage_of(a->growth), S->name, (int)floorf(a->growth * 100))
                                      : TextFormat("%s %s  %d%%", stage_of(a->growth), S->name, (int)floorf(a->growth * 100));
    float iw = inj ? label_w(h, inj, UI_T_SMALL - 1) + UI_GAP : 0;
    text(h, false, fit(h, who, UI_T_BODY - 1, w - iw), x, y - 2, UI_T_BODY - 1, UI_INK);
    if (inj) label(h, inj, x + w - iw + UI_GAP, y + 1, UI_T_SMALL - 1, with(UI_DANGER, pulse()));
    vitals_rows(h, a, x, y + 22, w, 2, 28);
}

void cret_hud_draw(CretHud* h, const CretSim* sim, int me, const float* viewproj, float cam_yaw) {
    scale_begin(h, 600);
    if (h->play_t0 <= 0) h->play_t0 = GetTime();
    float sw = SW(), sh = SH();
    const CretAgent* a = &sim->agents[me];
    hunt_update(h, sim, me);
    // Food prompt: what E does here, or where the meat is when hungry.
    if (a->alive && a->act != CRET_ACT_EAT) {
        float fd = 0; int fs = cret_food_state(sim, me, &fd);
        bool carn = CRET_SPECIES_TABLE[a->species].diet == CRET_DIET_CARNIVORE, tc = h->touch;
        Prompt p[2]; int np = 0; const char* hint = NULL;
        if (fs == 1 && (carn || a->food < 0.9f)) prompt_set(&p[np++], "E", carn ? UI_I_MEAT : UI_I_LEAF, carn ? "Eat" : "Graze", carn ? "the carcass" : NULL, NULL);
        else if (fs == 2) { prompt_set(&p[np++], NULL, UI_I_MEAT, "Carcass", TextFormat("%d m away", (int)fd), NULL); hint = tc ? "Walk up to it and hold Eat" : "Walk up to it and press E"; }
        else if (fs == 3) prompt_set(&p[np++], "LMB", UI_I_BITE, "Bite", "the small prey", NULL);   // critters.c: a bite catches it
        else if (carn && fs != 1 && fs != 2 && hunt_prompt(h, sim, me, cam_yaw, &p[np])) { hint = p[np].hint; np++; }   // small prey it noticed, and where (hunt_hud.c)
        // Carrying (sim.c): what B (the Carry button) does here. A full mouth replaces the eat prompt.
        int what = -1, cs = carn ? cret_carry_state(sim, me, &what) : 0;
        if (cs == 1) { np = 0; prompt_set(&p[np++], "B", UI_I_CARRY, "Put down", item_name(&sim->carcasses[what], true), NULL); hint = NULL; }
        else if (cs == 2 && np < 2) prompt_set(&p[np++], "B", UI_I_CARRY, "Pick up", item_name(&sim->carcasses[what], true), NULL);
        else if (cs == 3 && np < 2) prompt_set(&p[np++], "B", UI_I_CARRY, "Take an egg", NULL, NULL);
        else if (what <= -2 && np < 2) prompt_set(&p[np++], NULL, UI_I_CARRY, "Too heavy to carry", TextFormat("%s (%.0f kg; you lift %.0f kg)", item_name(&sim->carcasses[-what - 2], false),
                                                                                                             cret_item_kg(&sim->carcasses[-what - 2]), cret_carry_limit(a)), NULL);
        float y = sh * 0.72f;
        for (int k = 0; k < np; k++) y += prompt_draw(h, &p[k], sw * 0.5f, y) + 6;
        hint_draw(h, hint, sw * 0.5f, y + 2);
    } else h->hint[0] = 0;
    // Nameplates over other animals within 40-110 m (larger ones further):
    // the species and a thin health bar, fading out toward the range; group
    // members in green, further out, with their distance.
    for (int i = 0; i < sim->cfg.num_agents; i++) {
        const CretAgent* b = &sim->agents[i];
        if (i == me || !b->alive) continue;
        const CretSpecies* S = &CRET_SPECIES_TABLE[b->species];
        bool mate = a->group && b->group == a->group, marked = marked_for(sim, me, b);
        float d = hypotf(b->x - a->x, b->z - a->z), range = mate ? 300 : 40 + 6 * S->length * cret_growth_scale(b->growth);
        if (marked) range *= 2;
        if (d > range) continue;
        float px, py; if (!project(viewproj, b->x, b->y + b->height * 1.05f + 0.4f, b->z, &px, &py)) continue;
        float al = clampf((range - d) / (range * 0.4f), 0, 1) * (mate || marked ? 1 : 0.92f);
        const char* nm = b->growth < 0.6f ? TextFormat("Young %s", S->name) : S->name;
        if (marked) nm = TextFormat("%s  -  Marked %d m", nm, (int)d);
        else if (mate && d > 25) nm = TextFormat("%s  %d m", nm, (int)d);
        float ns = UI_T_SMALL + 1, tw = text_w(h, false, nm, ns), ix = marked ? 14 : 0;
        Color c = with(marked ? UI_DANGER : mate ? UI_GOOD : UI_INK, al);
        if (marked) icon(UI_I_MARK, (Vector2){px - (tw + ix) * 0.5f + 5, py + ns * 0.5f}, 11, c);
        text_sh(h, nm, px - (tw - ix) * 0.5f, py, ns, c);
        float hp = b->max_health > 0 ? clampf(b->health / b->max_health, 0, 1) : 1, bw = fminf(fmaxf(tw * 0.6f, 40), 70);
        DrawRectangleRec((Rectangle){px - bw * 0.5f - 1, py + 17, bw + 2, 5}, (Color){0, 0, 0, (unsigned char)(120 * al)});
        DrawRectangleRec((Rectangle){px - bw * 0.5f, py + 18, bw * hp, 3}, with(mate ? UI_GOOD : UI_HEALTH, al));
    }
    // Rally point: where a group member called from.
    if (a->alive && a->rally_ticks > 0 && sim->world) {
        float ry = cret_height(sim->world, a->rally_x, a->rally_z) + 3, px, py;
        float d = hypotf(a->rally_x - a->x, a->rally_z - a->z);
        if (d > 6 && project(viewproj, a->rally_x, ry, a->rally_z, &px, &py)) {
            DrawPolyLinesEx((Vector2){px, py}, 4, 9, 0, 2.5f, (Color){0, 0, 0, 90});
            DrawPolyLinesEx((Vector2){px, py}, 4, 8, 0, 2, UI_ACCENT);
            icon(UI_I_CALL, (Vector2){px + 2, py}, 9, UI_ACCENT);
            const char* t = TextFormat("Rally  %d m", (int)d);
            text_sh(h, t, px - text_w(h, false, t, UI_T_SMALL + 1) * 0.5f, py + 13, UI_T_SMALL + 1, UI_ACCENT);
        }
    }
    // Aim reticle: a faint dot where the primary blow lands, while another
    // animal is near enough to be hit there (dimmer while cooling down).
    // Round it, the primary's cooldown is a ring that closes as it runs out;
    // a click waiting in the queue (CRET_ATTACK_BUFFER, 0.3 s) lights the dot
    // up; a click the game threw away, because it came earlier than the queue
    // holds, flashes the ring red and shows it for a moment even with nothing
    // near, so a refused click never looks like a lost one.
    if (a->alive && !h->map_big) {
        const CretSpecies* SA = &CRET_SPECIES_TABLE[a->species]; float g = cret_growth_scale(a->growth), sx, sz, px, py;
        cret_strike_point(a, &sx, &sz);
        bool near = false;
        for (int i = 0; i < sim->cfg.num_agents && !near; i++)
            near = i != me && sim->agents[i].alive && cret_body_dist(&sim->agents[i], sx, sz) < SA->reach * g * 2.0f;
        int busy = a->act == CRET_ACT_ATTACK ? (int)(SA->attack_time * CRET_TICK_HZ) - a->act_ticks + 1 :
                   a->act == CRET_ACT_ATTACK2 ? (int)(SA->alt.attack_time * CRET_TICK_HZ) - a->act_ticks + 1 : 0;
        int wait = a->attack_cd > busy ? a->attack_cd : busy;   // ticks until a press would start a blow
        if (!h->menu && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && IsCursorHidden() && wait > CRET_ATTACK_BUFFER) h->refused_t = GetTime();
        float refused = clampf(1 - (float)(GetTime() - h->refused_t) / 0.6f, 0, 1);
        if ((near || refused > 0) && project(viewproj, sx, a->y + a->height * 0.55f, sz, &px, &py)) {
            bool queued = a->buf_act == CRET_ACT_ATTACK && a->buf_ticks > 0;
            unsigned char al = queued ? 235 : a->attack_cd > 0 ? 70 : 150;
            Vector2 c = {px, py};
            DrawCircleV(c, queued ? 4.0f : 3.0f, (Color){0, 0, 0, (unsigned char)(al / 2)});
            DrawCircleV(c, queued ? 3.0f : 2.0f, queued ? UI_ACCENT : (Color){240, 232, 210, al});
            float full = SA->cooldown * CRET_TICK_HZ, frac = full > 0 ? clampf(a->attack_cd / full, 0, 1) : 0;
            if (frac > 0) {
                Color rc = refused > 0 ? (Color){226, 70, 52, (unsigned char)(120 + 120 * refused)} : (Color){240, 232, 210, 110};
                DrawRing(c, 8.5f, 11.5f, -90, -90 + 360 * frac, 24, (Color){0, 0, 0, 70});
                DrawRing(c, 9.0f, 11.0f, -90, -90 + 360 * frac, 24, rc);
            }
        }
    }
    compass(h, cam_yaw, sim, me);
    minimap(h, sim, me);
    float gy = group_panel(h, sim, me);
    world_markers(h, sim, me, viewproj);
    float qy = h->map_big ? 64 : quest_tracker(h, sim, me);
    // The scent panel: under the minimap and group panel; on touch under the quest card, clear of the right-hand buttons.
    if (a->alive && !h->map_big) { hunt_marker(h, sim, me, viewproj, cam_yaw); hunt_scent_panel(h, sim, me, cam_yaw, h->touch ? 28 + UI_PAD : SW() - 300 - 28, h->touch ? qy : gy); }
    if (a->alive) ability_bar(h, sim, me);
    // Vitals, bottom left; under touch controls a compact strip below the
    // compass, as the stick takes the bottom left.
    if (h->touch) { if (!h->map_big) vitals_compact(h, a); } else vitals(h, a);
    // Damage flash at the screen edges (with combat feedback on: its vignette, arcs, marker and target bar).
    if (h->fb) draw_feedback(h, sim, me, viewproj, cam_yaw);
    else if (a->alive && a->hurt_ticks > 0) {
        unsigned char k = (unsigned char)(90 * clampf(a->hurt_ticks / 12.0f, 0, 1));
        DrawRectangleGradientH(0, 0, (int)(sw / 6), (int)sh, (Color){120, 0, 0, k}, (Color){120, 0, 0, 0});
        DrawRectangleGradientH((int)(sw - sw / 6), 0, (int)(sw / 6), (int)sh, (Color){120, 0, 0, 0}, (Color){120, 0, 0, k});
    }
    if (!a->alive) {   // the death screen: the countdown as a bar, and what to press
        int mid = (int)(sh * 0.5f);
        DrawRectangleGradientV(0, 0, (int)sw + 1, mid, (Color){4, 3, 3, 90}, (Color){12, 4, 3, 170});
        DrawRectangleGradientV(0, mid, (int)sw + 1, (int)sh + 1 - mid, (Color){12, 4, 3, 170}, (Color){4, 3, 3, 90});
        float cy = sh * 0.4f;
        text_c(h, true, "You died", sw * 0.5f, cy, UI_T_HEAD + 8, UI_INK);
        int left = (sim->cfg.respawn_ticks - a->dead_ticks + CRET_TICK_HZ - 1) / CRET_TICK_HZ;
        float done = sim->cfg.respawn_ticks > 0 ? clampf((float)a->dead_ticks / sim->cfg.respawn_ticks, 0, 1) : 1;
        const char* s = TextFormat("Respawning in %d s", left > 0 ? left : 0);
        text_c(h, false, s, sw * 0.5f, cy + 66, UI_T_BODY, UI_DIM);
        meter(sw * 0.5f - 140, cy + 90, 280, 3, done, with(UI_INK, 0.7f));
        if (h->touch) { if (h->can_respawn) text_c(h, false, "Tap to respawn now", sw * 0.5f, cy + 108, UI_T_BODY, UI_INK); }
        else {
            const char* keys[2] = {"Enter", "Esc"}; const char* verbs[2] = {"Respawn now", "Menu"};
            footer(h, h->can_respawn ? keys : keys + 1, h->can_respawn ? verbs : verbs + 1, h->can_respawn ? 2 : 1, sw * 0.5f, cy + 108);
        }
    }
    if (h->notice[0] && GetTime() < h->notice_until) {   // under the death text, or above the vitals
        float ny = !a->alive ? sh * 0.4f + 148 : h->touch ? 196 : sh - 252, nw = text_w(h, false, h->notice, UI_T_BODY);
        panel((Rectangle){sw * 0.5f - nw * 0.5f - UI_PAD - 6, ny - 9, nw + 2 * UI_PAD + 12, 34});
        DrawRectangleRounded((Rectangle){sw * 0.5f - nw * 0.5f - UI_PAD - 6, ny - 9, 3, 34}, 1, 2, UI_ACCENT);
        text(h, false, h->notice, sw * 0.5f - nw * 0.5f, ny, UI_T_BODY, UI_INK);
    }
    scale_end();
}

// ---------------------------------------------------------------- key help
// One list for the key bindings page and the F1 help.
static const char* const HELP_KEYS[] = {"W A S D", "Shift", CRET_WALK_KEY_NAME, "Space", "R", "E", "G", "LMB", "RMB Q", "1 2 3", "V", "B", "N",
                                    "Mouse", "Wheel", "M", "Tab", "Esc", "F", "T", "F1"};
static const char* const HELP_WHAT[] = {"Move (relative to the camera)", "Sprint", "Walk", "Jump (where the species can)", "Autorun",
                                    "Eat, or drink when facing water (hold)", "Call: rally and form a group", "Attack", "Tail swipe or slash (tap)",
                                    "Abilities (slot 3 is bought with marks in the Esc menu)", "Sniff for prey and carcasses (hold)", "Carry or put down food",
                                    "Build a nest (hold; a fed adult in a group)", "Look; under WSL hold the right button and drag", "Camera distance",
                                    "Island map", "Free the cursor", "Menu", "Free camera", "Time x60 (hold)", "Key help"};
int cret_ui_keys(const char* const** keys, const char* const** what) { *keys = HELP_KEYS; *what = HELP_WHAT; return (int)(sizeof HELP_KEYS / sizeof HELP_KEYS[0]); }

void cret_hud_help(CretHud* h, bool open, bool drag, const char* dev_keys) {
    if (h->touch || h->menu) return;
    scale_begin(h, 600);
    float sw = SW(), sh = SH();
    if (!open) {   // the first minute of play: a quiet pointer to the help
        float t = h->play_t0 > 0 ? (float)(GetTime() - h->play_t0) : 0, k = 1 - clampf((t - UI_HELP_HINT) / 0.6f, 0, 1);
        if (k > 0) {
            A = k; float vw = text_w(h, false, "Help", UI_T_SMALL + 1), x = sw - 28 - vw - keycaps_w(h, "F1") - UI_GAP, y = sh - 28 - KEY_H;
            keycaps(h, "F1", x, y); text_sh(h, "Help", sw - 28 - vw, y + (KEY_H - UI_T_SMALL - 1) * 0.5f, UI_T_SMALL + 1, UI_DIM);
        }
        scale_end(); return;
    }
    int n = (int)(sizeof HELP_KEYS / sizeof HELP_KEYS[0]), half = (n + 1) / 2;
    float cw = 400, rh = 30, w = 2 * cw + 40, ph = 70 + half * rh + (dev_keys ? 40 : 0), x = sw * 0.5f - w * 0.5f, y = fmaxf(20, sh * 0.5f - ph * 0.5f);
    panel_k((Rectangle){x - 20, y - 16, w + 40, ph + 16}, 1.3f);
    text(h, false, "Keys", x, y, UI_T_ROW, UI_INK);
    const char* close[1] = {"F1"}; const char* cv[1] = {"Close"};
    footer(h, close, cv, 1, x + w - 50, y);
    y += 44;
    for (int i = 0, row = 0; i < n; i++) {
        if (drag && !strcmp(HELP_KEYS[i], "Tab")) continue;   // the drag scheme never holds the cursor
        float cx = x + (row >= half ? cw + 40 : 0), cy = y + (row % half) * rh;
        keycaps(h, HELP_KEYS[i], cx, cy);
        text(h, false, fit(h, HELP_WHAT[i], UI_T_SMALL + 1, cw - 118), cx + 110, cy + (KEY_H - UI_T_SMALL - 1) * 0.5f, UI_T_SMALL + 1, UI_INK);
        row++;
    }
    if (dev_keys) {
        float dy = y + half * rh + 12;
        DrawRectangleRec((Rectangle){x, dy - 6, w, 1}, fa(UI_FAINT));
        label(h, "Developer", x, dy + 4, UI_T_SMALL - 1, UI_DIM);
        text(h, false, dev_keys, x + 110, dy + 2, UI_T_SMALL + 1, UI_DIM);
    }
    scale_end();
}

// ---------------------------------------------------------------- menu and credits
static CretHudAction menu(CretHud* h, const CretSim* sim, int me);
static void heading(const CretHud* h, const char* s, float x, float y, float w) {
    label(h, s, x, y, UI_T_SMALL, UI_DIM);
    float lw = label_w(h, s, UI_T_SMALL) + UI_GAP;
    if (w > lw) DrawRectangleRec((Rectangle){x + lw, y + UI_T_SMALL * 0.5f, w - lw, 1}, fa(UI_FAINT));
}
static void draw_credits(CretHud* h) {
    float sw = SW(), sh = SH();
    DrawRectangle(0, 0, (int)sw + 1, (int)sh + 1, fa((Color){8, 8, 7, 215}));
    float x = sw * 0.5f - 360, y = sh * 0.12f, w = 760;
    text(h, true, "Credits", x, y, UI_T_HEAD, UI_INK); y += 70;
    heading(h, "Dinosaurs", x, y, w); y += 28;
    for (int r = 0; r < h->ncredits; r++) {
        text(h, false, h->credit[r][0], x, y, UI_T_BODY + 2, UI_INK);
        text(h, false, TextFormat("\"%s\" by %s", h->credit[r][1], h->credit[r][2]), x + 200, y, UI_T_BODY + 1, UI_INK);
        text(h, false, h->credit[r][3], x + 200, y + 21, UI_T_SMALL + 1, UI_DIM);
        y += 50;
    }
    text(h, false, "Licensed under Creative Commons Attribution 4.0 (creativecommons.org/licenses/by/4.0).", x, y, UI_T_SMALL + 1, UI_DIM); y += 18;
    text(h, false, "Models were rescaled, decimated, re-timed and re-textured for the game.", x, y, UI_T_SMALL + 1, UI_DIM); y += 42;
    heading(h, "World", x, y, w); y += 28;
    text(h, false, "Terrain textures: ambientCG and Poly Haven (CC0)", x, y, UI_T_BODY + 1, UI_INK); y += 24;
    text(h, false, "Rocks, logs and stumps: Poly Haven (CC0)", x, y, UI_T_BODY + 1, UI_INK); y += 24;
    text(h, false, "Leaf, frond and bark photographs: CC0 sources listed in assets/foliage/SOURCES.md", x, y, UI_T_BODY + 1, UI_INK); y += 24;
    text(h, false, "Sounds: CC0 field and animal recordings from Freesound, listed in assets/audio/SOURCES.md", x, y, UI_T_BODY + 1, UI_INK); y += 24;
    text(h, false, "Thank you to Poly Haven, ambientCG and the Freesound recordists for releasing their work into the public domain.", x, y, UI_T_SMALL + 1, UI_DIM); y += 42;
    heading(h, "Built with", x, y, w); y += 28;
    text(h, false, "raylib, Box3D, ENet, PufferLib", x, y, UI_T_BODY + 1, UI_INK);
    const char* keys[1] = {"Esc"}; const char* verbs[1] = {cret_touch_active() ? "Tap to go back" : "Back"};
    if (cret_touch_active()) text_c(h, false, verbs[0], sw * 0.5f, sh - 48, UI_T_SMALL + 1, UI_DIM);
    else footer(h, keys, verbs, 1, sw * 0.5f, sh - 52);
}

void cret_hud_draw_credits(CretHud* h) { scale_begin(h, 880); draw_credits(h); scale_end(); }

// ---------------------------------------------------------------- input method and selection
// The last way the player worked a menu: keys, the mouse or touch.
static int g_input = CRET_UI_KEYS;
static void input_watch(void) {
    if (cret_touch_active()) { g_input = CRET_UI_TOUCH; return; }
    if (g_input == CRET_UI_TOUCH) g_input = CRET_UI_MOUSE;
    Vector2 d = GetMouseDelta();
    if (d.x != 0 || d.y != 0 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || GetMouseWheelMove() != 0) g_input = CRET_UI_MOUSE;
    static const int K[] = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_W, KEY_A, KEY_S, KEY_D, KEY_ENTER, KEY_SPACE, KEY_ESCAPE};
    for (int i = 0; i < (int)(sizeof K / sizeof K[0]); i++) if (IsKeyPressed(K[i])) g_input = CRET_UI_KEYS;
}
int cret_ui_input(void) { return g_input; }

// The selection slides from row to row: its box eases to the selected
// row's within about UI_FADE, and snaps when the menu was not drawn the
// frame before (a new page) or the jump is far.
static struct { Rectangle r; double seen; } g_sel;
static void select_box(Rectangle box) {
    double now = GetTime(); float dt = GetFrameTime();
    bool snap = now - g_sel.seen > 0.25 || fabsf(box.x - g_sel.r.x) > 400 || fabsf(box.y - g_sel.r.y) > 400 || g_sel.r.width <= 0;
    float k = snap ? 1 : 1 - expf(-dt / (UI_FADE * 0.3f));
    g_sel.r.x += (box.x - g_sel.r.x) * k; g_sel.r.y += (box.y - g_sel.r.y) * k;
    g_sel.r.width += (box.width - g_sel.r.width) * k; g_sel.r.height += (box.height - g_sel.r.height) * k; g_sel.seen = now;
    Rectangle r = g_sel.r;
    DrawRectangleGradientH((int)r.x, (int)r.y, (int)r.width, (int)r.height, fa(with(UI_HILITE, 2.6f)), fa(with(UI_HILITE, 0.4f)));
    DrawRectangleRounded((Rectangle){r.x, r.y + 5, 3, r.height - 10}, 1, 2, fa(UI_ACCENT));
}
void cret_ui_select(Rectangle box) { select_box(box); }

// ---------------------------------------------------------------- stepped values, sliders, switches
// A value that steps ("<  value  >") fills a box w wide: "<" at its left end,
// ">" at its right end, the value centred between. A click on the left half
// steps back and on the right half forward. With ends (a bar between the
// arrows, which takes clicks itself) only the arrows step.
static bool step_zone(Vector2 m, float x, float y, float w, float size, bool ends, int side) {
    if (m.y < y - 8 || m.y > y + size + 8 || m.x < x - 12 || m.x > x + w + 12) return false;
    float aw = size * 0.6f + 12;   // an arrow and its margin
    if (ends) return side < 0 ? m.x < x + aw : m.x > x + w - aw;
    return side < 0 ? m.x < x + w * 0.5f : m.x >= x + w * 0.5f;
}
static int step_hit(Vector2 m, bool click, float x, float y, float w, float size, bool ends) {
    if (!click) return 0;
    return step_zone(m, x, y, w, size, ends, -1) ? -1 : step_zone(m, x, y, w, size, ends, 1) ? 1 : 0;
}
// The arrows are drawn chevrons, always shown: in the row's colour, amber
// on the selected row, and the one under the pointer sits on a soft disc so
// it reads as a button. A value too long for the room between them is drawn
// smaller. value NULL: arrows only (the caller draws a bar between them).
static void stepper(const CretHud* h, const char* value, float x, float y, float w, float size, Color c, Vector2 m) {
    bool lit_row = c.r == UI_INK.r && c.a == UI_INK.a;
    for (int side = -1; side <= 1; side += 2) {
        float gx = side < 0 ? x + size * 0.3f : x + w - size * 0.3f, gy = y + size * 0.52f;
        bool lit = step_zone(m, x, y, w, size, !value, side);
        if (lit) DrawCircleV((Vector2){gx, gy}, size * 0.62f, fa((Color){255, 255, 255, 36}));
        Pen p = {{gx, gy}, size * 0.62f, 2.0f, fa(lit ? UI_INK : lit_row ? UI_ACCENT : with(UI_DIM, c.a / 190.0f))};
        ln(&p, -0.12f * side, -0.32f, 0.14f * side, 0); ln(&p, 0.14f * side, 0, -0.12f * side, 0.32f);
    }
    if (!value) return;
    float room = w - 2 * (size * 0.6f + 14), vs = size, vw = text_w(h, false, value, vs);
    if (vw > room) { vs = fmaxf(12, size * room / vw); vw = text_w(h, false, value, vs); }
    text(h, false, value, x + (w - vw) * 0.5f, y + (size - vs) * 0.5f, vs, c);
}
void cret_ui_slider(float x, float y, float w, float size, float v, bool lit) {
    float cy = y + size * 0.55f, t = 4; v = clampf(v, 0, 1);
    DrawRectangleRounded((Rectangle){x, cy - t * 0.5f, w, t}, 1, 4, fa((Color){255, 255, 255, 34}));
    DrawRectangleRounded((Rectangle){x, cy - t * 0.5f, fmaxf(w * v, t), t}, 1, 4, fa(lit ? UI_ACCENT : with(UI_INK, 0.6f)));
    Vector2 k = {x + w * v, cy};
    DrawCircleV(k, 7.5f, fa((Color){0, 0, 0, 90}));
    DrawCircleV(k, 6.5f, fa(lit ? UI_INK : with(UI_INK, 0.75f)));
}
void cret_ui_toggle(float x, float y, float size, bool on, bool lit) {
    float h = size * 0.9f, w = h * 1.8f, cy = y + size * 0.55f;
    Rectangle r = {x, cy - h * 0.5f, w, h};
    DrawRectangleRounded(r, 1, 8, fa(on ? with(lit ? UI_ACCENT : UI_INK, lit ? 0.85f : 0.5f) : (Color){255, 255, 255, 30}));
    DrawRectangleRoundedLinesEx(r, 1, 8, 1.2f, fa(with(UI_INK, lit ? 0.5f : 0.3f)));
    DrawCircleV((Vector2){on ? x + w - h * 0.5f : x + h * 0.5f, cy}, h * 0.5f - 2.5f, fa(on || lit ? UI_INK : UI_DIM));
}

// ---------------------------------------------------------------- shared with menu.c
void cret_ui_begin(CretHud* h) { scale_begin(h, 880); input_watch(); }
void cret_ui_end(void) { scale_end(); }
float cret_ui_w(void) { return SW(); }
float cret_ui_h(void) { return SH(); }
Vector2 cret_ui_mouse(void) { return mouse(); }
bool cret_ui_tap(Vector2* at) {
    Vector2 p; if (!cret_touch_tap(&p)) return false;
    if (at) *at = (Vector2){p.x / U, p.y / U};
    return true;
}
void cret_ui_fade(float a) { A = clampf(a, 0, 1); }
Color cret_ui_fa(Color c) { return fa(c); }
void cret_ui_text(const CretHud* h, bool big, const char* s, float x, float y, float size, Color c) { text(h, big, s, x, y, size, c); }
float cret_ui_text_w(const CretHud* h, bool big, const char* s, float size) { return text_w(h, big, s, size); }
void cret_ui_label(const CretHud* h, const char* s, float x, float y, float size, Color c) { label(h, s, x, y, size, c); }
float cret_ui_label_w(const CretHud* h, const char* s, float size) { return label_w(h, s, size); }
void cret_ui_heading(const CretHud* h, const char* s, float x, float y, float w) { heading(h, s, x, y, w); }
void cret_ui_panel(Rectangle r) { panel(r); }
void cret_ui_footer(const CretHud* h, const char* const* keys, const char* const* verbs, int n, float x, float y, bool centre) { footer_at(h, keys, verbs, n, x, y, centre); }
void cret_ui_title(const CretHud* h, const char* s, float x, float y, float size, Color c) {
    char b[64]; caps(s, b, sizeof b); c = fa(c);
    if (h->font_ok) DrawTextEx(h->font_big, b, (Vector2){roundf(x * U) / U, roundf(y * U) / U}, size, size * 0.16f, c);
    else DrawText(b, (int)x, (int)y, (int)size, c);
}
void cret_ui_credits(CretHud* h) { draw_credits(h); }
int cret_ui_step_hit(Vector2 m, bool click, float x, float y, float w, float size, bool ends) { return step_hit(m, click, x, y, w, size, ends); }
void cret_ui_stepper(const CretHud* h, const char* value, float x, float y, float w, float size, Color c, Vector2 m) { stepper(h, value, x, y, w, size, c, m); }

CretHudAction cret_hud_menu(CretHud* h, const CretSim* sim, int me) {
    scale_begin(h, h->credits ? 880 : 600); input_watch();
    double now = GetTime();
    if (now - h->menu_seen > 0.25) h->menu_t0 = now;   // just opened: fade in
    h->menu_seen = now;
    A = clampf((float)(now - h->menu_t0) / UI_FADE, 0, 1);
    CretHudAction act = menu(h, sim, me);
    scale_end();
    return act;
}

// The pause menu. Rows keep their places (tools/web_smoke.js taps Resume):
// labels from x = middle - 220, the first at 32% of the height, 46 apart.
static CretHudAction menu(CretHud* h, const CretSim* sim, int me) {
    if (h->species < 0) h->species = sim->agents[me].species;
    if (h->credits) {
        draw_credits(h);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || cret_ui_tap(NULL)) h->credits = false;
        return HUD_NONE;
    }
    float sw = SW(), sh = SH();
    DrawRectangleGradientH(0, 0, (int)sw + 1, (int)sh + 1, fa((Color){6, 6, 5, 150}), fa((Color){6, 6, 5, 110}));
    enum { R_RESUME, R_SPECIES, R_RESPAWN, R_ABILITY, R_SETTINGS, R_CREDITS, R_QUIT, R_COUNT };
    const char* rows[R_COUNT] = {"Resume", "Species", "Respawn", "Ability 3", "Settings", "Credits", "Quit"};
    // Slot 3: one of two abilities, bought with marks (Enter or a click on the label buys or equips).
    const CretAgent* me_a = &sim->agents[me]; const CretSpeciesAbilities* P = &CRET_SPECIES_ABILITIES[me_a->species % CRET_SPECIES];
    h->choice = h->choice & 1; int ab3 = P->ab[2 + h->choice];
    bool owned = cret_choice_unlocked(sim, me, h->choice), equipped = owned && sim->prog[me].loadout[2] == ab3;
    char abv[96]; snprintf(abv, sizeof abv, "%s  %s", CRET_ABILITIES[ab3].name, equipped ? "(equipped)" : owned ? "(owned)" : TextFormat("(%d marks)", P->cost[h->choice]));
    // The species and ability values step: "<" and ">" either side of the value, right of the label.
    float x = sw * 0.5f - 220, y0 = sh * 0.32f, rh = 46, bw = 460, vx = x + 124, vw = bw - 152;
    // Short windows (a phone held sideways) leave out the notes and the controls under the rows.
    float py = y0 - 92; bool notes = y0 + R_COUNT * rh + 112 < sh; float ph = R_COUNT * rh + 92 + (notes ? 112 : 4);
    panel((Rectangle){x - 40, py - 20, bw + 48, ph + 20});
    text(h, true, "Paused", x, py, UI_T_HEAD, UI_INK);
    label(h, TextFormat("%d marks", sim->prog[me].marks), x + bw - 32 - label_w(h, TextFormat("%d marks", sim->prog[me].marks), UI_T_SMALL), py + 22, UI_T_SMALL, UI_ACCENT);
    CretHudAction act = HUD_NONE;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) h->sel = (h->sel + 1) % R_COUNT;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) h->sel = (h->sel + R_COUNT - 1) % R_COUNT;
    Vector2 m = mouse(); bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    Vector2 tp; bool tap = cret_ui_tap(&tp);   // a tap selects and picks its row at once
    if (tap) { m = tp; click = true; }
    for (int r = 0; r < R_COUNT; r++) {
        float y = y0 + r * rh;
        Rectangle box = {x - 16, y - 8, bw, rh - 8};
        bool hover = CheckCollisionPointRec(m, box), steps = r == R_SPECIES || r == R_ABILITY;
        if (hover && (tap || GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) h->sel = r;
        bool disabled = ((r == R_RESPAWN || r == R_SPECIES) && !h->can_respawn) || (r == R_ABILITY && !me_a->alive);
        int step = steps && !disabled ? step_hit(m, click, vx, y, vw, UI_T_ROW, false) : 0;
        if (step) h->sel = r;
        bool on = h->sel == r;
        if (on) select_box(box);
        Color c = disabled ? UI_FAINT : on ? UI_INK : UI_DIM;
        text(h, false, rows[r], x, y, UI_T_ROW, c);
        if (steps) stepper(h, r == R_SPECIES ? CRET_SPECIES_TABLE[h->species].name : abv, vx, y + 1, vw, UI_T_ROW - 2, c, disabled ? (Vector2){-1e4f, -1e4f} : m);
        if (disabled) continue;
        // A click on the label picks the row; one on the value steps it (above).
        bool pick = on && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || (click && hover && !step && !(steps && m.x >= vx - 12)));
        if (r == R_SPECIES) {
            if (on && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))) step = -1;
            if (on && (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) step = 1;
            h->species = (h->species + CRET_SPECIES + step) % CRET_SPECIES;
        } else if (r == R_ABILITY) {
            if (on && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))) step = 1;
            if (step) h->choice ^= 1;   // two choices: either arrow shows the other
            else if (pick && !equipped) act = HUD_ABILITY;
        } else if (pick) {
            if (r == R_RESUME) act = HUD_RESUME;
            else if (r == R_RESPAWN) act = HUD_RESPAWN;
            else if (r == R_SETTINGS) act = HUD_SETTINGS;
            else if (r == R_CREDITS) h->credits = true;
            else if (r == R_QUIT) act = HUD_QUIT;
        }
    }
    if (IsKeyPressed(KEY_ESCAPE)) act = HUD_RESUME;
    if (!notes) return act;
    // What the selected row means: respawning, or the ability on offer.
    float hy = y0 + R_COUNT * rh + 4;
    DrawRectangleRec((Rectangle){x, hy, bw - 56, 1}, fa(UI_FAINT));
    icon(ability_icon(ab3), (Vector2){x + 9, hy + 22}, 16, UI_DIM);
    text(h, false, fit(h, TextFormat("%s:  %s", CRET_ABILITIES[ab3].name, CRET_ABILITIES[ab3].desc), UI_T_SMALL + 1, bw - 84), x + 26, hy + 15, UI_T_SMALL + 1, UI_DIM);
    text(h, false, "Respawning starts a new life as the chosen species.", x + 26, hy + 36, UI_T_SMALL + 1, UI_DIM);
    if (g_input == CRET_UI_TOUCH) text_c(h, false, "Tap a row, or the < or > of a value", x + bw * 0.5f - 28, py + ph - 26, UI_T_SMALL + 1, UI_DIM);
    else {
        const char* keys[3] = {"Up Down", "Left Right", "Esc"}; const char* verbs[3] = {"Choose", "Change", "Resume"};
        footer(h, keys, verbs, 3, x + bw * 0.5f - 28, py + ph - 38);
    }
    return act;
}

#include "touch.c"   // compiled here: build.sh lists the player sources outside ocean/cretaceous
