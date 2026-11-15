// The dinosaurs' procedural animation layers (render/dino_proc.h): no GL, no
// world, no models. Springs are exact, stable and framerate-independent from
// dt 1/30 to 1/240 (and survive huge rates and steps); the layers settle to
// the same pose at every frame rate, stay within their limits, never produce
// NaNs (even fed NaN, inf or absurd dt) and are bitwise deterministic; the
// tail trails turns and whips (the tip lags the base); breathing quickens as
// stamina is spent; the head stabiliser takes the fast part of a bob out and
// leaves a steady offset alone; look-at respects its limits and lets go of
// targets behind; two-bone IK keeps bone lengths, reaches what it can,
// clamps what it cannot and keeps the knee's side; foot locks hold, release
// and fade; the rig finder picks the spine joints of a synthetic skeleton.
#include "test_util.h"
#include "../render/dino_proc.h"

static const float DTS[] = {1.0f / 30, 1.0f / 60, 1.0f / 120, 1.0f / 240};
#define NDT 4
static bool finite_out(const DinoProcOut* o) {
    const float* f = (const float*)o; size_t n = sizeof *o / sizeof(float);
    for (size_t k = 0; k < n; k++) if (!isfinite(f[k])) return false;
    return true;
}

// ---------------------------------------------------------------- springs
static void test_spring(void) {
    // Step response against the closed form x(t) = T + (x0 - T)(1 + w t) e^{-w t}, at each dt.
    float w = 9.0f, T = 1.0f;
    for (int d = 0; d < NDT; d++) {
        DpSpring s = {0, 0}; float dt = DTS[d], t = 0, worst = 0, maxx = -1; bool mono = true; float prev = 0;
        int steps = (int)lroundf(2.0f / dt);
        for (int k = 0; k < steps; k++) {
            dp_spring(&s, T, w, dt); t += dt;
            float want = T - (1 + w * t) * expf(-w * t);
            worst = fmaxf(worst, fabsf(s.x - want)); maxx = fmaxf(maxx, s.x);
            if (s.x < prev - 1e-6f) mono = false;
            prev = s.x;
        }
        CHECK(worst < 2e-4f, "spring dt 1/%.0f: off the closed form by %.2e", 1 / dt, worst);
        CHECK(maxx <= T + 1e-5f && mono, "spring dt 1/%.0f: overshoots (%.6f) or reverses", 1 / dt, maxx);
        CHECK(fabsf(s.x - T) < 1e-4f, "spring dt 1/%.0f: settled at %.6f", 1 / dt, s.x);
    }
    // A piecewise-constant target (changing at 30 Hz) gives the same path at every frame rate.
    uint32_t rng = 7; float tg[90]; for (int k = 0; k < 90; k++) tg[k] = t_frand(&rng) * 2 - 1;
    float ref[90];
    for (int d = 0; d < NDT; d++) {
        DpSpring s = {0.3f, -2.0f}; int sub = (int)lroundf((1.0f / 30) / DTS[d]); float worst = 0;
        for (int k = 0; k < 90; k++) {
            for (int q = 0; q < sub; q++) dp_spring(&s, tg[k], 12.0f, DTS[d]);
            if (d == 0) ref[k] = s.x; else worst = fmaxf(worst, fabsf(s.x - ref[k]));
        }
        if (d) CHECK(worst < 1e-4f, "spring: dt 1/%.0f strays %.2e from dt 1/30 on the same target path", 1 / DTS[d], worst);
    }
    // Stiff springs and long steps: no blow-up, no overshoot; bad inputs recover.
    DpSpring s = {0, 0};
    for (int k = 0; k < 100; k++) { dp_spring(&s, 5, 1e4f, 0.1f); CHECK_CAPPED(isfinite(s.x) && s.x <= 5 + 1e-4f, 3, "stiff spring: %f", s.x); }
    s.x = NAN; dp_spring(&s, 2, 5, 0.016f); CHECK(isfinite(s.x) && isfinite(s.v), "spring recovers from a NaN state");
    dp_spring(&s, NAN, 5, 0.016f); CHECK(isfinite(s.x), "spring ignores a NaN target");
    float x0 = s.x; dp_spring(&s, 9, 5, 0); dp_spring(&s, 9, 5, -1); dp_spring(&s, 9, 5, NAN);
    CHECK(s.x == x0, "spring: dt <= 0 or NaN leaves it alone");
}

