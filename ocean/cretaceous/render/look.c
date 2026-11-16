// The look (look.h): the knob table, look files and the shipped looks.
// Compiled from view.c (build.sh lists the player sources outside ocean/cretaceous).
#include "look.h"
#include "gfx.h"
#include "../menu.h"        // cret_settings_dir
#include "../character.h"   // cret_config_saved (web: the page keeps the config directory)
#include "raylib.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* const CRET_LOOK_SECTIONS[LKS_COUNT] = {"Exposure and tone", "Colour", "Effects", "Atmosphere", "Light", "Foliage"};
const char* const CRET_LOOK_TONEMAPS[LK_TM_COUNT] = {"Game (ACES)", "AgX", "PBR Neutral", "Hable", "Reinhard"};

// Defaults are today's values: the image at the defaults is the image before the studio.
const CretLookKnob CRET_LOOK_KNOBS[LK_COUNT] = {
    [LK_EXPOSURE]    = {"exposure", "Exposure", "Brighter or darker overall, in stops", LKS_TONE, -3, 3, 0, 0.05f, LKF_POST, "%+.2f EV"},
    [LK_ADAPT]       = {"eye_adaptation", "Eye adaptation", "How far exposure follows what you look at (0 fixed)", LKS_TONE, 0, 1.5f, 1, 0.05f, LKF_POST, "%.2f"},
    [LK_ADAPT_SPEED] = {"adaptation_speed", "Adaptation speed", "How quickly the eye adjusts, per second", LKS_TONE, 0.25f, 10, 2.5f, 0.05f, 0, "%.2f"},
    [LK_NIGHT_DROP]  = {"night_exposure", "Night darkening", "Stops the moonlit frame is darkened", LKS_TONE, 0, 4, 1.8f, 0.05f, LKF_POST, "%.2f"},
    [LK_TONEMAP]     = {"tonemapper", "Tone curve", "How bright light rolls off to white", LKS_TONE, 0, LK_TM_COUNT - 1, 0, 1, LKF_POST | LKF_CHOICE, NULL},
    [LK_WHITE]       = {"white_point", "White point", "Lower: highlights reach white sooner", LKS_TONE, 1, 11.2f, 11.2f, 0.1f, LKF_POST, "%.1f"},
    [LK_CONTRAST]    = {"contrast", "Contrast", "Darker shadows and brighter lit surfaces (by day)", LKS_TONE, -0.5f, 1, 0.3f, 0.01f, LKF_POST, "%.2f"},
    [LK_TOE]         = {"toe", "Toe", "Right: deeper blacks; left: lifted, softer blacks", LKS_TONE, -1, 1, 0, 0.01f, LKF_POST, "%+.2f"},
    [LK_SHOULDER]    = {"shoulder", "Shoulder", "Right: brighter highlights; left: softer highlights", LKS_TONE, -1, 1, 0, 0.01f, LKF_POST, "%+.2f"},
    [LK_SATURATION]  = {"saturation", "Saturation", "Strength of every colour", LKS_COLOUR, 0, 2, 1, 0.01f, LKF_POST, "%.2f"},
    [LK_VIBRANCE]    = {"vibrance", "Vibrance", "Strength of dull colours only (keeps skin and vivid ones)", LKS_COLOUR, -1, 1, 0, 0.01f, LKF_POST, "%+.2f"},
    [LK_TEMPERATURE] = {"temperature", "Temperature", "White balance: left cooler (blue), right warmer", LKS_COLOUR, -1, 1, 0, 0.01f, LKF_POST, "%+.2f"},
    [LK_TINT]        = {"tint", "Tint", "White balance: left greener, right more magenta", LKS_COLOUR, -1, 1, 0, 0.01f, LKF_POST, "%+.2f"},
    [LK_LIFT]        = {"lift", "Shadows", "Raise or lower the darkest tones", LKS_COLOUR, -0.2f, 0.2f, 0, 0.005f, LKF_POST, "%+.3f"},
    [LK_LIFT_HUE]    = {"lift_hue", "Shadows colour", "The colour shadows lean toward", LKS_COLOUR, 0, 360, 220, 1, LKF_POST, "%.0f"},
    [LK_LIFT_COLOR]  = {"lift_color", "Shadows colour amount", "How far they lean (0 none)", LKS_COLOUR, 0, 1, 0, 0.01f, LKF_POST, "%.2f"},
    [LK_GAMMA]       = {"gamma", "Midtones", "Raise or lower the middle tones", LKS_COLOUR, -0.5f, 0.5f, 0, 0.01f, LKF_POST, "%+.2f"},
    [LK_GAMMA_HUE]   = {"gamma_hue", "Midtones colour", "The colour midtones lean toward", LKS_COLOUR, 0, 360, 40, 1, LKF_POST, "%.0f"},
    [LK_GAMMA_COLOR] = {"gamma_color", "Midtones colour amount", "How far they lean (0 none)", LKS_COLOUR, 0, 1, 0, 0.01f, LKF_POST, "%.2f"},
    [LK_GAIN]        = {"gain", "Highlights", "Raise or lower the brightest tones", LKS_COLOUR, -0.5f, 0.5f, 0, 0.01f, LKF_POST, "%+.2f"},
    [LK_GAIN_HUE]    = {"gain_hue", "Highlights colour", "The colour highlights lean toward", LKS_COLOUR, 0, 360, 45, 1, LKF_POST, "%.0f"},
    [LK_GAIN_COLOR]  = {"gain_color", "Highlights colour amount", "How far they lean (0 none)", LKS_COLOUR, 0, 1, 0, 0.01f, LKF_POST, "%.2f"},
    [LK_GRADE]       = {"grade", "Warm light, cool shade", "The game's grade: warm sun, cool shadows, lifted shade", LKS_COLOUR, 0, 2, 1, 0.05f, LKF_POST, "%.2f"},
    [LK_GREEN_HUE]   = {"green_hue", "Greens: hue", "Left: yellower greens; right: bluer greens", LKS_COLOUR, -30, 30, 0, 0.5f, LKF_POST, "%+.1f"},
    [LK_GREEN_SAT]   = {"green_saturation", "Greens: saturation", "Strength of the greens only", LKS_COLOUR, 0, 2, 1, 0.01f, LKF_POST, "%.2f"},
    [LK_BLOOM]       = {"bloom", "Bloom", "Glow round bright light (sun, sky through leaves)", LKS_EFFECTS, 0, 0.5f, 0.1f, 0.005f, LKF_POST, "%.3f"},
    [LK_BLOOM_THRESHOLD] = {"bloom_threshold", "Bloom threshold", "How bright light must be to glow", LKS_EFFECTS, 0.5f, 16, 4, 0.1f, 0, "%.1f"},
    [LK_BLOOM_RADIUS]    = {"bloom_radius", "Bloom radius", "How far the glow spreads", LKS_EFFECTS, 0.25f, 3, 1, 0.05f, 0, "%.2f"},
    [LK_SHARPEN]     = {"sharpen", "Sharpen", "Crisper texture (high and ultra at full resolution)", LKS_EFFECTS, 0, 1.5f, 0.6f, 0.05f, LKF_POST, "%.2f"},
    [LK_VIGNETTE]    = {"vignette", "Vignette", "Darker corners", LKS_EFFECTS, 0, 3, 1, 0.05f, LKF_POST, "%.2f"},
    [LK_AO]          = {"ambient_occlusion", "Contact shade", "Shade in creases and where things meet (high, medium)", LKS_EFFECTS, 0, 2, 1, 0.05f, LKF_POST, "%.2f"},
    [LK_AO_RADIUS]   = {"ambient_occlusion_radius", "Contact shade radius", "How far that shade reaches", LKS_EFFECTS, 0.25f, 3, 1, 0.05f, 0, "%.2f"},
    [LK_GRAIN]       = {"film_grain", "Film grain", "A fine moving grain (0 off)", LKS_EFFECTS, 0, 1, 0, 0.01f, LKF_POST, "%.2f"},
    [LK_HAZE]        = {"haze", "Haze", "Distance fading into the sky colour", LKS_ATMOS, 0, 3, 1, 0.05f, 0, "%.2f"},
    [LK_HAZE_WARMTH] = {"haze_warmth", "Haze colour", "Left: bluer haze; right: warmer haze", LKS_ATMOS, -1, 1, 0, 0.01f, 0, "%+.2f"},
    [LK_MIST]        = {"forest_mist", "Forest mist", "Pale mist between the trunks inside a forest", LKS_ATMOS, 0, 3, 1, 0.05f, LKF_POST, "%.2f"},
    [LK_SKY]         = {"sky_brightness", "Sky brightness", "The sky you see (not the light it gives)", LKS_ATMOS, 0.25f, 3, 1, 0.05f, LKF_POST, "%.2f"},
    [LK_CLOUDS]      = {"cloud_brightness", "Cloud brightness", "How bright the clouds are", LKS_ATMOS, 0.25f, 2, 1, 0.05f, LKF_POST, "%.2f"},
    [LK_RAIN_HAZE]   = {"rain_haze", "Rain haze", "Extra haze under overcast and rain", LKS_ATMOS, 0, 3, 1, 0.05f, 0, "%.2f"},
    [LK_SUN]         = {"sun", "Sunlight", "Strength of direct sun", LKS_LIGHT, 0, 3, 1, 0.05f, 0, "%.2f"},
    [LK_SUN_WARMTH]  = {"sun_warmth", "Sun colour", "Left: whiter, cooler sun; right: warmer sun", LKS_LIGHT, -1, 1, 0, 0.01f, 0, "%+.2f"},
    [LK_AMBIENT]     = {"sky_light", "Sky light", "Light from the sky: fills shade and haze (and the sky)", LKS_LIGHT, 0, 3, 1, 0.05f, 0, "%.2f"},
    [LK_BOUNCE]      = {"ground_bounce", "Ground bounce", "Sunlight bounced up from the ground onto plants", LKS_LIGHT, 0, 4, 1, 0.05f, 0, "%.2f"},
    [LK_SHADOW_SOFT] = {"shadow_softness", "Shadow softness", "Softer, wider shadow edges", LKS_LIGHT, 0.25f, 3, 1, 0.05f, 0, "%.2f"},
    [LK_MOON]        = {"moonlight", "Moonlight", "Strength of the moon at night", LKS_LIGHT, 0, 3, 1, 0.05f, 0, "%.2f"},
    [LK_LEAF_SAT]    = {"leaf_saturation", "Leaf saturation", "Colour of leaves and fronds", LKS_FOLIAGE, 0, 2, 1, 0.01f, 0, "%.2f"},
    [LK_LEAF_BRIGHT] = {"leaf_brightness", "Leaf brightness", "Lighter or darker leaves", LKS_FOLIAGE, 0.5f, 1.5f, 1, 0.01f, 0, "%.2f"},
    [LK_LEAF_HUE]    = {"leaf_hue", "Leaf hue", "Left: yellower leaves; right: bluer leaves", LKS_FOLIAGE, -30, 30, 0, 0.5f, 0, "%+.1f"},
    [LK_LEAF_BACK]   = {"leaf_backlight", "Leaf backlight", "Glow of sun shining through leaves", LKS_FOLIAGE, 0, 3, 1, 0.05f, 0, "%.2f"},
    [LK_SWARD_SAT]   = {"ground_cover_saturation", "Ground cover saturation", "Colour of the green ground cover drawn on the terrain", LKS_FOLIAGE, 0, 2, 1, 0.01f, 0, "%.2f"},
};

