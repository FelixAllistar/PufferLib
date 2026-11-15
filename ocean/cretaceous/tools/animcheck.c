// Animation check: reads .dino files (tools/dino_convert.py) without GL and
// measures what is otherwise judged by eye, per clip:
//   - slide: how fast a planted foot moves against the ground. The planted
//     foot is found as the converter finds it (each frame, the lowest joint
//     named toe, foot or metatars; the IK ankles when none is), counted only
//     while it is within 2% of the model's height of the clip's lowest foot
//     in that frame and the frames either side (lift-off, touch-down and a
//     trot's flight are not stance).
//     The ground runs backward at the clip's stored speed (clips are in
//     place), so slip = foot velocity + speed; reported as the mean slip over
//     the planted frames in % of the speed (cm/s for clips that stand).
//   - feet: the ground speed measured from the planted feet (the median),
//     against the stored speed the game plays the clip at.
//   - below / hover: the lowest skinned LOD0 vertex, under the ground plane
//     (y = 0, the converter's idle pose), and over it while a foot is planted.
//   - seam: for looping clips, the pose jump from the last frame back to the
//     first (the game loops over frames 0..n-1, so the two must match): the
//     largest local rotation difference and joint displacement.
//   - drift: the largest change of a bone's length (joint to parent) against
//     the rest pose (translation or scale keys in the wrong units).
//   - step / flips: the largest local rotation change between two frames
//     (a hemisphere jump pops the joint) and quaternion sign flips (harmless:
//     the game's blend aligns signs; listed for the record).
// and per sim gait of the species (walk, trot, sprint): the clips the game
// blends there, the playback rate it wants and the one it clamps to
// (render/dino.c, animate), and the slip that leaves (rate x the clips' foot
// speed against the sim's speed).
//   animcheck [--species NAME] [--check] [--limit NAME=VALUE]... [FILE.dino ...]
// With no files: every species' model in ocean/cretaceous/assets/dinos (run
// from the repository root). The species comes from the file name (the
// asset name in CRET_SPECIES_TABLE) unless --species names it. --check exits
// 1 when any limit is broken (limits below, overridable with --limit).
#include "../sim.h"
#include "../render/dino_ik.h"
#include "dino_file.h"
#include <stdarg.h>
#include <strings.h>
#include <math.h>

typedef struct { const char* name; float v; const char* what; } Limit;
static Limit LIMITS[] = {
    {"slide", 10.0f, "mean planted-foot slip in a gait clip, % of its speed"},
    {"slide_still", 5.0f, "mean planted-foot slip in an idle clip, cm/s"},
    {"feet", 10.0f, "stored speed against the feet's speed, % apart"},
    {"below", 2.0f, "lowest vertex under the ground, % of the model's height"},
    {"seam_deg", 3.0f, "loop seam, largest local rotation jump, degrees"},
    {"seam", 1.0f, "loop seam, largest joint displacement, % of the model's height"},
    {"drift", 5.0f, "bone length change against the rest pose, %"},
    {"step", 30.0f, "largest local rotation change between frames, degrees (900 deg/s)"},
    {"gait_slip", 10.0f, "slip left at a sim gait after the playback rate clamp, % of the speed"},
};
enum { L_SLIDE, L_SLIDE_STILL, L_FEET, L_BELOW, L_SEAM_DEG, L_SEAM, L_DRIFT, L_STEP, L_GAIT, L_COUNT };
static float lim(int k) { return LIMITS[k].v; }

static int g_fail;
static char g_why[8192];
// Records a broken limit: what was measured (printf style) and what it means on screen.
static void fail(const char* tag, const char* what, const char* fmt, ...) {
    char line[256]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    size_t n = strlen(g_why);
    snprintf(g_why + n, sizeof g_why - n, "  FAIL %s: %s (%s)\n", tag, line, what);
    g_fail++;
}