// ---------------------------------------------------------------- body layers
static DinoProcIn base_in(int sp, float dt) {
    const CretSpecies* S = &CRET_SPECIES_TABLE[sp];
    DinoProcIn in = {.dt = dt, .yaw = 0, .speed = 0, .y = 0, .walk = S->walk, .sprint = S->sprint, .slope = 0, .body_pitch = 0,
                     .stamina = 1, .sprinting = false, .active = 1, .head_free = 1, .move = 0, .ntail = DPROC_CHAIN, .seed = 3};
    return in;
}
// Turns at w rad/s at speed v for `secs`, the heading integrated exactly; returns the last output.
static DinoProcOut run_turn(int sp, float dt, float v, float w, float secs, DinoProcState* st) {
    DinoProcParams p; dproc_params(sp, 1, &p); memset(st, 0, sizeof *st);
    DinoProcIn in = base_in(sp, dt); in.speed = v; in.move = 1;
    DinoProcOut o; int n = (int)lroundf(secs / dt);
    for (int k = 0; k < n; k++) { in.yaw = remainderf(w * (k + 1) * dt, 6.2831853f); dproc_step(st, &p, &in, DPROC_ALL & ~DPROC_TAIL, &o); }
    return o;
}
static void test_layers(void) {
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        DinoProcParams p; dproc_params(sp, 1, &p);
        const char* nm = CRET_SPECIES_TABLE[sp].asset;
        float sprint = CRET_SPECIES_TABLE[sp].sprint;
        // A steady turn (0.8 rad/s at the sprint) settles to the same lean and bend at every frame rate.
        DinoProcState st; DinoProcOut ref = run_turn(sp, DTS[0], sprint, 0.8f, 4, &st);
        CHECK(ref.lean < 0 && ref.lean >= -p.lean_max - 1e-5f, "%s: turning +yaw leans %.3f (want -%.3f..0)", nm, ref.lean, p.lean_max);
        float want = fminf(p.lean_k * atanf(sprint * 0.8f / 9.81f), p.lean_max);
        CHECK(fabsf(-ref.lean - want) < 0.01f, "%s: lean %.3f, want %.3f", nm, -ref.lean, want);
        CHECK(ref.bend_front > 0 && ref.bend_rear < 0 && ref.bend_front <= p.bend_max, "%s: spine bend %.3f / %.3f", nm, ref.bend_front, ref.bend_rear);
        for (int d = 1; d < NDT; d++) {
            DinoProcOut o = run_turn(sp, DTS[d], sprint, 0.8f, 4, &st);
            CHECK(fabsf(o.lean - ref.lean) < 0.005f && fabsf(o.bend_front - ref.bend_front) < 0.005f,
                  "%s: dt 1/%.0f lean %.4f bend %.4f vs dt 1/30 %.4f %.4f", nm, 1 / DTS[d], o.lean, o.bend_front, ref.lean, ref.bend_front);
        }
        // The tail: a step into a turn; the base trails out of it first, the tip later; settled within the limit;
        // the same at every frame rate (tail sway off: sway amplitude rides on the time base only, checked below).
        DinoProcOut tail_ref = {0}; float tip_early = 0, base_early = 0;
        for (int d = 0; d < NDT; d++) {
            float dt = DTS[d]; DinoProcIn in = base_in(sp, dt); memset(&st, 0, sizeof st); in.speed = sprint * 0.6f; in.move = 1;
            DinoProcParams q = p; q.sway_amp = 0; DinoProcOut o; int n = (int)lroundf(3.0f / dt);
            for (int k = 0; k < n; k++) {
                float t = (k + 1) * dt, w = t > 0.5f ? 1.0f : 0.0f;
                in.yaw = remainderf(w * (t - 0.5f), 6.2831853f); dproc_step(&st, &q, &in, DPROC_ALL, &o);
                if (d == 0 && fabsf(t - 0.6f) < dt * 0.5f) { base_early = o.tail_yaw[0]; tip_early = o.tail_yaw[DPROC_CHAIN - 1]; }
                bool lim = true; for (int c = 0; c < DPROC_CHAIN; c++) lim = lim && fabsf(o.tail_yaw[c]) <= p.tail_yaw_max + 1e-5f && fabsf(o.tail_pitch[c]) <= p.tail_pitch_max + 1e-5f;
                CHECK_CAPPED(lim && finite_out(&o), 4, "%s: tail out of its limits or not finite", nm);
            }
            if (d == 0) { tail_ref = o; CHECK(o.tail_yaw[0] > 0, "%s: tail trails a +yaw turn (%.3f)", nm, o.tail_yaw[0]); }
            else CHECK(fabsf(o.tail_yaw[0] - tail_ref.tail_yaw[0]) < 0.005f && fabsf(o.tail_yaw[DPROC_CHAIN - 1] - tail_ref.tail_yaw[DPROC_CHAIN - 1]) < 0.005f,
                       "%s: tail at dt 1/%.0f %.4f..%.4f vs %.4f..%.4f", nm, 1 / dt, o.tail_yaw[0], o.tail_yaw[DPROC_CHAIN - 1], tail_ref.tail_yaw[0], tail_ref.tail_yaw[DPROC_CHAIN - 1]);
        }
        CHECK(base_early > tip_early + 1e-4f && tip_early >= 0, "%s: 0.1 s into the turn the tail base %.4f leads the tip %.4f", nm, base_early, tip_early);
        // Braking hard lifts the tail, a landing (an upward kick of the height) drops it.
        {
            DinoProcIn in = base_in(sp, DTS[1]); memset(&st, 0, sizeof st); DinoProcParams q = p; q.sway_amp = 0; DinoProcOut o;
            in.speed = sprint; in.move = 1; for (int k = 0; k < 120; k++) dproc_step(&st, &q, &in, DPROC_ALL, &o);
            in.speed = 0; float lift = -1; for (int k = 0; k < 20; k++) { dproc_step(&st, &q, &in, DPROC_ALL, &o); lift = fmaxf(lift, o.tail_pitch[0]); }
            CHECK(q.tail_acc <= 0 || lift > 0, "%s: braking lifts the tail (%.4f)", nm, lift);
        }
        // Slope: the chest takes chest_k of what the body does not, within chest_max.
        {
            DinoProcIn in = base_in(sp, DTS[2]); memset(&st, 0, sizeof st); in.slope = 0.4f; in.body_pitch = 0.1f; DinoProcOut o;
            for (int k = 0; k < 600; k++) dproc_step(&st, &p, &in, DPROC_ALL, &o);
            float wantc = fminf(0.3f * p.chest_k, p.chest_max);
            CHECK(fabsf(o.chest_pitch - wantc) < 1e-3f, "%s: chest pitch %.4f on a 0.4 slope (body 0.1), want %.4f", nm, o.chest_pitch, wantc);
        }
        // Breathing: deeper and 1 + 1.6 x 1.6 = 3.6 times as fast with no stamina left and sprinting.
        for (int tired = 0; tired < 2; tired++) {
            DinoProcIn in = base_in(sp, DTS[1]); memset(&st, 0, sizeof st); in.stamina = tired ? 0 : 1; in.sprinting = tired;
            DinoProcOut o; int peaks = 0; float prev = 0, prev2 = 0, hi = 0, lo = 1;
            for (int k = 0; k < 60 * 60; k++) {
                dproc_step(&st, &p, &in, DPROC_ALL, &o);
                if (k > 600) { if (prev > prev2 && prev >= o.breath) peaks++; hi = fmaxf(hi, o.breath); lo = fminf(lo, o.breath); }
                prev2 = prev; prev = o.breath;
            }
            float rate = peaks / 50.0f, want_r = p.breath_hz * (tired ? 1 + 1.6f * 1.6f : 1);
            CHECK(fabsf(rate - want_r) < 0.06f * want_r + 0.03f, "%s: %s breaths %.3f /s, want %.3f", nm, tired ? "tired" : "rested", rate, want_r);
            CHECK(lo >= 0 && hi <= p.breath_amp * (1 + 1.1f * 1.6f) + 1e-4f && hi > 0.9f * p.breath_amp, "%s: breath %.4f..%.4f", nm, lo, hi);
        }
        // Look-at: within the limits, a target behind is let go, a busy head does not look.
        {
            float targets[4][3] = {{0.3f, 0.1f, 1}, {1.6f, 0.9f, 1}, {3.0f, 0.0f, 1}, {0.3f, 0.1f, 0}};
            for (int c = 0; c < 4; c++) {
                DinoProcIn in = base_in(sp, DTS[1]); memset(&st, 0, sizeof st); in.look_on = true; in.look_yaw = targets[c][0]; in.look_pitch = targets[c][1]; in.head_free = targets[c][2];
                DinoProcOut o; for (int k = 0; k < 400; k++) dproc_step(&st, &p, &in, DPROC_ALL, &o);
                float wy = in.head_free && fabsf(in.look_yaw) < 2 * p.look_yaw_max + 0.3f ? fminf(in.look_yaw, p.look_yaw_max) : 0;
                float wp = wy != 0 ? fminf(in.look_pitch, p.look_pitch_max) : 0;
                CHECK(fabsf(o.look_yaw - wy) < 2e-3f && fabsf(o.look_pitch - wp) < 2e-3f, "%s: look at (%.2f, %.2f) free %.0f: %.3f %.3f, want %.3f %.3f",
                      nm, in.look_yaw, in.look_pitch, in.head_free, o.look_yaw, o.look_pitch, wy, wp);
            }
        }
        // Each layer's bit turns it off; inactive (dead) bodies settle flat.
        {
            DinoProcState a; DinoProcOut o; run_turn(sp, DTS[1], sprint, 0.8f, 2, &a);
            DinoProcIn in = base_in(sp, DTS[1]); in.speed = sprint; in.active = 0; in.yaw = a.last_yaw;
            for (int k = 0; k < 600; k++) dproc_step(&a, &p, &in, DPROC_ALL, &o);
            CHECK(fabsf(o.lean) < 1e-3f && fabsf(o.bend_front) < 1e-3f && fabsf(o.tail_yaw[0]) < 1e-3f && o.breath == 0, "%s: an inactive body settles (lean %.4f)", nm, o.lean);
            memset(&a, 0, sizeof a); in = base_in(sp, DTS[1]); in.speed = sprint; in.slope = 0.3f; in.stamina = 0; in.look_on = true; in.look_yaw = 0.3f;
            for (int k = 0; k < 300; k++) { in.yaw = remainderf(0.8f * k * DTS[1], 6.2831853f); dproc_step(&a, &p, &in, 0, &o); }
            bool zero = o.lean == 0 && o.chest_pitch == 0 && o.bend_front == 0 && o.bend_rear == 0 && o.look_yaw == 0 && o.breath == 0;
            for (int c = 0; c < DPROC_CHAIN; c++) zero = zero && fabsf(o.tail_yaw[c]) < 1e-9f && o.tail_pitch[c] == 0;
            CHECK(zero, "%s: every layer off gives a zero output", nm);
        }
        // A juvenile's springs are quicker, its breathing faster.
        DinoProcParams j; dproc_params(sp, 0.35f, &j);
        CHECK(j.tail_omega > p.tail_omega && j.breath_hz > p.breath_hz && j.lean_max == p.lean_max, "%s: juvenile scaling", nm);
    }
}