void cret_look_default(CretLook* l) {
    memset(l, 0, sizeof *l);
    for (int k = 0; k < LK_COUNT; k++) l->v[k] = CRET_LOOK_KNOBS[k].def;
    snprintf(l->name, sizeof l->name, "Default");
}
bool cret_look_equal(const CretLook* a, const CretLook* b) {
    for (int k = 0; k < LK_COUNT; k++) if (fabsf(a->v[k] - b->v[k]) > 1e-6f) return false;
    return true;
}
bool cret_look_is_default(const CretLook* l) { CretLook d; cret_look_default(&d); return cret_look_equal(l, &d); }

static float knob_clamp(int k, float x) {
    const CretLookKnob* K = &CRET_LOOK_KNOBS[k];
    if (!(x == x)) return K->def;   // NaN from a hand-edited file
    if (K->flags & LKF_CHOICE) x = roundf(x);
    return x < K->lo ? K->lo : x > K->hi ? K->hi : x;
}

void cret_look_clean_name(char* s) {
    char* o = s;
    for (const char* p = s; *p && o - s < 47; p++) if (isalnum((unsigned char)*p) || *p == ' ' || *p == '-' || *p == '_') *o++ = *p;
    *o = 0;
    while (o > s && o[-1] == ' ') *--o = 0;   // no trailing spaces in a file name
    if (!*s) snprintf(s, 48, "Look");
}

