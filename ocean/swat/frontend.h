#ifndef SWAT_FRONTEND_H
#define SWAT_FRONTEND_H
#include "render.h"
#include "settings.h"
#include "feedback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SwatScreen { SWAT_SCREEN_GAME, SWAT_SCREEN_MAIN,
                         SWAT_SCREEN_PAUSE, SWAT_SCREEN_SETTINGS, SWAT_SCREEN_CONNECT,
                         SWAT_SCREEN_PLAN, SWAT_SCREEN_SCOPE } SwatScreen;
typedef struct SwatFrontend {
    SwatSettings settings, saved_settings;
    SwatScreen screen, settings_back;
    bool captured, quit, restart_requested, reset_input, wait_for_release;
    bool host_requested,join_requested,disconnect_requested,networked,leader;
    bool hosting,connect_pending,address_edit,port_edit;
    int actor;
    int selected_kit,plan_preview,last_episode;
    float plan_yaw;
    bool loadout_pending;
    int plan_tab,selected_sniper,sniper_post[SWAT_SNIPERS],sniper_rifle[SWAT_SNIPERS];
    bool sniper_pending[SWAT_SNIPERS],sniper_enabled[SWAT_SNIPERS];
    bool camera_open,camera_pointer;
    float camera_expansion;
    bool scenario_requested,seed_edit;
    SwatConfig scenario;
    char layout_seed[16];
    int layout_difficulty,layout_generator;
    SwatFeedback feedback;
    int discard_mouse_frames;
    char address[128],port[8];
    char settings_path[SWAT_SETTINGS_PATH_SIZE];
    char notice[192];
} SwatFrontend;

void swat_frontend_init(SwatFrontend* app, const char* settings_path);
void swat_frontend_update(SwatFrontend* app, const SwatSim* sim, bool policy);
void swat_frontend_set_screen(SwatFrontend* app, SwatScreen screen);
bool swat_frontend_playing(const SwatFrontend* app);
SwatInput swat_frontend_input(SwatFrontend* app, const SwatSim* sim);
// Draw inside the caller's BeginDrawing/EndDrawing pair, after the game view.
void swat_frontend_draw(SwatFrontend* app, const SwatSim* sim, bool policy);
void swat_frontend_close(SwatFrontend* app);

#ifdef __cplusplus
}
#endif
#endif
