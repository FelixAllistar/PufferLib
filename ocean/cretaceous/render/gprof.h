// Per-pass GPU and CPU timing (CRET_GPU_PROFILE=1). Passes are marked in
// order with gprof_mark(name); the interval up to the next mark is charged to
// that name. GL timestamps are read back a few frames late, so profiling does
// not stall the pipeline. gprof_report() prints the averages and resets them.
#ifndef CRET_GPROF_H
#define CRET_GPROF_H
#include <stdbool.h>
bool gprof_enabled(void);
void gprof_frame_begin(void);
void gprof_mark(const char* name);
void gprof_frame_end(void);
void gprof_report(const char* label);
void gprof_reset(void);
// Turns profiling on or off at run time (the debug console's `profile`); it
// takes effect at the next gprof_frame_begin.
void gprof_set_enabled(bool on);
// Per-pass averages (ms) since the last snapshot or report, then resets them.
// Returns the pass count (at most max); frame totals in *frame_gpu, *frame_cpu.
int gprof_snapshot(const char** names, float* gpu, float* cpu, int max, float* frame_gpu, float* frame_cpu);
#endif
