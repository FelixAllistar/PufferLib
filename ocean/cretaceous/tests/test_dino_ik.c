// Foot IK (render/dino_ik.c) on synthetic ground, without GL or a world:
// the test models are tests/fixtures/*.dino (tools/blender/make_fixture.py),
// the ground is a stub cret_ground_at (flat, slopes of 15, 30 and 45 degrees
// up the body and across it, a step, a low rock top that a foot stands on and
// a tall one it does not). For a standing pose (run until the smoothed
// pelvis settles) and a walk cycle played on top, it checks:
//   - each foot's contact (its lowest joint) ends at its animated height over
//     the ground under it, within 5 mm, unless that is out of reach (the
//     leg straight, or the IK's own limits on how far a foot may move);
//   - the knee stays in the leg's bend plane (the plane of hip, knee and
//     ankle as animated): it tilts no more than moving the foot out of that
//     plane forces, and does not twist about the leg;
//   - the pelvis drops no further than the highest foot can fold (0.55 of
//     the hip height), within its own limits;
//   - planted feet tilt toward the ground normal by the IK's amount;
//   - with the weight at 0 the legs are untouched, and nothing is NaN, also
//     for ground far above or below the feet.
// Runs from the repository root: ./build/cretaceous/test/test_dino_ik
#include "../render/dino_ik.h"
#include "../tools/dino_file.h"
#include "test_util.h"

// ---------------------------------------------------------------- the ground
enum { G_FLAT, G_SLOPE, G_STEP, G_ROCK };
typedef struct { int kind; float slope, dir;   // slope (rad) rising toward world direction dir (rad, 0 = +x)
                 float step_x, step_h;          // a step up of step_h for x >= step_x
                 float rock_x, rock_z, rock_r, rock_h; } Ground;   // a flat-topped rock: a disc of radius rock_r, top rock_h
static Ground g_ground;
static float terrain(float x, float z) {
    const Ground* g = &g_ground;
    if (g->kind == G_SLOPE) return tanf(g->slope) * (x * cosf(g->dir) + z * sinf(g->dir));
    if (g->kind == G_STEP) return x >= g->step_x ? g->step_h : 0;
    return 0;
}
// As sim.c's: the terrain, or a prop's top when the ray down from y_top meets
// it above the terrain (here the rock's top, normal up).
float cret_ground_at(const CretSim* s, float x, float z, float y_top, float normal[3]) {
    const Ground* g = &g_ground; float h = terrain(x, z);
    if (normal) {
        if (g->kind == G_SLOPE) { float t = tanf(g->slope), n[3] = {-t * cosf(g->dir), 1, -t * sinf(g->dir)}, l = sqrtf(n[0] * n[0] + 1 + n[2] * n[2]); for (int k = 0; k < 3; k++) normal[k] = n[k] / l; }
        else { normal[0] = 0; normal[1] = 1; normal[2] = 0; }
    }
    if (g->kind == G_ROCK && (x - g->rock_x) * (x - g->rock_x) + (z - g->rock_z) * (z - g->rock_z) < g->rock_r * g->rock_r && y_top > g->rock_h) {
        if (normal) { normal[0] = 0; normal[1] = 1; normal[2] = 0; }
        return g->rock_h;
    }
    return h;
}

