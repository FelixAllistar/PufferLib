#pragma once
// Build-time choice: no runtime resizing mode or branching in the hot path.
#ifndef RETRO_OBS_SCALE
#define RETRO_OBS_SCALE 4
#endif
#if RETRO_OBS_SCALE == 4
#define RETRO_OBSERVATION_CONTRACT "fullscreen64x60_luma_v1"
#elif RETRO_OBS_SCALE == 2
#define RETRO_OBSERVATION_CONTRACT "fullscreen128x120_luma_v1"
#else
#error "RETRO_OBS_SCALE must be 2 or 4"
#endif
#define RETRO_WINDOW_W (256 / RETRO_OBS_SCALE)
#define RETRO_WINDOW_H (240 / RETRO_OBS_SCALE)
