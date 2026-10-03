#ifndef SWAT_RENDER_H
#define SWAT_RENDER_H
#include "sim.h"
#include "raylib.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SwatView {
    bool initialized;
    int width,height,actor;
    float yaw_offset,pitch_offset;
    bool planning;
    int plan_preview;
    float plan_yaw;
    char session_status[128];
} SwatView;
void swat_view_init(SwatView* view, bool hidden);
// Draws inside the caller's BeginDrawing/EndDrawing pair.
void swat_view_draw(SwatView* view, const SwatSim* sim, bool policy, float vertical_fov);
void swat_view_close(SwatView* view);
#ifdef __cplusplus
}
#endif
#endif
