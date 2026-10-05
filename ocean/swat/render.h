#ifndef SWAT_RENDER_H
#define SWAT_RENDER_H
#include "sim.h"
#include "raylib.h"
#include "environment_art.h"
#include "lighting.h"
#include "weapon_art.h"
#include "character_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SwatView {
    bool initialized;
    int width,height,actor;
    float yaw_offset,pitch_offset;
    float weapon_size,weapon_horizontal,weapon_vertical;
    bool planning,debug;
    int plan_preview;
    float plan_yaw;
    bool scope;
    bool sniper_camera;
    float camera_expansion;
    RenderTexture2D camera_target;
    Font hud_font;
    SwatEnvironmentArt environment;
    SwatLighting lighting;
    SwatWeaponArt weapons;
    SwatCharacterRuntime* characters;
    int sniper_unit;
    char session_status[128];
} SwatView;
typedef struct SwatCameraLayout {
    Rectangle panel,feed,unit[SWAT_SNIPERS],previous,next,takeover,close;
} SwatCameraLayout;
SwatCameraLayout swat_camera_layout(int width,int height,float expansion,bool has_feed);
int swat_hud_measure(const SwatView* view,const char* text,int size);
void swat_hud_text(const SwatView* view,const char* text,int x,int y,int size,Color color);
void swat_view_init(SwatView* view, bool hidden);
// Draws inside the caller's BeginDrawing/EndDrawing pair.
void swat_view_draw(SwatView* view, const SwatSim* sim, bool policy, float vertical_fov);
void swat_view_close(SwatView* view);
#ifdef __cplusplus
}
#endif
#endif
