// Player overlay (raylib 2D): survival bars, compass, minimap, group panel,
// nameplates, the death screen, the pause menu (species, respawn) and the
// credits screen. Laid out for 1080 lines and scaled to the window height
// (1.0 at 1080p), times the player's UI scale. Reads the sim, never changes
// it: the caller acts on the menu's result.
#ifndef CRET_HUD_H
#define CRET_HUD_H
#include "raylib.h"
#include "sim.h"
#include "ui_style.h"   // sizes, spacing, colours, timings and icons shared by every screen

#define CRET_HUD_MAX_CREDITS 16

// Combat feedback the HUD draws over the game (render/fx.c fills it).
typedef struct CretHudFeedback {
    float marker_t, marker_k; bool marker_kill;   // s since your last blow landed (the marker shows ~0.3 s), its strength 0..1, it killed
    float whiff_t;                                // s since your last blow missed everything (an open ring shows ~0.35 s)
    float vignette;                               // red pulse 0..1 when you are hit
    struct { float x, z, t, k; } dir[6];          // where recent blows on you came from (world xz), age (s), strength 0..1
    int target; float target_t, target_shown;     // the animal you hit last (-1: none), s left on its bar, the bar's lagging health 0..1
} CretHudFeedback;

// HUD_ABILITY: buy or equip slot-3 choice h->choice (abilities.c).
typedef enum { HUD_NONE, HUD_RESUME, HUD_RESPAWN, HUD_SETTINGS, HUD_QUIT, HUD_ABILITY } CretHudAction;

typedef struct CretHud {
    Font font, font_big; bool font_ok;
    char font_path[256]; float font_k;   // fonts are rasterised at k x their 1080p size (k grows with the scale)
    float ui_scale;                      // the player's multiplier on the resolution scale, 0.5..2 (settings "Interface size"; default 1)
    char notice[96]; double notice_until;   // a short message (for example a refused respawn), until GetTime() passes notice_until
    bool menu, credits;              // pause menu open; credits page open (from the menu)
    int sel, species;                // menu row; species to respawn as
    int choice;                      // slot-3 ability choice shown in the menu (0, 1)
    char note[128]; float note_t;    // a short message (quest done, ability bought), shown note_t more seconds
    char character[24];              // character name (title of the vitals panel)
    bool can_respawn;                // set by the caller
    bool map_big;                    // M: the whole-island map instead of the corner minimap
    bool touch;                      // touch controls are showing (touch.h): the HUD makes room for them
    Texture2D map_tex; bool map_ok;  // built from the world map on first draw
    int ncredits;
    char credit[CRET_HUD_MAX_CREDITS][4][160];   // species, work, author, licence (from assets/dinos/CREDITS.md)
    const CretHudFeedback* fb;       // combat feedback to draw (NULL: none; set by the caller)
    double refused_t;                // GetTime() of the last attack click the game threw away (too early in the cooldown)
    bool sniffing;                   // the player holds the scent key (set by the caller; hunt_hud.c)
    int prey, prey_how;              // small prey the HUD leads to (critter slot, -1 none) and how it was noticed (CRET_SENSE_*)
    float prey_d, prey_seen_t;       // its distance, when it was last noticed (GetTime)
    double play_t0;                  // GetTime() of the first HUD frame (the "F1 help" hint shows for a minute)
    char hint[128]; double hint_t0;  // the long hint under the prompt (how to stalk) and when it first showed; it fades after UI_HINT_HOLD
    double menu_t0, menu_seen;       // the pause menu opened at, last drawn at (its fade-in)
} CretHud;

void cret_hud_init(CretHud* h);
// Pixels per layout unit this frame: window height / 1080 (at least 0.75)
// times ui_scale, which is capped so the layout keeps 600 lines.
float cret_hud_scale(const CretHud* h);
// Shows a message over the HUD for `seconds`.
void cret_hud_notice(CretHud* h, const char* msg, float seconds);
// In-game overlay. viewproj: the frame's view-projection (column-major), for
// nameplates; cam_yaw: camera heading for the compass.
void cret_hud_draw(CretHud* h, const CretSim* sim, int me, const float* viewproj, float cam_yaw);
// A message over the ability bar for a few seconds (quest complete, ability unlocked).
void cret_hud_note(CretHud* h, const char* msg);
// Pause menu and credits: handles input and draws. Call every frame while
// h->menu is set (Esc toggles it in the caller).
CretHudAction cret_hud_menu(CretHud* h, const CretSim* sim, int me);
// The credits page alone (also used by capture).
void cret_hud_draw_credits(CretHud* h);
// Key help (F1 in play): open draws the keys over the game; closed, a small
// "F1 help" hint shows for the first minute of play. drag: the mouse drag
// scheme (WSL); dev_keys: the developer hotkeys line (or NULL). Nothing on
// touch, where the buttons say what they do.
void cret_hud_help(CretHud* h, bool open, bool drag, const char* dev_keys);
void cret_hud_close(CretHud* h);