// Fuzz: random, extreme and non-finite inputs; outputs stay finite and in their limits. Two runs over the
// same input sequence are bitwise equal (determinism).
static void test_fuzz(void) {
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        DinoProcParams p; dproc_params(sp, 1, &p);
        DinoProcState a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
        uint32_t rng = 1234 + sp; bool same = true, fin = true, lim = true;
        for (int k = 0; k < 20000; k++) {
            DinoProcIn in = base_in(sp, DTS[t_rand(&rng) % NDT]);
            int r = t_rand(&rng) % 100;
            in.yaw = (t_frand(&rng) * 2 - 1) * (r < 3 ? 1e6f : 4); in.speed = t_frand(&rng) * 20; in.y = (t_frand(&rng) - 0.5f) * (r < 5 ? 100 : 1);
            in.slope = (t_frand(&rng) - 0.5f) * 2; in.body_pitch = (t_frand(&rng) - 0.5f); in.stamina = t_frand(&rng); in.sprinting = r & 1;
            in.look_on = r & 2; in.look_yaw = (t_frand(&rng) - 0.5f) * 7; in.look_pitch = (t_frand(&rng) - 0.5f) * 3; in.move = t_frand(&rng);
            if (r == 50) in.dt = 5.0f; if (r == 51) in.dt = 0; if (r == 52) in.dt = NAN; if (r == 53) in.yaw = NAN; if (r == 54) in.speed = INFINITY;
            if (r == 55) in.y = NAN; if (r == 56) in.look_yaw = NAN; if (r == 57) in.stamina = NAN; if (r == 58) in.ntail = 99; if (r == 59) in.ntail = -3;
            DinoProcOut oa, ob; dproc_step(&a, &p, &in, DPROC_ALL, &oa); dproc_step(&b, &p, &in, DPROC_ALL, &ob);
            float np, ny, np2, ny2; float el = (t_frand(&rng) - 0.5f) * 2, az = (t_frand(&rng) - 0.5f) * 7;
            if (r == 60) el = NAN;
            dproc_head(&a, &p, el, az, in.body_pitch, 0.1f, t_frand(&rng), in.dt, DPROC_ALL, &np, &ny);
            dproc_head(&b, &p, el, az, in.body_pitch, 0.1f, 0.0f, in.dt, DPROC_ALL, &np2, &ny2);   // another weight: only the output differs
            same = same && !memcmp(&oa, &ob, sizeof oa);
            fin = fin && finite_out(&oa) && isfinite(np) && isfinite(ny);
            lim = lim && fabsf(oa.lean) <= p.lean_max + 1e-5f && fabsf(oa.bend_front) <= p.bend_max && fabsf(np) <= p.stab_max + 1e-6f && fabsf(ny) <= p.stab_max + 1e-6f
                && fabsf(oa.look_yaw) <= p.look_yaw_max + 1e-6f && fabsf(oa.chest_pitch) <= p.chest_max + 1e-5f;
        }
        CHECK(same, "%s: two runs over the same inputs differ", CRET_SPECIES_TABLE[sp].asset);
        CHECK(fin, "%s: a non-finite output under fuzzing", CRET_SPECIES_TABLE[sp].asset);
        CHECK(lim, "%s: an output past its limit under fuzzing", CRET_SPECIES_TABLE[sp].asset);
    }
}

