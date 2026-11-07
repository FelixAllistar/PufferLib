#version 330
#include "common.glsl"
// Temporal accumulation for the half-resolution cloud march. Clouds are far,
// so history is reprojected by view direction only, then clamped to the
// current neighbourhood to avoid ghosting.
// Checkerboard (u_cb.x >= 0): t_cur is quarter size and holds this frame's
// march of one pixel per 2x2 block (offset u_cb). That pixel blends its new
// sample in; the other three keep clamped history. The estimate and clamp
// box come from the four nearest marched samples, weighted by how well their
// scene depth matches this pixel's, so cloud does not bleed across a
// mountain or tree silhouette.
in vec2 v_uv; out vec4 o;
uniform sampler2D t_cur, t_hist, t_depth;
uniform mat4 u_inv_viewproj, u_prev_viewproj;
uniform vec3 u_cam;
uniform vec2 u_screen;   // half-res size
uniform float u_far;
uniform int u_hist_valid;
uniform ivec2 u_cb;
vec2 g_puv = vec2(0.0);   // where this pixel was last frame (set by reproject)
bool reproject(vec2 uv, out vec4 h) {
    vec4 pf = u_inv_viewproj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(pf.xyz / pf.w - u_cam);
    vec4 pc = u_prev_viewproj * vec4(u_cam + dir * 20000.0, 1.0);
    vec2 puv = pc.xy / pc.w * 0.5 + 0.5; g_puv = puv;
    if (u_hist_valid == 0 || pc.w <= 0.0 || any(lessThan(puv, vec2(0.0))) || any(greaterThan(puv, vec2(1.0)))) return false;
    h = texture(t_hist, puv); return true;
}
void main() {
    vec2 uv = gl_FragCoord.xy / u_screen;
    if (u_cb.x < 0) {
        vec4 c = texelFetch(t_cur, ivec2(gl_FragCoord.xy), 0);
        vec4 h; if (!reproject(uv, h)) { o = c; return; }
        vec4 mn = c, mx = c;
        for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
            vec4 s = texelFetch(t_cur, clamp(ivec2(gl_FragCoord.xy) + ivec2(x, y), ivec2(0), ivec2(u_screen) - 1), 0); mn = min(mn, s); mx = max(mx, s);
        }
        o = mix(clamp(h, mn, mx), c, 0.1);
        return;
    }
    ivec2 hp = ivec2(gl_FragCoord.xy), qs = textureSize(t_cur, 0), ds = textureSize(t_depth, 0);
    float dz = linear_depth(texelFetch(t_depth, min(hp * 2, ds - 1), 0).r, u_far);
    // Marched samples sit at half-res 2q + u_cb; take the 2x2 around this pixel.
    vec2 f = (vec2(hp - u_cb)) * 0.5; ivec2 q0 = ivec2(floor(f)); vec2 fr = f - vec2(q0);
    vec4 acc = vec4(0.0), mn = vec4(1e9), mx = vec4(-1e9); float wsum = 0.0;
    for (int k = 0; k < 4; k++) {
        ivec2 d = ivec2(k & 1, k >> 1), q = clamp(q0 + d, ivec2(0), qs - 1);
        float bw = (d.x == 1 ? fr.x : 1.0 - fr.x) * (d.y == 1 ? fr.y : 1.0 - fr.y) + 0.05;
        float dq = linear_depth(texelFetch(t_depth, min((q * 2 + u_cb) * 2, ds - 1), 0).r, u_far);
        float match = 1.0 / (1.0 + abs(dq - dz) / max(dz, 1.0) * 40.0);
        vec4 s = texelFetch(t_cur, q, 0);
        acc += s * bw * match; wsum += bw * match;
        if (match > 0.5) { mn = min(mn, s); mx = max(mx, s); }
    }
    vec4 est = acc / max(wsum, 1e-6);
    mn = min(mn, est); mx = max(mx, est);
    // Widen the box a little: four sparse samples underestimate the local range.
    vec4 pad = (mx - mn) * 0.15; mn -= pad; mx += pad;
    bool mine = (hp - (hp / 2) * 2) == u_cb;
    vec4 h;
    if (!reproject(uv, h)) { o = mine ? texelFetch(t_cur, hp / 2, 0) : est; return; }
    h = clamp(h, mn, mx);
    // A new sample every fourth frame: blend it in harder than the 1-in-1 path.
    // While the view turns, bilinear history smears and the fresh 1-in-4
    // samples stand out as a dot grid: lean every pixel toward the smooth
    // estimate in proportion to the motion (half-res pixels moved).
    float vel = length((g_puv - uv) * u_screen);
    float a = clamp(vel * 0.06, 0.0, 0.45);
    vec4 cur = texelFetch(t_cur, hp / 2, 0);
    o = mine ? mix(h, mix(cur, est, a), max(0.3, a)) : mix(h, est, a);
}