// Shared UI pieces for the other screens (menu.c). Layout is in 1080-line
// units: draw between cret_ui_begin and cret_ui_end; cret_ui_w/h give the
// window size in those units. The scale is h's (ui_scale capped at 880 lines).
// Colours, sizes and icons are ui_style.h's.
void cret_ui_begin(CretHud* h);
void cret_ui_end(void);
float cret_ui_w(void);
float cret_ui_h(void);
Vector2 cret_ui_mouse(void);
// A tap this frame (touch.h), and where in layout units: menus take it as a click.
bool cret_ui_tap(Vector2* at);
// The page's fade, 0..1 (a page fades in over UI_FADE): text and widgets
// take it; cret_ui_fa applies it to a colour for direct raylib drawing.
void cret_ui_fade(float a);
Color cret_ui_fa(Color c);
// How the player is working the menus: the last of keys, mouse or touch.
enum { CRET_UI_KEYS, CRET_UI_MOUSE, CRET_UI_TOUCH };
int cret_ui_input(void);
void cret_ui_text(const CretHud* h, bool big, const char* s, float x, float y, float size, Color c);
float cret_ui_text_w(const CretHud* h, bool big, const char* s, float size);
// Spaced capitals (section labels).
void cret_ui_label(const CretHud* h, const char* s, float x, float y, float size, Color c);
float cret_ui_label_w(const CretHud* h, const char* s, float size);
// A section heading: spaced capitals over a hairline w wide.
void cret_ui_heading(const CretHud* h, const char* s, float x, float y, float w);
// A panel: the soft dark backing with a faint edge every screen uses.
void cret_ui_panel(Rectangle r);
// An icon (UI_I_*) of size s centred at c.
void cret_ui_icon(int icon, Vector2 c, float s, Color col);
// A key cap with its key ("E", "Esc"; "Up", "Down", "Left", "Right" draw
// arrows, "LMB" and "RMB" a mouse) at (x, y), UI_T_SMALL text in a box about
// 22 units tall; returns its width.
float cret_ui_keycap(const CretHud* h, const char* key, float x, float y);
// A row of key caps and what they do: keys[i] (several keys separated by
// spaces share one verb; "" none) and verbs[i], centred on x or from x.
void cret_ui_footer(const CretHud* h, const char* const* keys, const char* const* verbs, int n, float x, float y, bool centre);
// A name set in spaced capitals in the large face (the game's name).
void cret_ui_title(const CretHud* h, const char* s, float x, float y, float size, Color c);
// The selected row's mark: a wash and an amber bar at its left, sliding to a
// newly selected row over UI_FADE.
void cret_ui_select(Rectangle box);
// A 0..1 slider (track, fill, knob) w wide at (x, y) for a row of text
// height `size`; lit when its row is selected.
void cret_ui_slider(float x, float y, float w, float size, float v, bool lit);
// An on/off switch at (x, y) for a row of text height `size`.
void cret_ui_toggle(float x, float y, float size, bool on, bool lit);
void cret_ui_credits(CretHud* h);   // the credits page body (no begin/end)
// The key list (key bindings page, F1 help): n rows of key and action.
int cret_ui_keys(const char* const** keys, const char* const** what);
// A value that steps, "<  value  >" in a box w wide at (x, y): the arrows
// always show and light up under the pointer. cret_ui_step_hit is -1 or +1
// when a click or tap at m lands on the left or right half of the box (only
// on the arrows with ends, when a bar between them takes clicks), else 0.
// cret_ui_stepper draws it (value NULL: the arrows alone).
int cret_ui_step_hit(Vector2 m, bool click, float x, float y, float w, float size, bool ends);
void cret_ui_stepper(const CretHud* h, const char* value, float x, float y, float w, float size, Color c, Vector2 m);
// The same with only the arrows stepping and lighting up (the value between them is its own button: the title's Play row).
void cret_ui_stepper_ends(const CretHud* h, const char* value, float x, float y, float w, float size, Color c, Vector2 m);
// The walk key. Natively Left Ctrl. In a browser Ctrl+W (walk forward) closes
// the tab before the page sees it, and Ctrl+S, D, R, T and N are browser
// shortcuts too, so the web build walks with Left Alt (web/loader.js keeps
// Alt from opening the browser's menu); Ctrl still walks there as well.
#ifdef __EMSCRIPTEN__
#define CRET_WALK_KEY_NAME "Alt"
#define CRET_WALK_DOWN() (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_LEFT_CONTROL))
#else
#define CRET_WALK_KEY_NAME "Ctrl"
#define CRET_WALK_DOWN() IsKeyDown(KEY_LEFT_CONTROL)
#endif
#endif
