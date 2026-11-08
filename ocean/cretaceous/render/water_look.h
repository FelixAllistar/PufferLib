// water_look.h: the tunable look of flow-mapped water, read from a small
// text file so it can be tuned while the game runs.
// Single header, C99, libc only; include it where the water shader's
// uniforms are set.
//
// File format (an .ini subset): one `key = value` per line; colours take
// three numbers. `;` and `#` start comments, [sections] are ignored. Keys
// missing from the file keep their defaults; unknown keys are reported.
//
//   flow_speed      m/s of surface drift per unit of steepness (x2.5 at the steepest)
//   max_speed       soft cap on the drift, m/s (tanh: about this fast at most)
//   ripple_scale    ripple size multiplier (bigger: broader ripples)
//   ripple_strength ripple slope multiplier (smaller: calmer, glassier)
//   fast_gloss      0..1: fast water flattens along the current, so its
//                   glints stretch into smooth streaks (0 off)
//   foam            rapids foam amount (shore foam is separate)
//   sparkle         near-field glint brightness multiplier
//   sparkle_size    metres: wavelength of the fine chop that makes the glints
//   sparkle_peak    cap on a near-field glint (bounded energy: dots stay small)
//   colour          in-scatter colour of clear water (linear RGB)
//   silt            in-scatter colour of silty water past a river mouth
//   turbidity       0..1: how silty a river plume gets
//   clarity         absorption divisor (2: light reaches twice as deep)
#ifndef WATER_LOOK_H
#define WATER_LOOK_H
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct WaterLook {
    float flow_speed, max_speed, ripple_scale, ripple_strength, fast_gloss;
    float foam, sparkle, sparkle_size, sparkle_peak;
    float colour[3], silt[3], turbidity, clarity;
} WaterLook;

static inline WaterLook water_look_defaults(void) {
    WaterLook w = {1.2f, 2.2f, 1.0f, 1.0f, 0.55f, 0.35f, 1.0f, 1.4f, 6.0f,
                   {0.012f, 0.075f, 0.085f}, {0.075f, 0.07f, 0.045f}, 0.8f, 1.0f};
    return w;
}

// Reads `path` over *w (keys not in the file are left as they are).
// Returns the number of keys read, or -1 if the file cannot be opened.
static inline int water_look_load(const char* path, WaterLook* w) {
    FILE* f = fopen(path, "r"); if (!f) return -1;
    static const struct { const char* key; size_t off; int n; } K[] = {
        {"flow_speed", offsetof(WaterLook, flow_speed), 1}, {"max_speed", offsetof(WaterLook, max_speed), 1},
        {"ripple_scale", offsetof(WaterLook, ripple_scale), 1}, {"ripple_strength", offsetof(WaterLook, ripple_strength), 1},
        {"fast_gloss", offsetof(WaterLook, fast_gloss), 1}, {"foam", offsetof(WaterLook, foam), 1},
        {"sparkle", offsetof(WaterLook, sparkle), 1}, {"sparkle_size", offsetof(WaterLook, sparkle_size), 1},
        {"sparkle_peak", offsetof(WaterLook, sparkle_peak), 1}, {"colour", offsetof(WaterLook, colour), 3},
        {"silt", offsetof(WaterLook, silt), 3}, {"turbidity", offsetof(WaterLook, turbidity), 1},
        {"clarity", offsetof(WaterLook, clarity), 1}};
    char line[256]; int read = 0, ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char* c = strpbrk(line, ";#"); if (c) *c = 0;
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq = 0; char* k = line; while (*k == ' ' || *k == '\t') k++;
        char* e = eq; while (e > k && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        size_t i = 0, nk = sizeof K / sizeof K[0];
        while (i < nk && strcmp(K[i].key, k)) i++;
        if (i == nk) { fprintf(stderr, "water_look: %s:%d: unknown key '%s'\n", path, ln, k); continue; }
        float v[3]; char* p = eq + 1; int got = 0;
        for (; got < K[i].n; got++) { char* q; v[got] = strtof(p, &q); if (q == p) break; p = q; }
        if (got != K[i].n) { fprintf(stderr, "water_look: %s:%d: '%s' needs %d number%s\n", path, ln, k, K[i].n, K[i].n > 1 ? "s" : ""); continue; }
        memcpy((char*)w + K[i].off, v, sizeof(float) * K[i].n); read++;
    }
    fclose(f);
    return read;
}
#endif