void cret_look_parse(CretLook* l, const char* text) {
    cret_look_default(l);
    for (const char* p = text; p && *p;) {
        const char* e = strchr(p, '\n'); size_t n = e ? (size_t)(e - p) : strlen(p);
        char line[160]; if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n); line[n] = 0; p = e ? e + 1 : NULL;
        char key[64], val[64]; float v;
        if (line[0] == '#' || line[0] == ';') continue;
        if (sscanf(line, " name = %47[^\r\n]", l->name) == 1) { cret_look_clean_name(l->name); continue; }
        if (sscanf(line, " %63[a-z_] = %63s", key, val) != 2) continue;
        for (int k = 0; k < LK_COUNT; k++) {
            if (strcmp(key, CRET_LOOK_KNOBS[k].key)) continue;
            if (CRET_LOOK_KNOBS[k].flags & LKF_CHOICE) {   // a curve by name ("agx") or by number
                int c = -1;
                for (int t = 0; t < LK_TM_COUNT; t++) {
                    char nm[32]; int j = 0;
                    for (const char* q = CRET_LOOK_TONEMAPS[t]; *q && *q != ' ' && j < 31; q++) nm[j++] = (char)tolower((unsigned char)*q);
                    nm[j] = 0; if (!strcmp(nm, val)) c = t;
                }
                v = c >= 0 ? (float)c : (float)atof(val);
            } else v = (float)atof(val);
            l->v[k] = knob_clamp(k, v);
        }
    }
}