static bool is_foot(const char* low) { return strstr(low, "toe") || strstr(low, "foot") || strstr(low, "metatars"); }
static bool is_gait(const char* nm) { return !strcmp(nm, "walk") || !strcmp(nm, "trot") || !strcmp(nm, "run"); }
static bool is_idle(const char* nm) { return !strncmp(nm, "idle", 4); }
static float qangle(const float* a, const float* b) {
    float d = fabsf(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2.0f * acosf(fminf(d, 1.0f)) * 57.29578f;
}

typedef struct { float speed, feet, slide, below, hover, seam_deg, seam, drift, step; int flips, drift_j, step_j, planted; bool moving; } ClipStat;

static void skin_min(const DinoFile* d, float (*G)[12], float* ymin) {
    static float S[DF_MAX_JOINTS][12];
    for (int j = 0; j < d->njoint; j++) df_mul(G[j], d->inv_bind[j], S[j]);
    float lo = 1e30f;
    for (int i = 0; i < d->nvert; i++) {
        const DfVert* v = &d->vert[i]; float y = 0;
        for (int k = 0; k < 4; k++) {
            if (!v->w[k]) continue;
            const float* s = S[v->j[k]];
            y += v->w[k] / 255.0f * (s[4] * v->p[0] + s[5] * v->p[1] + s[6] * v->p[2] + s[7]);
        }
        lo = fminf(lo, y);
    }
    *ymin = lo;
}

static ClipStat clip_stats(const DinoFile* d, const DfClip* C, const int* feet, int nfeet) {
    ClipStat st = {0}; st.speed = C->speed; st.below = 1e30f; st.hover = 0; st.drift_j = st.step_j = -1;
    int n = C->nframes, J = d->njoint;
    float (*G)[DF_MAX_JOINTS][12] = malloc(sizeof(float) * 12 * DF_MAX_JOINTS * n);
    float* ylo = malloc(sizeof(float) * n);
    for (int f = 0; f < n; f++) { df_globals(d, C->frames + (size_t)f * J * 8, G[f]); skin_min(d, G[f], &ylo[f]); st.below = fminf(st.below, ylo[f]); }
    // Rest bone lengths.
    static float R[DF_MAX_JOINTS][12]; float rl[DF_MAX_JOINTS];
    df_globals(d, &d->rest[0][0], R);
    for (int j = 0; j < J; j++) {
        int p = d->parent[j]; rl[j] = 0;
        if (p >= 0) { float dx = R[j][3] - R[p][3], dy = R[j][7] - R[p][7], dz = R[j][11] - R[p][11]; rl[j] = sqrtf(dx * dx + dy * dy + dz * dz); }
    }
    for (int f = 0; f < n; f++) for (int j = 0; j < J; j++) {
        int p = d->parent[j];
        if (p >= 0 && rl[j] > 2e-3f * d->length) {
            float dx = G[f][j][3] - G[f][p][3], dy = G[f][j][7] - G[f][p][7], dz = G[f][j][11] - G[f][p][11];
            float r = fabsf(sqrtf(dx * dx + dy * dy + dz * dz) / rl[j] - 1) * 100;
            if (r > st.drift) { st.drift = r; st.drift_j = j; }
        }
        if (f + 1 < n) {
            const float* a = C->frames + ((size_t)f * J + j) * 8 + 3; const float* b = C->frames + ((size_t)(f + 1) * J + j) * 8 + 3;
            float ang = qangle(a, b);
            if (ang > st.step) { st.step = ang; st.step_j = j; }
            if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0) st.flips++;
        }
    }
    if (C->loop && n > 1) {
        for (int j = 0; j < J; j++) {
            st.seam_deg = fmaxf(st.seam_deg, qangle(C->frames + ((size_t)(n - 1) * J + j) * 8 + 3, C->frames + (size_t)j * 8 + 3));
            float dx = G[n - 1][j][3] - G[0][j][3], dy = G[n - 1][j][7] - G[0][j][7], dz = G[n - 1][j][11] - G[0][j][11];
            st.seam = fmaxf(st.seam, sqrtf(dx * dx + dy * dy + dz * dz));
        }
    }
    // Planted feet: the lowest foot joint each frame, near the clip's lowest foot height.
    float ymin = 1e30f;
    for (int f = 0; f < n; f++) for (int k = 0; k < nfeet; k++) ymin = fminf(ymin, G[f][feet[k]][7]);
    float tol = 0.02f * d->height, sum = 0; int span = C->loop && n > 2 ? n - 1 : n;
    float* vx = malloc(sizeof(float) * n); int nv = 0;
    for (int f = 0; f < span && nfeet; f++) {
        int jb = feet[0];
        for (int k = 1; k < nfeet; k++) if (G[f][feet[k]][7] < G[f][jb][7]) jb = feet[k];
        int f0 = f - 1, f1 = f + 1;
        if (C->loop && n > 2) { f0 = (f0 + span) % span; f1 %= span; } else if (f0 < 0 || f1 >= n) continue;
        // On the ground in this frame and both neighbours: lift-off, touch-down and a trot's flight are not stance.
        if (G[f][jb][7] > ymin + tol || G[f0][jb][7] > ymin + tol || G[f1][jb][7] > ymin + tol) continue;
        float v[3] = {(G[f1][jb][3] - G[f0][jb][3]) * 15.0f, (G[f1][jb][7] - G[f0][jb][7]) * 15.0f, (G[f1][jb][11] - G[f0][jb][11]) * 15.0f};
        float s[3] = {v[0] + C->speed, v[1], v[2]};
        sum += sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); vx[nv++] = -v[0];
        st.hover = fmaxf(st.hover, ylo[f]);
    }
    st.planted = nv;
    if (nv) {
        for (int a = 1; a < nv; a++) for (int b = a; b > 0 && vx[b] < vx[b - 1]; b--) { float t = vx[b]; vx[b] = vx[b - 1]; vx[b - 1] = t; }
        st.feet = nv & 1 ? vx[nv / 2] : 0.5f * (vx[nv / 2 - 1] + vx[nv / 2]);
        st.moving = C->speed > 0.05f;
        st.slide = st.moving ? sum / nv / C->speed * 100 : sum / nv * 100;
    }
    free(vx); free(ylo); free(G);
    return st;
}

