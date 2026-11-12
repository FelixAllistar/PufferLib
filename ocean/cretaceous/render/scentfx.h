// The scent view (render/scentfx.c, included by render/view.c): what a
// sniffing carnivore smells, drawn over the finished frame.
//   - faint wisps along the trails of critters and other animals that pass
//     the nose (scent.h cret_scent_strength), each where its smell has
//     drifted with the wind, fading with age and range: warm for prey,
//     grey-green for other dinosaurs (their trail only, never the animal);
//   - a slow plume rising from a carcass downwind;
//   - a soft pulse, an expanding ring, on the nearest prey and carcass the
//     nose finds, seen through plants (dimmer when something is in front);
//   - a light darkening of the frame's edges while the sense is held.
// It is drawn after post-processing, in display colours, so it reads the
// same at noon and at night, and tests the scene depth itself: a wisp behind
// a plant shows at a third of its strength. Presentation only; the sim is
// not touched.
#ifndef CRET_RENDER_SCENTFX_H
#define CRET_RENDER_SCENTFX_H
#include "view.h"

// amount: 0..1, the caller's eased sniff (0 draws nothing). The window's
// framebuffer must be bound, with the frame cret_view_render just drew.
void cret_scent_draw(CretView* v, const CretSim* sim, int me, float amount, const CretCamera* cam);
void cret_scent_fx_close(void);
#endif
