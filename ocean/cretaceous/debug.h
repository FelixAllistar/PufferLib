// Developer console and debug overlays (raylib side; the commands and the
// sim cheats live in cheats.c). Backtick or F12 opens the console: history
// (Up/Down), Tab completion, PageUp/PageDown scroll, Esc closes. While it is
// open the game sees no keys (cretaceous.c gates raylib's key and mouse
// queries through cret_debug_pass).
// Hotkeys (console closed): F6 spawn a dummy of a random species 8 m ahead,
// F7 a passive one, F8 labels, F9 colliders.
// Overlays: colliders (Box3D shapes near the camera as wireframe), labels
// (species, AI state, target, health), navdebug (bot goals, feelers, stuck
// escapes, unwalkable ground), fps (frame time graph), profile (GPU/CPU ms
// per pass), near (nearest animals, distance and bearing).
// Clients (join) may use the graphics and fps/profile/colliders switches only:
// cheats and the overlays that reveal other animals answer "not allowed".
#ifndef CRET_DEBUG_H
#define CRET_DEBUG_H
#include "raylib.h"
#include "sim.h"
#include "cheats.h"
#include "render/view.h"

#define CRET_CON_LINES 64
#define CRET_CON_HIST 32
#define CRET_CON_LEN 160
#define CRET_FPS_HIST 240

typedef struct CretDebug {
    bool open; int opened_frame;            // console shown; the frame it opened (its toggle key is not typed)
    bool swallow;                           // this frame's keys belong to the console (also the frame it closes)
    char line[CRET_CON_LEN]; int cursor;
    char hist[CRET_CON_HIST][CRET_CON_LEN]; int nhist, hist_at;
    char log[CRET_CON_LINES][CRET_CON_LEN]; Color log_col[CRET_CON_LINES]; int nlog, scroll;
    char complete[200];                     // Tab's candidates when several match
    float toast_t;                          // the last reply shows this long with the console closed
    bool ran_env;                           // CRET_CONSOLE's commands ran
    bool colliders, labels, navdebug, fps, profile, nearest, fog_off;
    float frame_ms[CRET_FPS_HIST]; int frame_i;
    // Profiler snapshot (refreshed twice a second).
    int prof_n; const char* prof_name[48]; float prof_gpu[48], prof_cpu[48], prof_tot_gpu, prof_tot_cpu; double prof_t;
    CretCheats cheats;
    bool authority;                         // solo or host
    int target; bool has_aim; float aim[3]; // under the crosshair (refreshed each frame)
    bool cut;                               // the player teleported (the caller cuts the camera)
    int frame;
} CretDebug;

void cret_debug_init(CretDebug* d, CretPop* pop, uint32_t seed, bool authority);
// Per frame, before the game reads keys: the toggle, typing, hotkeys and
// commands. cam: last frame's camera (crosshair). Returns true while the console is open.
bool cret_debug_frame(CretDebug* d, CretSim* sim, CretView* view, const CretCamera* cam, int me);
// False while the console owns the keyboard (the game ignores the key).
bool cret_debug_pass(void);
// Sim time scale (speed command); 1 normally.
float cret_debug_speed(const CretDebug* d);
// Around cret_sim_step (solo and host only).
void cret_debug_inputs(CretDebug* d, CretSim* sim, CretInput* inputs, int me);
void cret_debug_after_step(CretDebug* d, CretSim* sim, int me);
// Overlays and the console (after the HUD). draw: the interpolated copy.
void cret_debug_draw(CretDebug* d, const CretSim* draw, CretSim* sim, const CretView* view, const CretCamera* cam, int me, float ui);
// The F1 help line's debug part.
const char* cret_debug_help(void);
// Prints a line to the console (and stdout).
void cret_debug_print(CretDebug* d, Color c, const char* fmt, ...);
// Graphics switch commands (preset, shadows, clouds, veg, grass, water, fog, wireframe) on a view; false if not one.
bool cret_debug_view_cmd(CretDebug* d, CretView* view, const CretCmd* cmd, char* out, int nout);
#endif