static int find_clip(const DinoFile* d, const char* nm) { for (int c = 0; c < d->nclip; c++) if (!strcmp(d->clip[c].name, nm)) return c; return -1; }

static void check_file(const char* path, int species) {
    static DinoFile D; DinoFile* d = &D;
    const char* base = strrchr(path, '/'); base = base ? base + 1 : path;
    if (!df_read(d, path)) { printf("%s: cannot read (missing or not a version 1 .dino)\n", path); fail(base, "the game would draw capsules", "unreadable"); return; }
    if (species < 0) {
        char stem[64]; snprintf(stem, sizeof stem, "%s", base); char* dot = strrchr(stem, '.'); if (dot) *dot = 0;
        for (int s = 0; s < CRET_SPECIES; s++) if (!strcmp(stem, CRET_SPECIES_TABLE[s].asset)) species = s;
    }
    const CretSpecies* S = species >= 0 ? &CRET_SPECIES_TABLE[species] : NULL;
    // Legs and feet as the game and the converter find them.
    static DinoLegs legs; dino_find_legs(&legs, d->njoint, d->parent, (const float (*)[8])d->rest, d->height, d->low);
    int feet[DF_MAX_JOINTS], nfeet = 0, nfoot_names = 0;
    for (int j = 0; j < d->njoint; j++) if (is_foot(d->low[j])) feet[nfeet++] = j;
    for (int j = 0; j < d->njoint; j++) if (strstr(d->low[j], "foot") && !strstr(d->low[j], "ik")) nfoot_names++;
    bool by_ik = false;
    if (!nfeet) { for (int l = 0; l < legs.nleg; l++) feet[nfeet++] = legs.leg[l].ankle; by_ik = true; }
    bool quad = nfoot_names >= 4 || strstr(path, "sauropelta");   // render/dino.c, load_model
    printf("%s: %s, %d joints, %d LOD0 vertices, length %.2f m", base, S ? S->asset : "no species", d->njoint, d->nvert, d->length);
    if (S) printf(" (sim %.2f)", S->length);
    printf(", height %.2f m, %s, %d IK legs, feet (%s):", d->height, quad ? "quadruped" : "biped", legs.nleg, by_ik ? "IK ankles" : "by name");
    for (int k = 0; k < nfeet && k < 6; k++) printf(" %s", d->name[feet[k]]);
    if (nfeet > 6) printf(" +%d", nfeet - 6);
    printf("\n");
    if (S && fabsf(d->length - S->length) > 0.02f * S->length) fail(base, "the model is drawn at its own length", "model length %.2f m, the sim's %.2f m", d->length, S->length);
    printf("  %-14s %6s %4s %7s %7s %8s %7s %7s %7s %6s %7s %7s %6s\n", "clip", "frames", "loop", "speed", "feet", "slide", "below", "hover", "seam", "seam", "drift", "step", "flips");
    printf("  %-14s %6s %4s %7s %7s %8s %7s %7s %7s %6s %7s %7s %6s\n", "", "", "", "m/s", "m/s", "", "cm", "cm", "deg", "cm", "%", "deg", "");
    float feet_speed[48] = {0};
    for (int c = 0; c < d->nclip; c++) {
        const DfClip* C = &d->clip[c];
        ClipStat st = clip_stats(d, C, feet, nfeet);
        feet_speed[c] = st.planted ? st.feet : C->speed;
        char slide[16] = "-", fs[16] = "-";
        if (st.planted) { snprintf(slide, sizeof slide, st.moving ? "%.1f%%" : "%.1fcm/s", st.slide); snprintf(fs, sizeof fs, "%.2f", st.feet); }
        char seam[16] = "-", seamc[16] = "-";
        if (C->loop) { snprintf(seam, sizeof seam, "%.2f", st.seam_deg); snprintf(seamc, sizeof seamc, "%.2f", st.seam * 100); }
        printf("  %-14s %6d %4s %7.2f %7s %8s %7.2f %7.2f %7s %6s %7.2f %7.1f %6d", C->name, C->nframes, C->loop ? "yes" : "no", C->speed, fs, slide,
               st.below * 100, st.hover * 100, seam, seamc, st.drift, st.step, st.flips);
        if (st.drift > lim(L_DRIFT) && st.drift_j >= 0) printf("  drift: %s", d->name[st.drift_j]);
        if (st.step > lim(L_STEP) && st.step_j >= 0) printf("  step: %s", d->name[st.step_j]);
        printf("\n");
        char tag[300]; snprintf(tag, sizeof tag, "%s %s", base, C->name);
        bool gait = is_gait(C->name) || C->speed > 0.05f;
        if (st.planted && gait && st.moving && st.slide > lim(L_SLIDE)) fail(tag, "feet skate on the ground", "planted feet slip %.1f%% of the speed (limit %.0f%%)", st.slide, lim(L_SLIDE));
        if (st.planted && is_idle(C->name) && !st.moving && st.slide > lim(L_SLIDE_STILL)) fail(tag, "feet shuffle while standing", "standing feet slip %.1f cm/s (limit %.0f)", st.slide, lim(L_SLIDE_STILL));
        if (st.planted && gait && st.moving && fabsf(st.feet / C->speed - 1) * 100 > lim(L_FEET))
            fail(tag, "playback at the stored speed slides the feet", "feet move at %.2f m/s, the clip is stored at %.2f m/s", st.feet, C->speed);
        if (st.below < -lim(L_BELOW) * 0.01f * d->height) fail(tag, "sinks into the ground before foot IK", "lowest vertex %.1f cm under the ground (limit %.1f cm)", st.below * 100, lim(L_BELOW) * d->height);
        if (C->loop && st.seam_deg > lim(L_SEAM_DEG)) fail(tag, "pops once a cycle", "loop seam %.1f deg (limit %.0f)", st.seam_deg, lim(L_SEAM_DEG));
        if (C->loop && st.seam > lim(L_SEAM) * 0.01f * d->height) fail(tag, "pops once a cycle", "loop seam %.1f cm (limit %.1f cm)", st.seam * 100, lim(L_SEAM) * d->height);
        if (st.drift > lim(L_DRIFT)) fail(tag, d->name[st.drift_j], "a bone stretches %.1f%% (limit %.0f%%)", st.drift, lim(L_DRIFT));
        if (st.step > lim(L_STEP)) fail(tag, d->name[st.step_j], "a joint turns %.0f deg in one frame (limit %.0f)", st.step, lim(L_STEP));
    }
    // The game's gait blend (render/dino.c, load_model and animate) at each sim gait, adult size.
    int slot_w = find_clip(d, "walk"), slot_t = find_clip(d, "trot"), slot_r = find_clip(d, "run"), loco[3], nloco = 0;
    bool idle_loco = find_clip(d, "idle") < 0;
    float cs[48]; for (int c = 0; c < d->nclip; c++) cs[c] = d->clip[c].speed;
    int roles[3] = {slot_w, slot_t, slot_r};
    for (int i = 0; i < 3; i++) {
        if (roles[i] < 0 || cs[roles[i]] <= 0.05f) continue;
        if (nloco && cs[roles[i]] <= cs[loco[nloco - 1]]) { printf("  note: %s (%.2f m/s) is not faster than %s: the game plays it as %.2f m/s\n", d->clip[roles[i]].name, cs[roles[i]], d->clip[loco[nloco - 1]].name, cs[loco[nloco - 1]] * 1.3f); cs[roles[i]] = cs[loco[nloco - 1]] * 1.3f; }
        loco[nloco++] = roles[i];
    }
    if (!nloco) { printf("  no gait clip (walk, trot or run with a speed): the game cannot use this model\n"); fail(base, "walk, trot or run", "no gait clip"); df_free(d); return; }
    if (!S) { printf("\n"); df_free(d); return; }
    printf("  %-8s %7s %-22s %7s %7s %7s %7s\n", "sim gait", "m/s", "clips (weight)", "stride", "rate", "clamped", "slip");
    const char* gname[3] = {"walk", "trot", "sprint"}; float gv[3] = {S->walk, S->trot, S->sprint};
    for (int g = 0; g < 3; g++) {
        float v = gv[g]; int ca = loco[0], cb = loco[0]; float wb = 0;
        if (nloco > 1)
            for (int k = 0; k + 1 < nloco; k++) {
                float s0 = cs[loco[k]], s1 = cs[loco[k + 1]];
                if (v <= s1 || k + 2 == nloco) { ca = loco[k]; cb = loco[k + 1]; wb = fminf(fmaxf((v - s0) / fmaxf(s1 - s0, 0.01f), 0), 1); break; }
            }
        float clip_speed = cs[ca] * (1 - wb) + cs[cb] * wb, foot = feet_speed[ca] * (1 - wb) + feet_speed[cb] * wb;
        float amp = 1; if (idle_loco && nloco == 1) amp = fminf(fmaxf(v / fmaxf(clip_speed, 0.05f), 0.3f), 1.0f);
        float want = v / fmaxf(clip_speed * amp, 0.05f), lo = nloco == 1 ? 0.4f : 0.55f, hi = nloco == 1 ? 4.5f : 2.4f;
        float rate = fminf(fmaxf(want, lo), hi), slip = fabsf(rate * foot * amp - v) / v * 100;
        char cl[64]; if (cb != ca) snprintf(cl, sizeof cl, "%s %.2f + %s %.2f", d->clip[ca].name, 1 - wb, d->clip[cb].name, wb); else snprintf(cl, sizeof cl, "%s", d->clip[ca].name);
        printf("  %-8s %7.2f %-22s %7.2f %7.2f %7s %6.1f%%\n", gname[g], v, cl, amp, want, rate != want ? "yes" : "no", slip);
        char tag[300]; snprintf(tag, sizeof tag, "%s at the sim's %s", base, gname[g]);
        if (slip > lim(L_GAIT)) fail(tag, rate != want ? "the playback rate is clamped" : "the clips' feet do not match their stored speed", "feet slip %.1f%% of %.2f m/s", slip, v);
    }
    printf("\n");
    df_free(d);
}

