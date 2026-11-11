// On-screen touch controls for phones and tablets (raylib 2D, drawn over the
// HUD at its scale): a floating stick on the left for movement (pushed to its
// rim it sprints), a drag anywhere else to turn the camera, round buttons on
// the right for attacks, jump and eating, a row along the bottom for call and
// abilities 1-3 (greyed while cooling down, as the ability bar is), and a
// menu button by the minimap. Menus are worked by tapping (cret_ui_tap).
//
// The controls show once the screen is touched (or at once on a touch-first
// browser page) and hide as soon as a key is pressed or the mouse clicks,
// so a laptop with a touch screen switches back and forth. CRET_TOUCH=1 shows
// them always (screenshots on a desktop), CRET_TOUCH=0 never.
//
// Compiled by including touch.c from hud.c (it shares the HUD's scale and
// text), so the build scripts outside ocean/cretaceous need no new source.
// Presentation and input only: the sim sees ordinary CretInput values.
#ifndef CRET_TOUCH_H
#define CRET_TOUCH_H
#include <stdbool.h>
#include "raylib.h"
#include "sim.h"

struct CretHud;

// What the controls asked for this frame (cret_touch_play).
typedef struct CretTouchPlay {
    float move_x, move_y;     // stick: right and forward, -1..1 each (0, 0 when not held)
    bool sprint;              // the stick is pushed to its rim
    Vector2 look;             // camera turn this frame in mouse-look pixels (CAM_LOOK radians each)
    bool looking;             // a finger is turning the camera now
    bool eat;                 // held: eat, or drink when facing water
    bool attack, attack2, jump, call, menu, map, respawn;   // pressed this frame
    int ability;              // 1-3 pressed this frame, 0 none
    bool nest;                // held: build a nest (N), shown to a fed adult with none
    bool carry;               // pressed this frame: pick up or put down food (carnivores, cret_carry_state)
} CretTouchPlay;

// Once per frame, before any input is read: follows the fingers and notices
// keyboard and mouse use.
void cret_touch_frame(void);
// The controls are showing (touch play).
bool cret_touch_active(void);
// A finger came down this frame; *at is where, in screen pixels. Menus treat
// it as a click there.
bool cret_touch_tap(Vector2* at);
// Play: assigns new fingers to the stick, the camera or a button and returns
// this frame's requests. Nothing while the controls are hidden.
CretTouchPlay cret_touch_play(struct CretHud* h, const CretSim* sim, int me);
// Draws the controls for the player's animal (call after cret_hud_draw).
void cret_touch_draw(struct CretHud* h, const CretSim* sim, int me);
#endif
