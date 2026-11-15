#version 330
#include "common.glsl"
// Temporal accumulation of the sparse cloud march into a full-resolution
// sky image. Each frame clouds.fs marches one pixel of every 4x4 block
// (offset u_cb, 16-frame Bayer order) into t_cur, a quarter-size target;
// this pass writes every full-res pixel:
//   - history, reprojected by view direction only (clouds are far, so a
//     turn is exact; a still camera reads its own texel back unblurred);
//   - the marched pixels blend their fresh sample in;
//   - without usable history (first frames, screen edges that just came
//     into view, a teleport) the pixel takes the bilinear estimate of the
//     four nearest marched samples and sharpens over the next cycles.
// While the view moves, history is clamped to the padded range of the
// current 3x3 marched neighbourhood against ghosting; at rest it is left
// alone, so every pixel converges to its own march.
// Marched samples with alpha < 0 are pixels whose surface hides the whole
// layer (clouds.fs); they carry no sample, and the sky image behind them is
// kept from history so nothing is missing when the camera turns past them.
in vec2 v_uv; out vec4 o;
uniform sampler2D t_cur, t_hist, t_depth;
uniform vec3 u_cam;
uniform vec2 u_screen;   // full-res size
uniform float u_far;
uniform int u_hist_valid;
uniform ivec2 u_cb;
// Catmull-Rom history fetch in five bilinear taps (Jimenez 2016): sharper
// than bilinear under motion, no ringing worth clamping away.
vec4 hist_catmull(vec2 uv) {
    vec2 size = u_screen, pos = uv * size, c = floor(pos - 0.5) + 0.5, f = pos - c;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f)), w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2, off12 = w2 / w12;
    vec2 t0 = (c - 1.0) / size, t3 = (c + 2.0) / size, t12 = (c + off12) / size;
    vec4 r = texture(t_hist, vec2(t12.x, t12.y)) * w12.x * w12.y;
    r += texture(t_hist, vec2(t0.x, t12.y)) * w0.x * w12.y + texture(t_hist, vec2(t3.x, t12.y)) * w3.x * w12.y;
    r += texture(t_hist, vec2(t12.x, t0.y)) * w12.x * w0.y + texture(t_hist, vec2(t12.x, t3.y)) * w12.x * w3.y;
    float wsum = w12.x * w12.y + w0.x * w12.y + w3.x * w12.y + w12.x * w0.y + w12.x * w3.y;
    return r / wsum;
}
// History at this pixel's previous position. Below half a pixel of motion
// the view is at rest up to float error (the 60 km inverse projection is not
// exact), and the texel is read back as is: any resampling kernel applied to
// the same sub-pixel offset frame after frame, with only a 1-in-16 refresh,
// drifts (Catmull-Rom's gain above one at some frequencies rang the history
// into pixel-scale stripes within a second).
vec4 history(vec2 puv, float vel) {
    if (vel < 0.5) return texelFetch(t_hist, clamp(ivec2(puv * u_screen), ivec2(0), ivec2(u_screen) - 1), 0);
    return hist_catmull(puv);
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(p) + 0.5) / u_screen;
    // Where this pixel's direction was last frame (camera basis, common.glsl:
    // exact at rest, so a still view reads its own texel back).
    vec3 dir = view_ray(uv * 2.0 - 1.0);
    vec2 puv; bool hist_ok = prev_uv(dir, puv) && u_hist_valid != 0 && all(greaterThanEqual(puv, vec2(0.0))) && all(lessThanEqual(puv, vec2(1.0)));
    float vel = length((puv - uv) * u_screen);   // full-res pixels moved
    if (u_cb.x < 0) {
        // Every pixel marched this frame: plain exponential accumulation.
        vec4 c = texelFetch(t_cur, p, 0);
        if (c.a < 0.0) c = hist_ok ? history(puv, vel) : vec4(0.0, 0.0, 0.0, 1.0);
        else if (hist_ok) c = mix(history(puv, vel), c, 0.15);
        o = c; return;
    }
    ivec2 qs = textureSize(t_cur, 0);
    bool mine = (p & 3) == u_cb;
    vec4 cur = vec4(0.0, 0.0, 0.0, -1.0);
    if (mine) {
        cur = texelFetch(t_cur, p >> 2, 0);
        // Firefly limit: a single jittered sample on a backlit rim can be many
        // times brighter than its four marched neighbours; keep it near them.
        // Only brightness is limited, and only by how bright cloud of the
        // same opacity is next door (radiance per unit of opacity). Clamping
        // the whole sample to the neighbours' range, as this did, removed
        // every cloud pixel whose four neighbours (4 pixels away) were sky,
        // which cut cloud edges into 4-pixel steps.
        float rad = 0.0; int nn = 0;
        for (int k = 0; k < 4; k++) {
            ivec2 d = ivec2((k & 1) * 2 - 1, 0); if (k >= 2) d = d.yx;
            vec4 s = texelFetch(t_cur, clamp((p >> 2) + d, ivec2(0), qs - 1), 0); if (s.a < 0.0 || s.a > 0.97) continue;
            rad = max(rad, max(s.r, max(s.g, s.b)) / (1.0 - s.a)); nn++;
        }
        if (cur.a >= 0.0 && nn >= 2) {
            float lim = (1.0 - cur.a) * rad * 2.5 + 0.05, m = max(cur.r, max(cur.g, cur.b));
            if (m > lim) cur.rgb *= lim / m;
        }
    }
    if (hist_ok && cur.a < 0.0 && vel < 0.5) { o = history(puv, vel); return; }   // the common case at rest: one fetch
    // Marched samples sit at full-res 4q + u_cb. Bilinear estimate from the
    // four around this pixel: the value without history, the clamp box with.
    vec2 f = vec2(p - u_cb) * 0.25; ivec2 q0 = ivec2(floor(f)); vec2 fr = f - vec2(q0);
    vec4 acc = vec4(0.0), mn = vec4(1e9), mx = vec4(-1e9); float wsum = 0.0; int nn = 0;
    for (int k = 0; k < 4; k++) {
        ivec2 d = ivec2(k & 1, k >> 1), q = clamp(q0 + d, ivec2(0), qs - 1);
        vec4 s = texelFetch(t_cur, q, 0); if (s.a < 0.0) continue;
        float bw = (d.x == 1 ? fr.x : 1.0 - fr.x) * (d.y == 1 ? fr.y : 1.0 - fr.y) + 0.02;
        acc += s * bw; wsum += bw; mn = min(mn, s); mx = max(mx, s); nn++;
    }
    vec4 est = wsum > 0.0 ? acc / wsum : vec4(0.0, 0.0, 0.0, 1.0);
    if (!hist_ok) {
        // No history (the first frames, a teleport, the screen edge a turn
        // brings in): each marched sample is one jittered ray, which at a
        // cloud's edge hits or misses, so the four nearest alone draw the
        // edge in 4-pixel steps. A wider tent over the 4x4 samples around
        // gives a soft cloud instead, and the pixel's own marches sharpen it
        // over the next cycles.
        vec4 a2 = vec4(0.0); float w2 = 0.0;
        for (int j = -1; j <= 2; j++) for (int i = -1; i <= 2; i++) {
            vec4 s = texelFetch(t_cur, clamp(q0 + ivec2(i, j), ivec2(0), qs - 1), 0); if (s.a < 0.0) continue;
            vec2 dq = vec2(float(i), float(j)) - fr; float tw = max(0.0, 1.0 - 0.5 * abs(dq.x)) * max(0.0, 1.0 - 0.5 * abs(dq.y));
            a2 += s * tw; w2 += tw;
        }
        o = w2 > 0.0 ? a2 / w2 : est; return;
    }
    vec4 h = history(puv, vel);
    if (vel >= 0.5 && nn >= 2) {
        // Under motion, history is kept unless it is far outside what the
        // marched neighbours show (a cloud that was not there). A turn of the
        // camera reprojects the sky exactly, so the converged image is still
        // right; clamping it to the range of four single-ray samples, as this
        // did, replaced it with their noise whenever the player looked round,
        // and cloud edges broke into 4-pixel blocks.
        vec4 pad = (mx - mn) * 1.5 + 0.25;
        h = clamp(h, mn - pad, mx + pad);
    }
    // Fresh sample: blend it in hard enough that a reset converges within a
    // few 16-frame cycles, soft enough that the jitter averages out.
    o = cur.a >= 0.0 ? mix(h, cur, 0.4) : h;
}