size_t cret_look_format(const CretLook* l, char* out, size_t n) {
    size_t at = 0;
    #define LK_PUT(...) do { int w = snprintf(out + at, at < n ? n - at : 0, __VA_ARGS__); if (w > 0) at += (size_t)w; } while (0)
    LK_PUT("# Cretaceous look (Settings > Look studio). Keys left out keep their default.\nname = %s\n", l->name);
    for (int s = 0; s < LKS_COUNT; s++) {
        LK_PUT("\n# %s\n", CRET_LOOK_SECTIONS[s]);
        for (int k = 0; k < LK_COUNT; k++) {
            const CretLookKnob* K = &CRET_LOOK_KNOBS[k]; if (K->section != s) continue;
            if (K->flags & LKF_CHOICE) {
                char nm[32]; int j = 0;
                for (const char* q = CRET_LOOK_TONEMAPS[(int)l->v[k]]; *q && *q != ' ' && j < 31; q++) nm[j++] = (char)tolower((unsigned char)*q);
                nm[j] = 0; LK_PUT("%s = %s\n", K->key, nm);
            } else LK_PUT("%s = %g\n", K->key, l->v[k]);
        }
    }
    #undef LK_PUT
    return at < n ? at : n ? n - 1 : 0;
}

bool cret_look_load_file(CretLook* l, const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    char buf[8192]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    cret_look_parse(l, buf);
    return true;
}
bool cret_look_save_file(const CretLook* l, const char* path) {
    char buf[8192]; size_t n = cret_look_format(l, buf, sizeof buf);
    FILE* f = fopen(path, "wb"); if (!f) return false;
    bool ok = fwrite(buf, 1, n, f) == n; ok &= fclose(f) == 0;
    cret_config_saved();
    return ok;
}