// Head stabilisation: a 2 Hz bob of the head (the gait) shrinks by about stab_k; a steady offset stays.
static void test_head(void) {
    for (int sp = 0; sp < CRET_SPECIES; sp++) {
        DinoProcParams p; dproc_params(sp, 1, &p); p.stab_max = 1;
        for (int d = 0; d < NDT; d++) {
            DinoProcState s; memset(&s, 0, sizeof s); float dt = DTS[d], hi = -1, lo = 1, np = 0, ny = 0;
            int n = (int)lroundf(6.0f / dt);
            for (int k = 0; k < n; k++) {
                float t = (k + 1) * dt, e = 0.2f + 0.1f * sinf(6.2831853f * 2 * t);
                dproc_head(&s, &p, e, 0.05f * sinf(6.2831853f * 1 * t), 0, 0, 1, dt, DPROC_ALL, &np, &ny);
                if (t > 3) { hi = fmaxf(hi, e + np); lo = fminf(lo, e + np); }
            }
            float amp = 0.5f * (hi - lo), want = 0.1f * (1 - p.stab_k);
            CHECK(amp < want + 0.03f && amp > 0.1f * (1 - p.stab_k) * 0.6f, "%s dt 1/%.0f: bob 0.100 -> %.3f with the stabiliser (stab_k %.2f)", CRET_SPECIES_TABLE[sp].asset, 1 / dt, amp, p.stab_k);
            CHECK(fabsf(0.5f * (hi + lo) - 0.2f) < 0.02f, "%s: the head's mean pitch kept (%.3f)", CRET_SPECIES_TABLE[sp].asset, 0.5f * (hi + lo));
        }
        DinoProcState s; memset(&s, 0, sizeof s); float np, ny;
        for (int k = 0; k < 600; k++) dproc_head(&s, &p, 0.4f, 3.12f + (k & 1) * 0.06f - 0.03f, 0.1f, 0, 1, 1.0f / 60, DPROC_ALL, &np, &ny);
        CHECK(fabsf(np) < 1e-3f && fabsf(ny) < 0.04f, "%s: steady head: correction %.4f %.4f (azimuth across +-pi)", CRET_SPECIES_TABLE[sp].asset, np, ny);
        dproc_head(&s, &p, 1.4f, 0, 0, 0, 1, 1.0f / 60, 0, &np, &ny);
        CHECK(np == 0 && ny == 0, "head layer off: no correction");
    }
}

