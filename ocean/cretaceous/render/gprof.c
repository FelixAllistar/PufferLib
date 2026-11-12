#include "gprof.h"
#include "gfx.h"
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RING 4
#define MAXM 64
#define MAXN 48
typedef struct { GLuint q[MAXM]; const char* name[MAXM]; double cpu[MAXM]; int n; bool pending; } Frame;
static int g_on = -1, g_cur; static bool g_open;   // g_open: "present" query of the last frame still running
static Frame g_fr[RING];
static const char* g_names[MAXN]; static double g_gpu[MAXN], g_cpu[MAXN]; static int g_nn, g_frames;
static double g_frame_gpu, g_frame_cpu;
static int g_req = -1; static bool g_made;   // run-time switch (gprof_set_enabled); queries created
static int g_every = -1; static const char* g_label = "frames";   // CRET_PROFILE_EVERY (gprof_set_label)
static void make_queries(void) { if (!g_made) { for (int i = 0; i < RING; i++) glGenQueries(MAXM, g_fr[i].q); g_made = true; } }

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec * 1e-6; }
bool gprof_enabled(void) {
    if (g_on < 0) {
        g_on = getenv("CRET_GPU_PROFILE") != NULL;
        if (g_on) make_queries();
    }
    return g_on;
}
static int slot(const char* name) {
    for (int i = 0; i < g_nn; i++) if (!strcmp(g_names[i], name)) return i;
    if (g_nn == MAXN) return MAXN - 1;
    g_names[g_nn] = name; return g_nn++;
}
static void collect(Frame* f) {
    if (!f->pending) return;
    // Query i times the interval from mark i to mark i+1 (GL_TIME_ELAPSED:
    // the d3d12 driver returns 0 for GL_TIMESTAMP counters).
    double tot = 0;
    for (int i = 0; i + 1 < f->n; i++) {
        GLuint64 ns = 0; glGetQueryObjectui64v(f->q[i], GL_QUERY_RESULT, &ns);
        int s = slot(f->name[i]); g_gpu[s] += ns * 1e-6; tot += ns * 1e-6; g_cpu[s] += f->cpu[i + 1] - f->cpu[i];
    }
    if (f->n > 1) { g_frame_gpu += tot; g_frame_cpu += f->cpu[f->n - 1] - f->cpu[0]; }
    g_frames++; f->pending = false;
}
// Closes the "present" interval (swap, raylib, the caller's own work) that
// gprof_frame_end opened, so frame totals add up to the wall-clock frame.
static void close_open(void) {
    if (!g_open) return;
    Frame* f = &g_fr[g_cur]; glEndQuery(GL_TIME_ELAPSED);
    f->name[f->n] = "end"; f->cpu[f->n] = now_ms(); f->n++; f->pending = true; g_open = false;
}
void gprof_set_enabled(bool on) { g_req = on; }
void gprof_frame_begin(void) {
    if (g_req >= 0) {   // a run-time switch, applied between frames
        bool on = g_req; g_req = -1; gprof_enabled();
        if (on && !g_on) {
            make_queries();
            for (int i = 0; i < RING; i++) { g_fr[i].pending = false; g_fr[i].n = 0; }
            g_on = 1; g_open = false;
        } else if (!on && g_on) {
            close_open();
            for (int i = 0; i < RING; i++) collect(&g_fr[i]);
            g_on = 0;
        }
    }
    if (!gprof_enabled()) return;
    close_open();
    g_cur = (g_cur + 1) % RING; collect(&g_fr[g_cur]);   // results from RING frames ago
    g_fr[g_cur].n = 0;
    if (g_every < 0) g_every = getenv("CRET_PROFILE_EVERY") ? atoi(getenv("CRET_PROFILE_EVERY")) : 0;
    if (g_every > 0 && g_frames >= g_every) gprof_report(g_label);
}
void gprof_set_label(const char* label) {
    if (gprof_enabled() && g_every > 0 && g_frames > 0) gprof_report(g_label);   // the frames so far were the old label's
    g_label = label;
}
// CRET_PROFILE_LOAD=1 also times the passes of the first two frames (the
// first one compiles shader variants and fills the plant and prop caches).
static int g_lp_frames; static const char* g_lp_prev;
static void load_pass(const char* name) {
    if (g_lp_frames >= 2 || !cret_load_profiling()) return;
    glFinish();
    if (g_lp_prev) { char b[64]; snprintf(b, sizeof b, "frame %d: %s", g_lp_frames + 1, g_lp_prev); cret_load_mark(b); }
    g_lp_prev = name;
}
void gprof_mark(const char* name) {
    load_pass(name);
    if (!gprof_enabled()) return;
    Frame* f = &g_fr[g_cur]; if (f->n >= MAXM) return;
    if (f->n > 0 && f->n < MAXM) glEndQuery(GL_TIME_ELAPSED);
    if (f->n + 1 < MAXM) glBeginQuery(GL_TIME_ELAPSED, f->q[f->n]);
    f->name[f->n] = name; f->cpu[f->n] = now_ms(); f->n++;
}
void gprof_frame_end(void) {
    if (g_lp_frames < 2 && cret_load_profiling()) { load_pass(NULL); g_lp_prev = NULL; g_lp_frames++; }
    if (!gprof_enabled()) return;
    gprof_mark("present"); g_open = g_fr[g_cur].n < MAXM;
    if (!g_open) { glEndQuery(GL_TIME_ELAPSED); g_fr[g_cur].pending = true; }
}
void gprof_report(const char* label) {
    if (!gprof_enabled()) return;
    close_open();
    for (int i = 0; i < RING; i++) collect(&g_fr[i]);
    if (!g_frames) return;
    printf("gpu-profile %s (%d frames)  total GPU %.2f ms  CPU %.2f ms\n", label, g_frames, g_frame_gpu / g_frames, g_frame_cpu / g_frames);
    for (int i = 0; i < g_nn; i++)
        printf("  %-14s gpu %6.2f  cpu %6.2f\n", g_names[i], g_gpu[i] / g_frames, g_cpu[i] / g_frames);
    memset(g_gpu, 0, sizeof g_gpu); memset(g_cpu, 0, sizeof g_cpu); g_frames = 0; g_frame_gpu = g_frame_cpu = 0; g_nn = 0;
}
void gprof_reset(void) {
    if (!gprof_enabled()) return;
    close_open();
    for (int i = 0; i < RING; i++) collect(&g_fr[i]);
    memset(g_gpu, 0, sizeof g_gpu); memset(g_cpu, 0, sizeof g_cpu); g_frames = 0; g_frame_gpu = g_frame_cpu = 0; g_nn = 0;
}
int gprof_snapshot(const char** names, float* gpu, float* cpu, int max, float* frame_gpu, float* frame_cpu) {
    int n = 0;
    if (frame_gpu) *frame_gpu = 0;
    if (frame_cpu) *frame_cpu = 0;
    if (g_frames > 0) {
        for (int i = 0; i < g_nn && n < max; i++, n++) { names[n] = g_names[i]; gpu[n] = (float)(g_gpu[i] / g_frames); cpu[n] = (float)(g_cpu[i] / g_frames); }
        if (frame_gpu) *frame_gpu = (float)(g_frame_gpu / g_frames);
        if (frame_cpu) *frame_cpu = (float)(g_frame_cpu / g_frames);
    }
    memset(g_gpu, 0, sizeof g_gpu); memset(g_cpu, 0, sizeof g_cpu); g_frames = 0; g_frame_gpu = g_frame_cpu = 0; g_nn = 0;
    return n;
}