// ---------------------------------------------------------------- shipped looks
// Tuned on the contact sheets in docs/look_studio (forest floor, grassland,
// river at golden hour, overcast). Every one keeps the scene lighting as it
// is unless it says otherwise, so a look stays a grade on top of the game.
static const char* const BUILTIN[] = {
    "name = Default\n",
    // The owner's hunch: a little more colour. Vibrance does most of it, so
    // the already strong colours (sunset, blood, the warm sun) stay put.
    "name = Richer\nsaturation = 1.08\nvibrance = 0.22\ngreen_saturation = 1.04\ncontrast = 0.34\n",
    // AgX rolls highlights off with their hue kept but reads grey on its own;
    // more contrast, colour and a deeper toe give it a rich, filmic body.
    "name = Filmic\ntonemapper = agx\ncontrast = 0.6\nsaturation = 1.45\nvibrance = 0.1\ntoe = 0.3\nshoulder = 0.1\n",
    // Grey days read flat: more contrast and colour, a touch warmer, and less rain haze.
    "name = Overcast\nexposure = 0.15\ncontrast = 0.45\nvibrance = 0.35\ntemperature = 0.12\nrain_haze = 0.75\ngain = 0.04\n",
    // Mine: warm late light, a little clearer air, cool shadows against gold highlights.
    "name = Golden\nsun_warmth = 0.4\nhaze = 0.85\nhaze_warmth = 0.1\nvibrance = 0.2\ngreen_saturation = 1.06\ncontrast = 0.36\n"
    "lift_hue = 215\nlift_color = 0.35\ngain = 0.02\ngain_hue = 40\ngain_color = 0.3\n",
    // Mine: clear and crisp, slightly cool shade, deeper greens, a stronger sharpen.
    "name = Crisp\nhaze = 0.75\ncontrast = 0.4\nsaturation = 1.05\nvibrance = 0.15\ngreen_saturation = 1.08\nsharpen = 0.8\nlift_hue = 215\nlift_color = 0.4\n",
};
int cret_look_builtins(void) { return (int)(sizeof BUILTIN / sizeof BUILTIN[0]); }
bool cret_look_builtin(int i, CretLook* l) {
    if (i < 0 || i >= cret_look_builtins()) return false;
    cret_look_parse(l, BUILTIN[i]); return true;
}

// ---------------------------------------------------------------- files next to settings.ini
static void look_path(char* out, size_t n, const char* name, bool make) {
    char dir[600]; cret_settings_dir(dir, sizeof dir, make);
    if (!name) { snprintf(out, n, "%s/look.ini", dir); return; }
    char ld[700]; snprintf(ld, sizeof ld, "%s/looks", dir);
    if (make) MakeDirectory(ld);
    snprintf(out, n, "%s/%s.ini", ld, name);
}
void cret_look_load_current(CretLook* l) {
    char p[800]; look_path(p, sizeof p, NULL, false);
    if (!cret_look_load_file(l, p)) cret_look_default(l);
}
bool cret_look_save_current(const CretLook* l) { char p[800]; look_path(p, sizeof p, NULL, true); return cret_look_save_file(l, p); }
bool cret_look_load_named(const char* name, CretLook* l) {
    char p[800]; look_path(p, sizeof p, name, false);
    if (!cret_look_load_file(l, p)) return false;
    snprintf(l->name, sizeof l->name, "%s", name);   // the file's name wins (it may have been renamed)
    return true;
}
bool cret_look_save_named(const CretLook* l) { char p[800]; look_path(p, sizeof p, l->name, true); return cret_look_save_file(l, p); }
bool cret_look_delete_named(const char* name) {
    char p[800]; look_path(p, sizeof p, name, false);
    bool ok = remove(p) == 0; cret_config_saved(); return ok;
}
static int name_cmp(const void* a, const void* b) { return strcmp((const char*)a, (const char*)b); }
int cret_look_list(char (*names)[48], int max) {
    char dir[600], ld[700]; cret_settings_dir(dir, sizeof dir, false); snprintf(ld, sizeof ld, "%s/looks", dir);
    if (!DirectoryExists(ld)) return 0;
    FilePathList fl = LoadDirectoryFilesEx(ld, ".ini", false);
    int n = 0;
    for (unsigned i = 0; i < fl.count && n < max; i++) {
        const char* b = GetFileNameWithoutExt(fl.paths[i]);
        snprintf(names[n], 48, "%s", b); n++;
    }
    UnloadDirectoryFiles(fl);
    qsort(names, (size_t)n, 48, name_cmp);
    return n;
}