// ---------------------------------------------------------------- legs
static float dist3(const float* a, const float* b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return sqrtf(x * x + y * y + z * z); }
static void test_two_bone(void) {
    uint32_t rng = 99; int bad_len = 0, bad_reach = 0, bad_side = 0, bad_nan = 0, n = 0, reached = 0, clamped_far = 0, clamped_near = 0;
    for (int k = 0; k < 20000; k++) {
        float H[3], K[3], P[3], W[3], K2[3], T[3];
        for (int q = 0; q < 3; q++) { H[q] = (t_frand(&rng) - 0.5f) * 4; K[q] = H[q] + (t_frand(&rng) - 0.5f) * 2; P[q] = K[q] + (t_frand(&rng) - 0.5f) * 2; W[q] = H[q] + (t_frand(&rng) - 0.5f) * 5; }
        if (!dp_two_bone(H, K, P, W, K2, T)) continue;
        n++;
        float a = dist3(H, K), b = dist3(K, P), dw = dist3(H, W), dt_ = dist3(H, T);
        bool fin = true; for (int q = 0; q < 3; q++) fin = fin && isfinite(K2[q]) && isfinite(T[q]);
        if (!fin) { bad_nan++; continue; }
        if (fabsf(dist3(H, K2) - a) > 1e-3f * (1 + a) || fabsf(dist3(K2, T) - b) > 2e-3f * (1 + b)) bad_len++;
        if (dw > fabsf(a - b) + 2e-3f && dw < (a + b) * 0.998f) { reached++; if (dist3(T, W) > 2e-3f * (1 + dw)) bad_reach++; }
        else if (dw >= (a + b) * 0.998f) { clamped_far++; if (dt_ > (a + b) * 0.999f + 1e-3f) bad_reach++; }
        else { clamped_near++; if (dt_ < fabsf(a - b) + 1e-3f - 1e-3f) bad_reach++; }
        // The knee on the animation's side of the hip-target line.
        float u[3] = {T[0] - H[0], T[1] - H[1], T[2] - H[2]}, ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        for (int q = 0; q < 3; q++) u[q] /= ul;
        float hk[3] = {K[0] - H[0], K[1] - H[1], K[2] - H[2]}, hk2[3] = {K2[0] - H[0], K2[1] - H[1], K2[2] - H[2]};
        float d1 = hk[0] * u[0] + hk[1] * u[1] + hk[2] * u[2], d2 = hk2[0] * u[0] + hk2[1] * u[1] + hk2[2] * u[2];
        float v1[3], v2[3]; for (int q = 0; q < 3; q++) { v1[q] = hk[q] - d1 * u[q]; v2[q] = hk2[q] - d2 * u[q]; }
        float l1 = sqrtf(v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2]), l2 = sqrtf(v2[0] * v2[0] + v2[1] * v2[1] + v2[2] * v2[2]);
        if (l1 > 0.05f * a && l2 > 0.02f * a && (v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2]) < 0) bad_side++;
    }
    CHECK(n > 19000 && reached > 1000 && clamped_far > 1000 && clamped_near > 50, "two-bone: %d solves (%d reached, %d too far, %d too near)", n, reached, clamped_far, clamped_near);
    CHECK(!bad_nan && !bad_len && !bad_reach && !bad_side, "two-bone: %d NaN, %d bone lengths changed, %d reach errors, %d knees flipped", bad_nan, bad_len, bad_reach, bad_side);
    // A straight leg straight down bends in the side plane (as foot_ik always has), a degenerate one is refused.
    float H[3] = {0, 1, 0}, K[3] = {0, 0.5f, 0}, P[3] = {0, 0, 0}, W[3] = {0, 0.2f, 0}, K2[3], T[3];
    CHECK(dp_two_bone(H, K, P, W, K2, T) && fabsf(K2[0]) > 0.1f && fabsf(K2[2]) < 1e-5f && fabsf(T[1] - 0.2f) < 1e-4f, "two-bone: straight leg folds (knee %.3f %.3f %.3f)", K2[0], K2[1], K2[2]);
    float Z[3] = {0, 1, 0};
    CHECK(!dp_two_bone(H, H, P, W, K2, T) && !dp_two_bone(H, K, K, W, K2, T) && !dp_two_bone(H, K, P, Z, K2, T), "two-bone: zero-length bones or target refused");
    float Wn[3] = {NAN, 0, 0}; CHECK(!dp_two_bone(H, K, P, Wn, K2, T), "two-bone: NaN target refused");
}

