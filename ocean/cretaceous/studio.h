// The Look studio: a panel down one side of the live game with every knob of
// the picture (render/look.h), the looks to load and save, a split compare
// and a screenshot button. Opened from Settings or with CRET_STUDIO_KEY in
// play; the mouse is free while it is open and the right button held over
// the scene turns the camera. Desktop only (the web build has no panel; the
// chosen look still applies there).
#ifndef CRET_STUDIO_H
#define CRET_STUDIO_H
#include "hud.h"
#include "render/view.h"


typedef struct CretStudio {
    bool open;
    CretLook before; bool have_before;   // the look in play when the studio first opened this session
    bool split, left_default;            // split compare on; its left side is Default instead of before
    float split_x;                       // the divider, 0..1 of the window width
    bool drag_split, hold_before;        // dragging the divider; the "hold for before" button is held
    float scroll, content_h;             // panel scroll (layout units) and the height of what scrolls
    bool collapsed[LKS_COUNT + 2];       // the knob sections, then Looks and Scene
    int active;                          // the slider being dragged (a knob, or 100 + a scene slider), -1 none
    int last_click; double last_click_t; // double clicks reset a slider
    char names[64][48]; int nnames;      // saved looks (looks/*.ini)
    CretLook ref;                        // the look of that name as loaded or saved: the sliders differ from it when edited
    int naming;                          // typing a name: 1 save as, 2 rename
    char name_buf[48];
    char note[200]; double note_t;       // a line under the header (saved, loaded, a screenshot's path)
    bool shot;                           // a screenshot is due on the next frame (the caller takes it)
    bool dirty; double dirty_t;          // look.ini is out of date
    bool freeze; float freeze_tod;       // the clock is held at freeze_tod (play, solo or host)
    bool over_panel;                     // the pointer is over the panel (the wheel scrolls it, no orbit)
    bool orbit_ok;                       // the right button went down over the scene: it turns the camera
    int hover;                           // the row under the pointer (its help shows in the footer), -1 none
    bool fresh;                          // opened this frame: the key that opened it does not close it
} CretStudio;

// The world the scene controls act on: the sim's time of day and cloud cover
// (NULL, or can_set false for a client, which follows the server).
typedef struct { CretSim* sim; bool can_set; } CretStudioWorld;

void cret_studio_init(CretStudio* s);
void cret_studio_open(CretStudio* s, CretView* v);
void cret_studio_close(CretStudio* s, CretView* v);
// Every frame while open, after the HUD: input and drawing. Returns true when it closed itself.
bool cret_studio_frame(CretStudio* s, CretHud* h, CretView* v, CretStudioWorld w, float dt);
// After the sim's ticks: holds the clock while frozen.
void cret_studio_after_tick(const CretStudio* s, CretSim* sim);
// The camera orbit while open: this frame's right-button drag over the scene, in pixels.
Vector2 cret_studio_orbit(CretStudio* s);
// Call right after cret_view_render (before any HUD) on the frame s->shot is set: writes
// screenshots/look_DATE.png at the window's full resolution with the look's ini beside it.
void cret_studio_take_shot(CretStudio* s, const CretView* v);
#endif
