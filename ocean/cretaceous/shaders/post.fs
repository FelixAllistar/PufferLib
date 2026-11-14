#version 330
#include "common.glsl"
in vec2 v_uv; out vec4 o;
uniform sampler2D t_src, t_clouds, t_bloom, t_depth, t_adapt;
uniform int u_has_clouds, u_frame;
uniform int u_upscale;   // 1 when the scene is rendered below the window size (render scale < 1), except on the low preset
uniform float u_sharpen; // strength of the full-resolution sharpen (high and ultra at render scale 1; 0 off)
uniform float u_exposure;
uniform float u_mist;    // 0..1: forest mist round the camera (view.c)
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
uniform float u_ao;      // 0..1: screen-space ambient occlusion strength (view.c, by preset; 0 off)
uniform float u_grade;   // 0..1: the colour grade (warm light, cool shade, a little richer colour)
// Ambient occlusion from the depth buffer (in the spirit of Alchemy/SAO): points of the surface round a
// pixel that rise above its tangent plane, within a world radius of half a metre (near) to a few metres
// (far), darken it. Grounds animals, rocks, trunks and plants in contact shade, and deepens creases,
// which the per-object baked terms cannot do between objects.
vec3 ao_dir(vec2 uv) { vec4 pf = u_inv_viewproj * vec4(uv * 2.0 - 1.0, 1.0, 1.0); return normalize(pf.xyz / pf.w - u_cam); }
vec3 ao_pos(vec2 uv, vec3 fwd, out float raw) {
    ivec2 ds = textureSize(t_depth, 0);
    raw = texelFetch(t_depth, clamp(ivec2(uv * vec2(ds)), ivec2(0), ds - 1), 0).r;
    vec3 dir = ao_dir(uv);
    return u_cam + dir * (linear_depth(raw, u_far) / max(dot(dir, fwd), 1e-3));
}
float ssao(vec2 uv) {
    vec4 cf = u_inv_viewproj * vec4(0.0, 0.0, 1.0, 1.0); vec3 fwd = normalize(cf.xyz / cf.w - u_cam);
    float raw; vec3 P = ao_pos(uv, fwd, raw);
    if (raw <= 0.0) return 1.0;
    vec2 px = 1.0 / vec2(textureSize(t_depth, 0)); float r0;
    vec3 xr = ao_pos(uv + vec2(px.x, 0.0), fwd, r0) - P, xl = P - ao_pos(uv - vec2(px.x, 0.0), fwd, r0);
    vec3 yu = ao_pos(uv + vec2(0.0, px.y), fwd, r0) - P, yd = P - ao_pos(uv - vec2(0.0, px.y), fwd, r0);
    vec3 dx = dot(xr, xr) < dot(xl, xl) ? xr : xl, dy = dot(yu, yu) < dot(yd, yd) ? yu : yd;   // the side on this surface
    vec3 N = normalize(cross(dx, dy)); if (dot(N, u_cam - P) < 0.0) N = -N;
    float dist = length(P - u_cam), R = clamp(dist * 0.035, 0.5, 3.0);
    float ang = length(ao_dir(uv + vec2(px.x, 0.0)) - ao_dir(uv));   // radians a pixel spans
    float rpx = min(R / max(dist * ang, 1e-4), 90.0);                 // the radius in pixels, capped (close-ups)
    if (rpx < 1.5) return 1.0;
    float a = 0.0, rot = ign(gl_FragCoord.xy, u_frame) * 6.2832;
    const int NS = 12;
    for (int i = 0; i < NS; i++) {
        float t = (float(i) + 0.5) / float(NS), th = rot + float(i) * 2.39996;
        vec2 o = vec2(cos(th), sin(th)) * sqrt(t) * rpx * px;
        float rs; vec3 v = ao_pos(uv + o, fwd, rs) - P;
        if (rs <= 0.0) continue;
        float vv = dot(v, v);
        a += max(dot(v, N) - 0.01 * dist, 0.0) / (vv + 0.05 * R * R) * (1.0 - smoothstep(R * R, 4.0 * R * R, vv));
    }
    return clamp(1.0 - 1.6 * R * a / float(NS), 0.0, 1.0);
}
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
// A light sharpen at full resolution: the luma of the pixel is pushed away
// from the mean of its four neighbours, more where the neighbourhood is flat
// in relative terms and less across strong edges, and clamped to the range
// of those five texels, so it restores the contrast that bilinear texture
// filtering, mips and the alpha-tested foliage soften without halos around
// bright edges (it works on the HDR scene, so the limits are relative).
vec3 scene_sharpened(vec2 uv) {
    ivec2 ts = textureSize(t_src, 0), i = clamp(ivec2(uv * vec2(ts)), ivec2(0), ts - 1), mx = ts - 1;
    vec3 c = texelFetch(t_src, i, 0).rgb;
    const vec3 W = vec3(0.2126, 0.7152, 0.0722);
    float l = dot(c, W);
    float n = dot(texelFetch(t_src, clamp(i + ivec2(0, 1), ivec2(0), mx), 0).rgb, W), s = dot(texelFetch(t_src, clamp(i - ivec2(0, 1), ivec2(0), mx), 0).rgb, W);
    float e = dot(texelFetch(t_src, clamp(i + ivec2(1, 0), ivec2(0), mx), 0).rgb, W), w = dot(texelFetch(t_src, clamp(i - ivec2(1, 0), ivec2(0), mx), 0).rgb, W);
    float lo = min(l, min(min(n, s), min(e, w))), hi = max(l, max(max(n, s), max(e, w)));
    float k = u_sharpen * (1.0 - smoothstep(0.3, 0.8, (hi - lo) / (hi + 1e-4)));   // relative local contrast
    float ls = clamp(l + k * (l - 0.25 * (n + s + e + w)), lo, hi);
    return c * (ls / max(l, 1e-6));
}
void main() {
    vec3 c = u_upscale != 0 ? scene_upscaled(v_uv) : u_sharpen > 0.0 ? scene_sharpened(v_uv) : texture(t_src, v_uv).rgb;
    if (u_has_clouds != 0) { vec4 cl = clouds_at(v_uv); c = c * cl.a + cl.rgb; }
    if (u_mist > 0.0) {   // mist between the trunks: the scene fades to a pale haze over some tens of metres (the sky a little too)
        float dr = texelFetch(t_depth, ivec2(v_uv * vec2(textureSize(t_depth, 0))), 0).r;
        float sm = dr <= 0.0 ? 120.0 : min(linear_depth(dr, u_far), 120.0);
        float f = (1.0 - exp(-sm * 0.018)) * 0.62 * u_mist;
        float al = exp2(texture(t_adapt, vec2(0.5)).r);
        c = mix(c, vec3(al) * vec3(1.0, 1.05, 1.03) * 7.0, f);   // well above the scene average: mist is lit air
    }
    if (u_ao > 0.0) { float ao = ssao(v_uv); c *= mix(1.0, ao * ao * (3.0 - 2.0 * ao), u_ao * 0.8); }
    c += texture(t_bloom, v_uv).rgb * 0.1;
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
    // A little more contrast by day, on the encoded image: mid shadows go down, lit surfaces up (the daylit
    // scene was flat, every surface a mid value). None at night, where the low end is the whole picture.
    {
        float l = dot(c, vec3(0.3, 0.59, 0.11)), s = l * l * (3.0 - 2.0 * l);
        c *= mix(1.0, s / max(l, 1e-4), 0.3 * (1.0 - u_night));
    }
    // Grade (by day): a little richer colour, sunlit tones a touch warm and shade a touch cool, so light
    // and shadow separate by hue as well as value (it read flat and grey-green).
    {
        float g = u_grade * (1.0 - u_night), l = dot(c, vec3(0.3, 0.59, 0.11));
        c = max(mix(vec3(l), c, 1.0 + 0.14 * g), 0.0);
        // Lift the low mid-tones (shaded forest, the far side of a crown) while black stays black: shaded
        // hillsides of forest read near-black against a bright sky.
        c *= 1.0 + 0.5 * g * smoothstep(0.0, 0.05, l) * (1.0 - smoothstep(0.05, 0.4, l));
        float shade = 1.0 - smoothstep(0.15, 0.65, l);
        c *= mix(vec3(1.0), mix(vec3(1.035, 1.0, 0.94), vec3(0.95, 0.99, 1.05), shade), g);
    }
    c += (ign(gl_FragCoord.xy, u_frame) - 0.5) / 255.0;
    o = vec4(c, 1.0);
}
