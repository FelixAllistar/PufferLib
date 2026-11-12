#version 330
#include "common.glsl"
in vec2 v_uv; out vec4 o;
uniform sampler2D t_src, t_clouds, t_bloom, t_depth, t_adapt;
uniform int u_has_clouds, u_frame;
uniform int u_upscale;   // 1 when the scene is rendered below the window size (render scale < 1)
uniform float u_exposure;
uniform float u_rain, u_time;   // rain 0..1 (view.c weather)
uniform mat4 u_inv_viewproj;
uniform vec3 u_cam;
// Rain: four layers of streaks at 1.5..14 m, parameterised by view
// direction (azimuth, elevation), so they stay put in the world as the camera
// turns, and falling with a little wind slant. A layer only shows in front of
// the scene depth. Returns streak coverage.
float hash11(float n) { return fract(sin(n) * 43758.5453); }
float rain_streaks(vec3 dir, float scene_m) {
    float az = atan(dir.z, dir.x), el = asin(clamp(dir.y, -1.0, 1.0));
    float acc = 0.0;
    for (int i = 0; i < 4; i++) {
        float d = 1.5 * pow(2.0, float(i));
        if (d > scene_m) break;
        // At distance d: one column per 8 cm, one row per metre of fall;
        // a drop is a ~15 cm motion-blurred streak, 1 px wide.
        vec2 q = vec2(az * d / 0.08, el * d);
        q.y += u_time * 9.0;          // 9 m/s fall
        q.x += q.y * 1.4;             // wind slant (1.3 m/s)
        float col = floor(q.x), h = hash11(col * 1.37 + float(i) * 17.1);
        float row = floor(q.y + h * 7.0), y = fract(q.y + h * 7.0);
        float on = step(mix(0.45, 0.8, float(i) / 3.0), hash11(col * 3.1 + row * 1.7 + float(i) * 5.3));
        float fx = abs(fract(q.x) - 0.5 - (h - 0.5) * 0.6), wpx = fwidth(q.x);
        float line = 1.0 - smoothstep(0.0, wpx * 1.2, fx);
        float seg = smoothstep(0.0, 0.03, y) * smoothstep(0.15 + 0.06 * h, 0.1, y);
        acc += line * seg * on * mix(1.0, 0.45, float(i) / 3.0);
    }
    return clamp(acc, 0.0, 1.0);
}
uniform vec3 u_sun;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
// Clouds: the cloud buffer is a full-res sky image (cloud_resolve.fs); a
// surface in front of the whole layer shows none of it (common.glsl).
uniform float u_far;
vec4 clouds_at(vec2 uv) {
    ivec2 ds = textureSize(t_depth, 0);
    float d0 = texelFetch(t_depth, clamp(ivec2(uv * vec2(ds)), ivec2(0), ds - 1), 0).r;
    if (d0 > 0.0) {
        vec4 pf = u_inv_viewproj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
        vec3 dir = normalize(pf.xyz / pf.w - u_cam);
        vec4 cf = u_inv_viewproj * vec4(0.0, 0.0, 1.0, 1.0);
        vec3 fwd = normalize(cf.xyz / cf.w - u_cam);
        if (cloud_blocked(d0, u_far, u_cam, dir, fwd)) return vec4(0.0, 0.0, 0.0, 1.0);
    }
    return texture(t_clouds, uv);
}
// The scene at a render scale below 1, resampled to the window. A bilinear
// lookup (as before) averages 2x2 texels and blurs the frame on top of the
// lower resolution; Catmull-Rom keeps edges and texture detail, in nine
// bilinear taps. Its negative lobes would ring around bright edges in HDR
// (the sun on a trunk, a lit leaf on sky), so the result is clamped to the
// four nearest texels: no halos and nothing new to shimmer.
vec3 scene_upscaled(vec2 uv) {
    vec2 ts = vec2(textureSize(t_src, 0)), sp = uv * ts;
    vec2 t1 = floor(sp - 0.5) + 0.5, f = sp - t1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f)), w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2, t0 = (t1 - 1.0) / ts, t3 = (t1 + 2.0) / ts, t12 = (t1 + w2 / w12) / ts;
    vec3 c = (textureLod(t_src, vec2(t0.x, t0.y), 0.0).rgb * w0.x + textureLod(t_src, vec2(t12.x, t0.y), 0.0).rgb * w12.x + textureLod(t_src, vec2(t3.x, t0.y), 0.0).rgb * w3.x) * w0.y
           + (textureLod(t_src, vec2(t0.x, t12.y), 0.0).rgb * w0.x + textureLod(t_src, vec2(t12.x, t12.y), 0.0).rgb * w12.x + textureLod(t_src, vec2(t3.x, t12.y), 0.0).rgb * w3.x) * w12.y
           + (textureLod(t_src, vec2(t0.x, t3.y), 0.0).rgb * w0.x + textureLod(t_src, vec2(t12.x, t3.y), 0.0).rgb * w12.x + textureLod(t_src, vec2(t3.x, t3.y), 0.0).rgb * w3.x) * w3.y;
    ivec2 i = ivec2(t1 - 0.5), mx = ivec2(ts) - 1;
    vec3 a = texelFetch(t_src, clamp(i, ivec2(0), mx), 0).rgb, b = texelFetch(t_src, clamp(i + ivec2(1, 0), ivec2(0), mx), 0).rgb;
    vec3 d = texelFetch(t_src, clamp(i + ivec2(0, 1), ivec2(0), mx), 0).rgb, e = texelFetch(t_src, clamp(i + ivec2(1, 1), ivec2(0), mx), 0).rgb;
    return clamp(c, min(min(a, b), min(d, e)), max(max(a, b), max(d, e)));
}
void main() {
    vec3 c = u_upscale != 0 ? scene_upscaled(v_uv) : texture(t_src, v_uv).rgb;
    if (u_has_clouds != 0) { vec4 cl = clouds_at(v_uv); c = c * cl.a + cl.rgb; }
    c += texture(t_bloom, v_uv).rgb * 0.06;
    if (u_rain > 0.0) {
        vec4 pf = u_inv_viewproj * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
        vec3 dir = normalize(pf.xyz / pf.w - u_cam);
        float dr = texelFetch(t_depth, ivec2(v_uv * vec2(textureSize(t_depth, 0))), 0).r;
        float scene_m = dr <= 0.0 ? 1e6 : linear_depth(dr, u_far);
        float avg_l = exp2(texture(t_adapt, vec2(0.5)).r);
        // Streaks catch the grey sky light: a little brighter than the scene average.
        c = mix(c, vec3(avg_l) * vec3(0.9, 0.95, 1.0) * 1.6, rain_streaks(dir, scene_m) * 0.35 * u_rain);
    }
    // Exposure follows the sun so dawn, dusk and night stay readable.
    float day = smoothstep(-0.15, 0.35, u_sun.y);
    float ev_fixed = mix(1.5, -0.6, day);
    // Eye adaptation: aim the adapted average at mid-grey, but only partly
    // and within +-2.5 stops of the sun-based exposure, so night stays dark
    // and a shaded cliff or forest floor reads without blowing out the sky.
    // Brightening (shaded forest floor, under canopy) adapts further than
    // darkening, as the eye does: a sunlit view or a sunset keeps the 60%
    // response, so its sky does not wash out.
    float avg = exp2(texture(t_adapt, vec2(0.5)).r);
    float ev_auto = clamp(log2(0.18 / (0.28 * avg)), ev_fixed - 2.5, ev_fixed + 3.0);
    float k = ev_auto > ev_fixed ? mix(0.6, 0.85, smoothstep(0.0, 1.5, ev_auto - ev_fixed)) : 0.6;
    float ev = mix(ev_fixed, ev_auto, k) + u_exposure;
    // Day for night: the moonlit frame is lit like day (with a cool moon and
    // a stronger sky light, common.glsl), so grade it down a stop and a half
    // and shift it toward the eye's rod vision (Purkinje): darker tones lose
    // more colour and turn blue, while moonlit highlights keep a little.
    ev -= 1.8 * u_night;
    c *= 0.28 * exp2(ev);
    float scot = dot(c, vec3(0.1, 0.6, 0.3));
    float rods = u_night * mix(0.85, 0.45, smoothstep(0.01, 0.25, scot));
    c = mix(c, scot * vec3(0.55, 0.78, 1.18), rods);
    // ACES per channel bleaches bright saturated colour (sunset clouds, lit
    // foliage) toward cream. Blend in the curve applied to the brightest
    // channel, which keeps hue and saturation; the brightest highlights
    // still roll off toward white.
    vec3 c0 = c;
    float m = max(c.r, max(c.g, c.b));
    vec3 hc = c * (aces(vec3(m)).r / max(m, 1e-5));
    hc = mix(hc, vec3(aces(vec3(m)).r), smoothstep(2.0, 12.0, m) * 0.6);
    c = mix(aces(c), hc, 0.4);
    // At night a softer toe: ACES crushes the bottom stops, where a moonlit
    // scene lives; this curve keeps them (slope 1 at black) and still rolls
    // the moon and the clouds' silver linings off to white.
    c = mix(c, 1.0 - exp(-c0 * 1.15), u_night);
    float vig = 1.0 - mix(0.18, 0.3, u_night) * dot(v_uv - 0.5, v_uv - 0.5) * 2.5;
    c *= vig;
    c = pow(c, vec3(1.0 / 2.2));
    c += (ign(gl_FragCoord.xy, u_frame) - 0.5) / 255.0;
    o = vec4(c, 1.0);
}
