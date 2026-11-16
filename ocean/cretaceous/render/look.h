// The look: every knob of the picture that the Look studio (studio.c) shows,
// as one table, so the panel, the look files and the shaders agree on names,
// ranges and defaults. At its default every knob leaves today's image as it
// was (docs/look_studio).
//
// A look is a set of knob values with a name. Looks are small ini files
// ("key = value" lines, unknown keys ignored, missing keys at their default):
// the one in play is look.ini next to settings.ini, the saved ones are in
// looks/NAME.ini beside it, and the shipped ones are built in (cret_look_builtin).
//
// Where a knob acts:
//   post   in the composite pass (post.fs): the split compare shows it.
//   scene  in the lighting of the 3D scene (common.glsl, veg_light.glsl,
//          terrain.fs, csm.glsl, the atmosphere LUTs) or a pass before post
//          (bloom, eye adaptation, ambient occlusion): the whole frame has
//          one value, so the split cannot show it.
#ifndef CRET_LOOK_H
#define CRET_LOOK_H
#include <stdbool.h>
#include <stddef.h>

enum {
    // Exposure and tone
    LK_EXPOSURE, LK_ADAPT, LK_ADAPT_SPEED, LK_NIGHT_DROP, LK_TONEMAP, LK_WHITE, LK_CONTRAST, LK_TOE, LK_SHOULDER,
    // Colour
    LK_SATURATION, LK_VIBRANCE, LK_TEMPERATURE, LK_TINT,
    LK_LIFT, LK_LIFT_HUE, LK_LIFT_COLOR, LK_GAMMA, LK_GAMMA_HUE, LK_GAMMA_COLOR, LK_GAIN, LK_GAIN_HUE, LK_GAIN_COLOR,
    LK_GRADE, LK_GREEN_HUE, LK_GREEN_SAT,
    // Effects
    LK_BLOOM, LK_BLOOM_THRESHOLD, LK_BLOOM_RADIUS, LK_SHARPEN, LK_VIGNETTE, LK_AO, LK_AO_RADIUS, LK_GRAIN,
    // Atmosphere
    LK_HAZE, LK_HAZE_WARMTH, LK_MIST, LK_SKY, LK_CLOUDS, LK_RAIN_HAZE,
    // Light
    LK_SUN, LK_SUN_WARMTH, LK_AMBIENT, LK_BOUNCE, LK_SHADOW_SOFT, LK_MOON,
    // Foliage
    LK_LEAF_SAT, LK_LEAF_BRIGHT, LK_LEAF_HUE, LK_LEAF_BACK, LK_SWARD_SAT,
    LK_COUNT
};
enum { LKS_TONE, LKS_COLOUR, LKS_EFFECTS, LKS_ATMOS, LKS_LIGHT, LKS_FOLIAGE, LKS_COUNT };
enum { LKF_POST = 1, LKF_CHOICE = 2 };
// The tone curves (LK_TONEMAP).
enum { LK_TM_TODAY, LK_TM_AGX, LK_TM_NEUTRAL, LK_TM_HABLE, LK_TM_REINHARD, LK_TM_COUNT };

typedef struct {
    const char* key;      // in look files
    const char* name;     // on the panel
    const char* help;     // one plain line on the panel
    int section;          // LKS_*
    float lo, hi, def, step;
    unsigned flags;       // LKF_*
    const char* fmt;      // printf format of the value ("%.2f"); choices show their names
} CretLookKnob;
extern const CretLookKnob CRET_LOOK_KNOBS[LK_COUNT];
extern const char* const CRET_LOOK_SECTIONS[LKS_COUNT];
extern const char* const CRET_LOOK_TONEMAPS[LK_TM_COUNT];

typedef struct CretLook { float v[LK_COUNT]; char name[48]; } CretLook;

void cret_look_default(CretLook* l);
bool cret_look_is_default(const CretLook* l);
bool cret_look_equal(const CretLook* a, const CretLook* b);
// Reads "key = value" lines over the defaults (and "name = ..."); values are clamped to the knob's range.
void cret_look_parse(CretLook* l, const char* text);
// Writes the look as ini text into out (n bytes); returns the length.
size_t cret_look_format(const CretLook* l, char* out, size_t n);
bool cret_look_load_file(CretLook* l, const char* path);
bool cret_look_save_file(const CretLook* l, const char* path);

// Shipped looks: 0 is Default (today's image); returns false past the last.
int cret_look_builtins(void);
bool cret_look_builtin(int i, CretLook* l);

// The look in play (look.ini) and the saved looks (looks/NAME.ini), next to settings.ini.
void cret_look_load_current(CretLook* l);
bool cret_look_save_current(const CretLook* l);
// The saved looks' names (file names without .ini), sorted; returns how many (at most max).
int cret_look_list(char (*names)[48], int max);
bool cret_look_load_named(const char* name, CretLook* l);
bool cret_look_save_named(const CretLook* l);
bool cret_look_delete_named(const char* name);
// Keeps letters, digits, spaces, '-' and '_' (a file name on every system).
void cret_look_clean_name(char* name);

// Derived values the shaders take, so the panel's plain knobs (a hue and a
// strength, a warmth) become colours once on the CPU.
typedef struct {
    float wb[3];                         // white balance gains (temperature, tint)
    float lift[3], gamma[3], gain[3];    // lift offset, gamma exponent, gain multiplier per channel
    bool lgg;                            // lift/gamma/gain away from identity
    float haze_tint[3];                  // in-scatter colour offset (0 = as today)
    float sun_tint[3];                   // sun colour times intensity
} CretLookDerived;
void cret_look_derive(const CretLook* l, CretLookDerived* d);

// The look the 3D scene is lit with this frame (render/view.c sets it), and
// the uniforms that carry it into every lit program; cret_csm_bind calls
// this, since every lit program binds the shadow maps.
struct GfxProgram;
void cret_look_set_scene(const CretLook* l);
void cret_look_scene_uniforms(const struct GfxProgram* p);
#endif