static void test_foot_lock(void) {
    for (int d = 0; d < NDT; d++) {
        DpFootLock L = {0}; float dt = DTS[d], x = 0, ox = 0, oz = 0, held = 0, worst = 0; int n = (int)lroundf(0.5f / dt); bool released = false;
        // The clip's foot slides 0.2 m/s while planted: the lock holds it (within 1 cm) until it strays 0.12 m.
        for (int k = 0; x < 0.2f; k++) {
            x += 0.2f * dt; dp_foot_lock(&L, x, 1, 1, 0.12f, dt, &ox, &oz);
            if (k > 2 && x < 0.11f) held = fmaxf(held, fabsf(x + ox - L.x));
            if (x > 0.05f && !L.on) released = true;
            worst = fmaxf(worst, sqrtf(ox * ox + oz * oz));
        }
        CHECK(held < 0.01f, "foot lock dt 1/%.0f: drift %.4f while planted", 1 / dt, held);
        CHECK(released, "foot lock dt 1/%.0f: released after straying 0.12 m", 1 / dt);
        CHECK(worst <= 0.12f + 1e-5f, "foot lock: offset %.4f within its limit", worst);
        for (int k = 0; k < n; k++) dp_foot_lock(&L, x, 1, 0, 0.12f, dt, &ox, &oz);
        CHECK(fabsf(ox) < 1e-3f && fabsf(oz) < 1e-3f, "foot lock dt 1/%.0f: lifted, the offset fades (%.4f)", 1 / dt, ox);
        dp_foot_lock(&L, NAN, 0, 1, 0.12f, dt, &ox, &oz); CHECK(ox == 0 && oz == 0 && !L.on, "foot lock: NaN position drops the lock");
    }
}