// ---------------------------------------------------------------- helpers
// The body transform as render/dino.c's body_matrix builds it.
static void body_matrix(float x, float y, float z, float yaw, float pitch, float roll, float scale, float* o) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    float Ry[9] = {cy, 0, -sy, 0, 1, 0, sy, 0, cy}, Rz[9] = {cp, -sp, 0, sp, cp, 0, 0, 0, 1}, Rx[9] = {1, 0, 0, 0, cr, -sr, 0, sr, cr}, t[9], r[9];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t[i * 3 + j] = Rz[i * 3] * Rx[j] + Rz[i * 3 + 1] * Rx[3 + j] + Rz[i * 3 + 2] * Rx[6 + j];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i * 3 + j] = Ry[i * 3] * t[j] + Ry[i * 3 + 1] * t[3 + j] + Ry[i * 3 + 2] * t[6 + j];
    for (int i = 0; i < 3; i++) { o[i * 4] = r[i * 3] * scale; o[i * 4 + 1] = r[i * 3 + 1] * scale; o[i * 4 + 2] = r[i * 3 + 2] * scale; }
    o[3] = x; o[7] = y; o[11] = z;
}
static void xform(const float* M, const float* g, float* o) { for (int r = 0; r < 3; r++) o[r] = M[r * 4] * g[3] + M[r * 4 + 1] * g[7] + M[r * 4 + 2] * g[11] + M[r * 4 + 3]; }
static void sub3(const float* a, const float* b, float* o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static void cross3(const float* a, const float* b, float* o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float len3(const float* a) { return sqrtf(dot3(a, a)); }
static float clampf_(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float sstep_(float a, float b, float x) { float t = clampf_((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static bool finite_all(const float* p, int n) { for (int i = 0; i < n; i++) if (!isfinite(p[i])) return false; return true; }

// ---------------------------------------------------------------- one model
typedef struct { const char* name; float yaw, pitch, roll; Ground g; } Scene;
typedef struct { float err, knee, tilt_err, fold; int reach, out_of_reach; bool nan; } Result;
static float g_lag;   // walking: the most the smoothed pelvis lagged below the fold limit, in hip heights

// The legs' contact joints (as dino_foot_ik picks them) for frames G.
static int contact(const DinoLegs* L, int nj, float (*G)[12], int l) {
    int c = L->leg[l].ankle;
    for (int j = 0; j < nj; j++) if ((L->leg_bits[j] >> (8 + l) & 1) && G[j][7] < G[c][7]) c = j;
    return c;
}

// Runs the IK once on frames Gin (model space) with body transform Min and
// measures the result against what the IK was asked to do.
static void measure(const DinoFile* d, const DinoLegs* L, float (*Gin)[12], const float* Min, float gs, float pelvis, float (*G)[12], const float* M, Result* r) {
    float hip_w = L->hip_y * gs, dy[4], lo = 1e30f, hi = -1e30f;
    r->nan |= !finite_all(&G[0][0], 12 * d->njoint) || !finite_all(M, 12) || !isfinite(pelvis);
    for (int l = 0; l < L->nleg; l++) {
        int c = contact(L, d->njoint, Gin, l); float F[3]; xform(Min, Gin[c], F);
        float plane_y = Min[4] * Gin[c][3] + Min[6] * Gin[c][11] + Min[7], nrm[3];
        dy[l] = cret_ground_at(NULL, F[0], F[2], plane_y + 0.5f * hip_w, nrm) - plane_y;
        lo = fminf(lo, dy[l]); hi = fmaxf(hi, dy[l]);
    }
    // The pelvis: no lower than the highest foot can fold to (within the IK's clamp).
    float fold = 0.55f * hip_w, floor_ = fminf(hi - fold, 0.25f * hip_w);
    r->fold = fminf(r->fold, pelvis - floor_);
    for (int l = 0; l < L->nleg; l++) {
        int h = L->leg[l].hip, k = L->leg[l].knee, a = L->leg[l].ankle, c = contact(L, d->njoint, Gin, l);
        float F[3], F2[3]; xform(Min, Gin[c], F); xform(M, G[c], F2);
        float target = F[1] + dy[l];   // its animated height over the plane, now over the ground
        // Out of reach: the IK clamps how far the foot moves against the body, or the leg is straight.
        float rem = dy[l] - pelvis;
        bool clamped = rem < -0.3f * hip_w + 1e-4f || rem > 0.75f * hip_w - 1e-4f;
        float H[3] = {G[h][3], G[h][7], G[h][11]}, K[3] = {G[k][3], G[k][7], G[k][11]}, A[3] = {G[a][3], G[a][7], G[a][11]};
        float H0[3] = {Gin[h][3], Gin[h][7], Gin[h][11]}, K0[3] = {Gin[k][3], Gin[k][7], Gin[k][11]}, A0[3] = {Gin[a][3], Gin[a][7], Gin[a][11]};
        float hk[3], ka[3], ha[3]; sub3(K0, H0, hk); sub3(A0, K0, ka); sub3(A, H, ha);
        bool straight = len3(ha) >= 0.998f * (len3(hk) + len3(ka));
        float e = fabsf(F2[1] - target) + hypotf(F2[0] - F[0], F2[2] - F[2]);
        r->out_of_reach += clamped || straight;
        if (!clamped && !straight) { r->reach++; r->err = fmaxf(r->err, e); }
        // Bend plane: the normal n0 of (hip, knee, ankle) as animated against n1 after. A foot moved out
        // of that plane tilts it by at least asin(|n0 . u|) (u the new hip-ankle line); what turns it
        // beyond that is a twist about the leg. Straight legs (no plane) are skipped.
        float n0[3], n1[3], ha0[3], hk1[3]; sub3(A0, H0, ha0); cross3(hk, ha0, n0); sub3(K, H, hk1); cross3(hk1, ha, n1);
        float c0 = len3(n0), c1 = len3(n1), la = len3(ha);
        if (c0 > 0.05f * len3(hk) * len3(ha0) && c1 > 0.05f * len3(hk1) * la) {
            float ang = acosf(clampf_(dot3(n0, n1) / (c0 * c1), -1, 1)), forced = asinf(clampf_(fabsf(dot3(n0, ha)) / (c0 * la), 0, 1));
            r->knee = fmaxf(r->knee, (ang - forced) * 57.29578f);
        }
        // Foot tilt: the contact frame's up axis turns toward the ground normal by min(angle, 0.4 rad x plant).
        float nrm[3]; cret_ground_at(NULL, F[0], F[2], Min[4] * Gin[c][3] + Min[6] * Gin[c][11] + Min[7] + 0.5f * hip_w, nrm);
        float nm[3] = {Min[0] * nrm[0] + Min[4] * nrm[1] + Min[8] * nrm[2], Min[1] * nrm[0] + Min[5] * nrm[1] + Min[9] * nrm[2], Min[2] * nrm[0] + Min[6] * nrm[1] + Min[10] * nrm[2]};
        float nl = len3(nm); for (int q = 0; q < 3; q++) nm[q] /= nl;
        float plant = 1 - sstep_(0.06f, 0.25f, Gin[c][7] / fmaxf(L->hip_y, 0.1f));
        float expect = fminf(acosf(clampf_(nm[1], -1, 1)), 0.4f * plant);
        float tr = 0;   // the contact frame's turn: the angle of R1 R0^T
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) tr += G[c][i * 4 + j] * Gin[c][i * 4 + j];
        float s2 = dot3((float[3]){Gin[c][0], Gin[c][4], Gin[c][8]}, (float[3]){Gin[c][0], Gin[c][4], Gin[c][8]});   // the frame's scale squared
        float got = acosf(clampf_((tr / s2 - 1) * 0.5f, -1, 1));
        if (!clamped && !straight) r->tilt_err = fmaxf(r->tilt_err, fabsf(got - expect) * 57.29578f);
    }
}

static void run_scene(const char* model, const DinoFile* d, const DinoLegs* L, const Scene* sc, float gs) {
    g_ground = sc->g;
    static float G0[DF_MAX_JOINTS][12], G[DF_MAX_JOINTS][12];
    int idle = -1, walk = -1;
    for (int c = 0; c < d->nclip; c++) { if (!strcmp(d->clip[c].name, "idle")) idle = c; if (!strcmp(d->clip[c].name, "walk")) walk = c; }
    // Three passes: the idle pose held until the smoothed pelvis settles (2 s at 60 fps); every walk
    // frame held the same way; the walk played (two cycles at 60 fps, measured over the second).
    // The pelvis rule holds where it has settled; walking, the smoothing lags behind it (reported).
    for (int pass = 0; pass < 3; pass++) {
        int ci = pass ? walk : idle; if (ci < 0) continue;
        const DfClip* C = &d->clip[ci];
        float x0 = 0.3f, z0 = -0.2f, M0[12], M[12];
        body_matrix(x0, terrain(x0, z0), z0, sc->yaw, sc->pitch, sc->roll, gs, M0);
        Result r = {0}; r.fold = 1e30f;
        int poses = pass == 1 ? C->nframes : 1, steps = pass == 2 ? (int)(2 * C->duration * 60) : 120;
        float pelvis = 0;
        for (int q = 0; q < poses; q++) {
            if (pass < 2) pelvis = 0;
            for (int s = 0; s < steps; s++) {
                float t = s / 60.0f, fr = fmodf(t * 30.0f, (float)(C->nframes > 1 ? C->nframes - 1 : 1));
                df_globals(d, C->frames + (size_t)(pass == 2 ? (int)fr : q) * d->njoint * 8, G0);
                memcpy(G, G0, sizeof(float) * 12 * d->njoint); memcpy(M, M0, sizeof M);
                dino_foot_ik(NULL, L, d->njoint, &pelvis, G, M, gs, 1.0f, 1.0f / 60, 0);
                if (pass == 2 ? s >= steps / 2 : s == steps - 1) measure(d, L, G0, M0, gs, pelvis, G, M, &r);
                else r.nan |= !finite_all(&G[0][0], 12 * d->njoint) || !finite_all(M, 12) || !isfinite(pelvis);
            }
        }
        const char* what = pass == 0 ? "stand" : pass == 1 ? "poses" : "walk";
        printf("  %-10s %-22s %-5s %4.1f %7.2f %6d %6d %8.2f %8.2f %8.3f %7.3f\n", model, sc->name, what, gs, r.err * 1000, r.reach, r.out_of_reach,
               r.knee, r.tilt_err, pelvis, r.fold);
        CHECK(!r.nan, "%s %s %s: NaN in the IK output", model, sc->name, what);
        CHECK(r.reach == 0 || r.err < 0.005f, "%s %s %s: a reachable foot is %.1f mm off the ground", model, sc->name, what, r.err * 1000);
        CHECK(r.knee < 1.0f, "%s %s %s: a knee left its bend plane by %.1f deg", model, sc->name, what, r.knee);
        if (pass < 2) CHECK(r.fold > -1e-4f, "%s %s %s: the pelvis dropped %.3f m below what the highest foot folds to", model, sc->name, what, -r.fold);
        CHECK(r.tilt_err < 0.5f, "%s %s %s: a foot tilts %.2f deg off the IK's amount", model, sc->name, what, r.tilt_err);
        g_lag = fmaxf(g_lag, pass == 2 ? -r.fold / (L->hip_y * gs) : 0);
    }
}

// The fade: with the weight at 0 the legs are untouched; NaN-free for ground far away.
static void run_edges(const char* model, const DinoFile* d, const DinoLegs* L) {
    static float G0[DF_MAX_JOINTS][12], G[DF_MAX_JOINTS][12];
    float M0[12], M[12]; body_matrix(0, 0, 0, 0.4f, 0, 0, 1, M0);
    df_globals(d, d->clip[0].frames, G0);
    g_ground = (Ground){.kind = G_SLOPE, .slope = 0.5f, .dir = 0.3f};
    float pelvis = 0.2f; memcpy(G, G0, sizeof(float) * 12 * d->njoint); memcpy(M, M0, sizeof M);
    dino_foot_ik(NULL, L, d->njoint, &pelvis, G, M, 1, 0.0f, 1.0f / 60, 0);
    CHECK(!memcmp(G, G0, sizeof(float) * 12 * d->njoint), "%s: weight 0 moved the legs", model);
    CHECK(pelvis < 0.2f && pelvis > 0 && fabsf(M[7] - pelvis) < 1e-6f, "%s: weight 0: the pelvis drop eases out (%.3f) and only it moves the body", model, pelvis);
    float hs[] = {-50, -2, 2, 50};
    for (int k = 0; k < 4; k++) {
        g_ground = (Ground){.kind = G_STEP, .step_x = -100, .step_h = hs[k]};
        pelvis = 0;
        for (int s = 0; s < 60; s++) {
            memcpy(G, G0, sizeof(float) * 12 * d->njoint); memcpy(M, M0, sizeof M);
            dino_foot_ik(NULL, L, d->njoint, &pelvis, G, M, 1, 1, 1.0f / 60, 0.3f);
        }
        CHECK(finite_all(&G[0][0], 12 * d->njoint) && finite_all(M, 12) && isfinite(pelvis), "%s: ground at %+.0f m: NaN", model, hs[k]);
        CHECK(pelvis >= -0.45f * L->hip_y - 1e-4f && pelvis <= 0.25f * L->hip_y + 1e-4f, "%s: ground at %+.0f m: pelvis %.3f outside its limits", model, hs[k], pelvis);
    }
}

int main(void) {
    double t0 = t_now();
    const char* files[2] = {"ocean/cretaceous/tests/fixtures/fixture_biped.dino", "ocean/cretaceous/tests/fixtures/fixture_quad.dino"};
    printf("  %-10s %-22s %-5s %4s %7s %6s %6s %8s %8s %8s %7s\n", "model", "ground", "pose", "gs", "err mm", "reach", "out", "knee deg", "tilt deg", "pelvis m", "fold m");
    for (int f = 0; f < 2; f++) {
        static DinoFile D; static DinoLegs L;
        CHECK(df_read(&D, files[f]), "cannot read %s", files[f]);
        if (!D.njoint) continue;
        dino_find_legs(&L, D.njoint, D.parent, (const float (*)[8])D.rest, D.height, D.low);
        const char* model = f ? "quadruped" : "biped";
        CHECK(L.nleg == (f ? 4 : 2), "%s: %d legs found by name", model, L.nleg);
        int nfront = 0; for (int l = 0; l < L.nleg; l++) nfront += L.leg[l].front;
        CHECK(nfront == (f ? 2 : 0), "%s: %d front legs", model, nfront);
        CHECK(L.hip_y > 0.5f && L.hip_y < 1.2f, "%s: hip height %.2f m", model, L.hip_y);
        // Rock under the left hind (biped: left) foot of the idle pose, 0.25 m (low) or 0.7 m (taller than half the hip height).
        static float G[DF_MAX_JOINTS][12]; df_globals(&D, D.clip[0].frames, G);
        float M[12]; body_matrix(0.3f, 0, -0.2f, 0, 0, 0, 1, M);
        int lf = 0; for (int l = 0; l < L.nleg; l++) if (!L.leg[l].front && G[L.leg[l].ankle][11] < 0) lf = l;
        float foot[3]; xform(M, G[contact(&L, D.njoint, G, lf)], foot);
        const float deg = 0.01745329f;
        Scene scenes[] = {
            {"flat", 0, 0, 0, {.kind = G_FLAT}},
            {"slope 15 up", 0, 0.4f * 15 * deg, 0, {.kind = G_SLOPE, .slope = 15 * deg, .dir = 0}},
            {"slope 30 up", 0, 0.4f * 30 * deg, 0, {.kind = G_SLOPE, .slope = 30 * deg, .dir = 0}},
            {"slope 45 up", 0, 0.4f * 45 * deg, 0, {.kind = G_SLOPE, .slope = 45 * deg, .dir = 0}},
            {"slope 15 down", 0, -0.4f * 15 * deg, 0, {.kind = G_SLOPE, .slope = 15 * deg, .dir = 3.14159265f}},
            {"slope 30 across", 0, 0, 0.15f * 30 * deg, {.kind = G_SLOPE, .slope = 30 * deg, .dir = 1.5707963f}},
            {"slope 45 across", 0, 0, 0.15f * 45 * deg, {.kind = G_SLOPE, .slope = 45 * deg, .dir = 1.5707963f}},
            {"slope 30 diagonal", 0.7f, 0.3f * 30 * deg, 0.1f * 30 * deg, {.kind = G_SLOPE, .slope = 30 * deg, .dir = 0.3f}},
            {"step 15 cm", 0, 0, 0, {.kind = G_STEP, .step_x = foot[0] + 0.05f, .step_h = 0.15f}},
            {"rock 25 cm", 0, 0, 0, {.kind = G_ROCK, .rock_x = foot[0], .rock_z = foot[2], .rock_r = 0.15f, .rock_h = 0.25f}},
            {"rock 70 cm (too tall)", 0, 0, 0, {.kind = G_ROCK, .rock_x = foot[0], .rock_z = foot[2], .rock_r = 0.15f, .rock_h = 0.7f}},
        };
        for (size_t s = 0; s < sizeof scenes / sizeof scenes[0]; s++) {
            run_scene(model, &D, &L, &scenes[s], 1.0f);
            if (s == 2) run_scene(model, &D, &L, &scenes[s], 0.5f);   // a juvenile on 30 degrees
        }
        run_edges(model, &D, &L);
        df_free(&D);
    }
    printf("  walking, the smoothed pelvis lagged at most %.2f hip heights below the fold limit (fold m < 0 above)\n", g_lag);
    return t_finish("test_dino_ik", t0);
}