int main(int argc, char** argv) {
    bool check = false; int species = -1; const char* files[256]; int nf = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "--species") && i + 1 < argc) {
            const char* s = argv[++i];
            for (int k = 0; k < CRET_SPECIES; k++) if (!strcasecmp(s, CRET_SPECIES_TABLE[k].asset) || !strcasecmp(s, CRET_SPECIES_TABLE[k].name)) species = k;
            if (species < 0) { fprintf(stderr, "animcheck: unknown species %s\n", s); return 2; }
        } else if (!strcmp(argv[i], "--limit") && i + 1 < argc) {
            char nm[32]; float v; const char* a = argv[++i]; const char* eq = strchr(a, '=');
            if (!eq || eq - a >= 31 || sscanf(eq + 1, "%f", &v) != 1) { fprintf(stderr, "animcheck: --limit NAME=VALUE\n"); return 2; }
            memcpy(nm, a, eq - a); nm[eq - a] = 0; bool found = false;
            for (int k = 0; k < L_COUNT; k++) if (!strcmp(LIMITS[k].name, nm)) { LIMITS[k].v = v; found = true; }
            if (!found) { fprintf(stderr, "animcheck: no limit %s\n", nm); return 2; }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: animcheck [--species NAME] [--check] [--limit NAME=VALUE]... [FILE.dino ...]\nlimits:\n");
            for (int k = 0; k < L_COUNT; k++) fprintf(stderr, "  %-12s %6.1f  %s\n", LIMITS[k].name, LIMITS[k].v, LIMITS[k].what);
            return 2;
        } else if (nf < 256) files[nf++] = argv[i];
    }
    static char paths[CRET_SPECIES][256];
    if (!nf) for (int s = 0; s < CRET_SPECIES; s++) { snprintf(paths[s], sizeof paths[s], "ocean/cretaceous/assets/dinos/%s.dino", CRET_SPECIES_TABLE[s].asset); files[nf++] = paths[s]; }
    for (int i = 0; i < nf; i++) check_file(files[i], species);
    if (g_fail) printf("%s", g_why);
    printf("animcheck: %s (%d file%s, %d problem%s)\n", g_fail ? "FAILED" : "ok", nf, nf == 1 ? "" : "s", g_fail, g_fail == 1 ? "" : "s");
    return check && g_fail ? 1 : 0;
}