// ---------------------------------------------------------------- rig and flags
static void test_rig(void) {
    // root 0 - pelvis 1 - spine 2 - spine 3 - neck 4 (base) - neck 5 - head 6; pelvis - tail 7 (base) - 8 - 9; pelvis - hip 10 - knee 11 - ankle 12.
    int par[13] = {-1, 0, 1, 2, 3, 4, 5, 1, 7, 8, 1, 10, 11};
    float nw[13] = {0}, tw[13] = {0}; nw[5] = 0.5f; nw[6] = 1; tw[8] = 0.5f; tw[9] = 1;
    DinoProcRig r; dproc_rig(&r, par, 13, nw, tw, 6, 10);
    CHECK(r.neck_base == 4 && r.tail_base == 7 && r.pelvis == 1, "rig: neck base %d, tail base %d, pelvis %d", r.neck_base, r.tail_base, r.pelvis);
    CHECK(r.nfront == 2 && r.front[0] == 2 && r.front[1] == 3 && r.chest == 3, "rig: front spine %d (%d %d), chest %d", r.nfront, r.front[0], r.front[1], r.chest);
    CHECK(r.nrear == 1 && r.rear[0] == 7, "rig: rear spine %d (%d)", r.nrear, r.rear[0]);
    // No tail: the pelvis is the hip's parent; the neck straight off the pelvis bends at its own base.
    int par2[8] = {-1, 0, 1, 2, 3, 1, 5, 6};   // root, pelvis, neck base 2, neck 3, head 4, hip 5, knee, ankle
    float nw2[8] = {0}, tw2[8] = {0}; nw2[3] = 0.5f; nw2[4] = 1;
    dproc_rig(&r, par2, 8, nw2, tw2, 4, 5);
    CHECK(r.pelvis == 1 && r.nfront == 1 && r.front[0] == 2 && r.nrear == 0 && r.tail_base == -1, "rig without a tail: pelvis %d front %d rear %d", r.pelvis, r.nfront, r.nrear);
    dproc_rig(&r, par2, 0, nw2, tw2, 4, 5); CHECK(r.nfront == 0 && r.nrear == 0, "rig: empty skeleton");
    dproc_rig(&r, par2, 8, nw2, tw2, -1, -1); CHECK(r.nfront == 0 && r.chest >= 0, "rig: no head");
}
static void test_flags(void) {
    CHECK(dproc_parse_flags("-tail,-look", DPROC_ALL) == (DPROC_ALL & ~(DPROC_TAIL | DPROC_LOOK)), "flags: -tail,-look");
    CHECK(dproc_parse_flags("none,+feet", DPROC_ALL) == DPROC_FEET && dproc_parse_flags("none", DPROC_ALL) == 0, "flags: none,+feet");
    CHECK(dproc_parse_flags("bogus, -all, lean", DPROC_ALL) == DPROC_LEAN, "flags: unknown words ignored");
    CHECK(dproc_parse_flags(NULL, 5) == 5 && dproc_layer("breath") == 6 && dproc_layer("all") == DPROC_LAYERS && dproc_layer("x") < 0, "flags: names");
    CHECK(cret_dino_proc_flags == DPROC_ALL, "every layer is on by default");
}

int main(void) {
    double t0 = t_now();
    test_spring();
    test_layers();
    test_fuzz();
    test_head();
    test_two_bone();
    test_foot_lock();
    test_rig();
    test_flags();
    return t_finish("test_dino_proc", t0);
}
