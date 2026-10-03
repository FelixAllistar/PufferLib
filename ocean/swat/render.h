#ifndef SWAT_RENDER_H
#define SWAT_RENDER_H
#include "sim.h"
#include "raylib.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SwatView { bool initialized, captured; int width, height; } SwatView;
void swat_view_init(SwatView* view, bool hidden);
void swat_view_draw(SwatView* view, const SwatSim* sim, bool policy);
void swat_view_close(SwatView* view);
SwatInput swat_view_input(void);
#ifdef __cplusplus
}
#endif
#endif
