#pragma once
#include "fpg_task.h"

// Local evaluation artifact, generated from a user-supplied ROM. No ROM bytes
// or test trajectories are built into the training bank or executable.
#define FPG_PARITY_MAGIC 0x46505032u
typedef struct {
    uint32_t magic, version, case_size, frame_size, cases;
    FpgConfig config;
} FpgParityHeader;
typedef struct { FpgState initial; int frames, source_frame, kind; } FpgParityCase;
typedef struct {
    int action, status;
    FpgBody body;
    FpgCamera camera;
    FpgActors actors;
    float observations[FPG_OBS];
} FpgParityFrame;
