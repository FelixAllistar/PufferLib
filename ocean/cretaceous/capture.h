// Camera-path captures: renders a scripted camera move as frames and encodes
// them to mp4 with ffmpeg (silent, windowless). Used to review motion:
// temporal clouds, shadow cascades, LOD transitions, water and rain.
//   ./cretaceous capture DIR --path NAME[,NAME...] [--frames N] [--fps F] [--size W H]
// Paths: flyover, forest_walk, tp_run, dusk_night, rain_in, turn, grass_meadow,
// grass_river, grass_push, grass_trail (or 'all').
// Writes DIR/NAME.mp4 and every CRET_PATH_STILLS-th frame (default 30) as
// DIR/NAME/fNNNN.png. CRET_GPU_PROFILE=1 prints per-pass timings per path.
#ifndef CRET_CAPTURE_H
#define CRET_CAPTURE_H
#include "sim.h"

// frames <= 0: each path's default length. Returns 0 on success.
int cret_capture_paths(CretSim* sim, const char* dir, const char* names, int W, int H, int frames, int fps);
#endif
