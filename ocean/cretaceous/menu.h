// Title screen, settings and key-binding pages (raylib 2D, drawn over the
// world flyover or the paused game), and the player's settings file.
// Screens only report choices; the caller (cretaceous.c) acts on them.
#ifndef CRET_MENU_H
#define CRET_MENU_H
#include <stdbool.h>
#include "hud.h"
#include "audio.h"

typedef struct CretSettings {
    int preset;               // CRET_PRESET_* (render/view.h)
    float render_scale;       // 3D resolution, 0.5..1
    float ui_scale;           // HUD and menu size on top of the window-height scale, 0.5..2
    bool fullscreen;
    int res;                  // windowed size: index into cret_settings_resolution
    float mouse_sens;         // multiplier, 0.25..3
    bool invert_y;
    float volume[CRET_AUDIO_BUSES];   // master, ambience, effects, interface
} CretSettings;

void cret_settings_default(CretSettings* s);
// ~/.config/cretaceous/settings.ini ($XDG_CONFIG_HOME first). Missing or
// unreadable keys keep their defaults.
void cret_settings_load(CretSettings* s);
bool cret_settings_save(const CretSettings* s);
int cret_settings_resolutions(void);
void cret_settings_resolution(int i, int* w, int* h);

typedef enum { MENU_NONE, MENU_PLAY, MENU_HOST, MENU_JOIN, MENU_QUIT, MENU_BACK, MENU_APPLY } CretMenuAction;

typedef struct CretMenu {
    int page, sel;            // title page and row
    int species;              // CRET_SP_* for Play and Host
    char address[64]; bool editing;
    int set_sel, set_from;    // settings row; page to return to
    float t;                  // time on the title (fades)
    char note[128];           // a message under the title menu (e.g. a failed join)
} CretMenu;

void cret_menu_init(CretMenu* m, int species);
// Title screen: Play, Host, Join (address field), Settings, Credits, Quit.
// MENU_APPLY: a setting changed (apply it); MENU_PLAY/HOST/JOIN/QUIT: go.
CretMenuAction cret_menu_title(CretMenu* m, CretHud* h, CretAudio* au, CretSettings* s, float dt);
// Settings page alone (from the pause menu). MENU_APPLY on a change,
// MENU_BACK when closed (settings are saved then).
CretMenuAction cret_menu_settings(CretMenu* m, CretHud* h, CretAudio* au, CretSettings* s);
#endif
