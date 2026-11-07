// Player overlay (raylib 2D): survival bars, compass, minimap, group panel,
// nameplates, the death screen, the pause menu (species, respawn) and the
// credits screen. Laid out for 1080 lines and scaled to the window height. Reads the
// sim, never changes it: the caller acts on the menu's result.
#ifndef CRET_HUD_H
#define CRET_HUD_H
#include "raylib.h"
#include "sim.h"

#define CRET_HUD_MAX_CREDITS 16

typedef enum { HUD_NONE, HUD_RESUME, HUD_RESPAWN, HUD_QUIT } CretHudAction;

typedef struct CretHud {
    Font font, font_big; bool font_ok;
    bool menu, credits;              // pause menu open; credits page open (from the menu)
    int sel, species;                // menu row; species to respawn as
    bool can_respawn;                // set by the caller (clients cannot)
    bool map_big;                    // M: the whole-island map instead of the corner minimap
    Texture2D map_tex; bool map_ok;  // built from the world map on first draw
    int ncredits;
    char credit[CRET_HUD_MAX_CREDITS][4][160];   // species, work, author, licence (from assets/dinos/CREDITS.md)
} CretHud;

void cret_hud_init(CretHud* h);
// In-game overlay. viewproj: the frame's view-projection (column-major), for
// nameplates; cam_yaw: camera heading for the compass.
void cret_hud_draw(CretHud* h, const CretSim* sim, int me, const float* viewproj, float cam_yaw);
// Pause menu and credits: handles input and draws. Call every frame while
// h->menu is set (Esc toggles it in the caller).
CretHudAction cret_hud_menu(CretHud* h, const CretSim* sim, int me);
// The credits page alone (also used by capture).
void cret_hud_draw_credits(CretHud* h);
void cret_hud_close(CretHud* h);
#endif