// ---------------------------------------------------------------- derived values
static float luma3(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
// A hue (degrees) as a colour of zero luma: what a colour wheel adds.
static void hue_chroma(float deg, float o[3]) {
    float h = deg / 360.0f;
    for (int i = 0; i < 3; i++) {
        float t = h + (i == 0 ? 0.0f : i == 1 ? 2.0f / 3 : 1.0f / 3); t -= floorf(t);
        float v = fabsf(t * 6 - 3) - 1; o[i] = v < 0 ? 0 : v > 1 ? 1 : v;
    }
    float y = luma3(o); for (int i = 0; i < 3; i++) o[i] -= y;
}
// Gains with a luma of one, so a colour shift does not change brightness.
static void unit_luma(float g[3]) { float y = luma3(g); if (y > 1e-6f) for (int i = 0; i < 3; i++) g[i] /= y; }

void cret_look_derive(const CretLook* l, CretLookDerived* d) {
    const float* v = l->v;
    // White balance: warmer adds red and takes blue; tint trades green for magenta.
    float t = v[LK_TEMPERATURE], k = v[LK_TINT];
    d->wb[0] = (1 + 0.14f * t) * (1 + 0.06f * k); d->wb[1] = 1 - 0.1f * k; d->wb[2] = (1 - 0.22f * t) * (1 + 0.06f * k);
    if (t != 0 || k != 0) unit_luma(d->wb); else d->wb[0] = d->wb[1] = d->wb[2] = 1;
    // Lift, gamma and gain (on the display image): an amount for all three channels plus a colour of zero luma.
    float hl[3], hg[3], hk[3];
    hue_chroma(v[LK_LIFT_HUE], hl); hue_chroma(v[LK_GAMMA_HUE], hg); hue_chroma(v[LK_GAIN_HUE], hk);
    for (int i = 0; i < 3; i++) {
        d->lift[i] = v[LK_LIFT] + 0.08f * v[LK_LIFT_COLOR] * hl[i];
        d->gamma[i] = 1.0f / fmaxf(1 + v[LK_GAMMA] + 0.3f * v[LK_GAMMA_COLOR] * hg[i], 0.2f);
        d->gain[i] = 1 + v[LK_GAIN] + 0.3f * v[LK_GAIN_COLOR] * hk[i];
    }
    d->lgg = v[LK_LIFT] != 0 || v[LK_LIFT_COLOR] != 0 || v[LK_GAMMA] != 0 || v[LK_GAMMA_COLOR] != 0 || v[LK_GAIN] != 0 || v[LK_GAIN_COLOR] != 0;
    // Haze colour: an offset on the sky light the haze carries (0 today).
    float hw = v[LK_HAZE_WARMTH];
    d->haze_tint[0] = 0.14f * hw; d->haze_tint[1] = 0.03f * hw; d->haze_tint[2] = -0.2f * hw;
    // Sun: warmer is redder and less blue, at the same brightness; times the intensity.
    float sw = v[LK_SUN_WARMTH];
    float st[3] = {1 + 0.16f * sw, 1 + 0.02f * sw, 1 - 0.3f * sw};
    if (sw != 0) unit_luma(st); else st[0] = st[1] = st[2] = 1;
    for (int i = 0; i < 3; i++) d->sun_tint[i] = st[i] * v[LK_SUN];
}

// ---------------------------------------------------------------- the scene's uniforms
// Every one is an offset from today (0 when a program never sets it), so a
// program that does not take part draws as before.
static CretLook g_scene; static bool g_scene_set;
void cret_look_set_scene(const CretLook* l) { g_scene = *l; g_scene_set = true; }
void cret_look_scene_uniforms(const GfxProgram* p) {
    if (!g_scene_set) cret_look_default(&g_scene), g_scene_set = true;
    const float* v = g_scene.v; CretLookDerived d; cret_look_derive(&g_scene, &d);
    gfx_set1f(p, "u_lk_haze", v[LK_HAZE] - 1);
    gfx_set1f(p, "u_lk_rain_haze", v[LK_RAIN_HAZE] - 1);
    gfx_set3f(p, "u_lk_haze_tint", d.haze_tint[0], d.haze_tint[1], d.haze_tint[2]);
    gfx_set1f(p, "u_lk_bounce", v[LK_BOUNCE] - 1);
    gfx_set1f(p, "u_lk_shadow_soft", v[LK_SHADOW_SOFT] - 1);
    gfx_set4f(p, "u_lk_leaf", v[LK_LEAF_SAT] - 1, v[LK_LEAF_BRIGHT] - 1, v[LK_LEAF_HUE], v[LK_LEAF_BACK] - 1);
    gfx_set1f(p, "u_lk_sward_sat", v[LK_SWARD_SAT] - 1);
}
