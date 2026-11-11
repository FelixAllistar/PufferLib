// Player overlay (raylib 2D): survival bars, compass, minimap, group panel,
// nameplates, the death screen, the pause menu (species, respawn) and the
// credits screen. Laid out for 1080 lines and scaled to the window height
// (1.0 at 1080p), times the player's UI scale. Reads the sim, never changes
// it: the caller acts on the menu's result.
#ifndef CRET_HUD_H
#define CRET_HUD_H
#include "raylib.h"
#include "sim.h"

#define CRET_HUD_MAX_CREDITS 16

// Combat feedback the HUD draws over the game (render/fx.c fills it).
typedef struct CretHudFeedback {
    float marker_t, marker_k; bool marker_kill;   // s since your last blow landed (the marker shows ~0.3 s), its strength 0..1, it killed
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
    Texture2D map_tex; bool map_ok;  // built from the world map on first draw
    int ncredits;
    char credit[CRET_HUD_MAX_CREDITS][4][160];   // species, work, author, licence (from assets/dinos/CREDITS.md)
    const CretHudFeedback* fb;       // combat feedback to draw (NULL: none; set by the caller)
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
void cret_hud_close(CretHud* h);

// Shared UI pieces for the other screens (menu.c). Layout is in 1080-line
// units: draw between cret_ui_begin and cret_ui_end; cret_ui_w/h give the
// window size in those units. The scale is h's (ui_scale capped at 880 lines).
extern const Color CRET_UI_INK, CRET_UI_DIM, CRET_UI_PANEL, CRET_UI_ACCENT;
void cret_ui_begin(CretHud* h);
void cret_ui_end(void);
float cret_ui_w(void);
float cret_ui_h(void);
Vector2 cret_ui_mouse(void);
void cret_ui_text(const CretHud* h, bool big, const char* s, float x, float y, float size, Color c);
float cret_ui_text_w(const CretHud* h, bool big, const char* s, float size);
void cret_ui_label(const CretHud* h, const char* s, float x, float y, float size, Color c);
void cret_ui_credits(CretHud* h);   // the credits page body (no begin/end)
#endif
