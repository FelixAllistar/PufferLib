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
// ---------------------------------------------------------------- the look (render/look.h, Look studio)
// Each knob's default reproduces the image as it was: the new steps are
// skipped at their defaults, and the old constants became uniforms of the
// same value.
uniform float u_lk_adapt;        // eye adaptation strength (1)
uniform float u_lk_night_drop;   // stops the night frame is darkened (1.8)
uniform int u_lk_tonemap;        // tone curve: 0 the game's ACES blend, 1 AgX, 2 Khronos PBR Neutral, 3 Hable, 4 Reinhard
uniform float u_lk_white;        // the exposed level that reaches white (11.2: as before)
uniform float u_lk_contrast;     // the daytime S-curve (0.3)
uniform float u_lk_toe, u_lk_shoulder;   // reshape blacks and highlights (0)
uniform vec3 u_lk_wb;            // white balance gains (1)
uniform int u_lk_lgg;            // 1 when lift / gamma / gain are in use
uniform vec3 u_lk_lift, u_lk_gamma, u_lk_gain;
uniform float u_lk_sat, u_lk_vib;   // saturation (1), vibrance (0)
uniform vec2 u_lk_green;         // greens: hue shift in degrees (0), saturation (1)
uniform float u_lk_bloom;        // bloom added (0.1)
uniform float u_lk_vignette;     // vignette strength (1)
uniform float u_lk_grain;        // film grain (0)
uniform float u_lk_sky, u_lk_clouds;   // brightness of the sky you see and of the clouds (1)
// The other tone curves. Each is scaled so mid grey comes out as with the
// game's curve (the white point at its default), so a switch changes the
// roll-off, not the exposure.
vec3 agx(vec3 x) {   // AgX (Troy Sobotka), the polynomial fit of its default look
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float lo = -12.47393, hi = 4.026069;
    x = (clamp(log2(max(inset * x, 1e-10)), lo, hi) - lo) / (hi - lo);
    vec3 x2 = x * x, x4 = x2 * x2;
    x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
    return pow(max(outset * x, 0.0), vec3(2.2));
}
vec3 pbr_neutral(vec3 c) {   // Khronos PBR Neutral: true colour up to the shoulder
    float x = min(c.r, min(c.g, c.b)), off = x < 0.08 ? x - 6.25 * x * x : 0.04;
    c -= off;
    float peak = max(c.r, max(c.g, c.b));
    if (peak < 0.76) return c;
    float np = 1.0 - 0.0576 / (peak - 0.52);
    c *= np / peak;
    return mix(c, vec3(np), 1.0 - 1.0 / (0.15 * (peak - np) + 1.0));
}
vec3 hable(vec3 x) { return (x * (0.15 * x + 0.05) + 0.004) / (x * (0.15 * x + 0.5) + 0.06) - 0.02 / 0.3; }
vec3 tone_other(vec3 x, int k) {
    if (k == 1) return agx(x * 1.309);
    if (k == 2) return pbr_neutral(x * 1.700);
    if (k == 3) return hable(x * 6.138);
    x *= 1.901; return x / (1.0 + x);
}
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0), p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}
vec3 hsv2rgb(vec3 c) { vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0); return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y); }
// Clouds: the cloud buffer is a full-res sky image (cloud_resolve.fs); a
// surface in front of the whole layer shows none of it (common.glsl).
uniform float u_far;
uniform float u_ao;      // 0..1: screen-space ambient occlusion strength (view.c, by preset; 0 off)
uniform float u_grade;   // 0..1: the colour grade (warm light, cool shade, a little richer colour)
uniform sampler2D t_ao;   // ambient occlusion at the render resolution (ao.fs, smoothed by ao_blur.fs)
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
    // At rest each sky pixel holds an average of only a few jittered marches (cloud_resolve.fs), so cloud
    // edges kept single bright and dark pixels at full resolution. For display only (the history stays
    // sharp): the pixel is clamped to the range of its eight neighbours, which removes lone outliers, and
    // softened a little by its neighbours. Clouds hold no detail at the scale of one pixel.
    ivec2 cs = textureSize(t_clouds, 0), cp = clamp(ivec2(uv * vec2(cs)), ivec2(0), cs - 1);
    vec4 c0 = texelFetch(t_clouds, cp, 0), mn = vec4(1e9), mx = vec4(-1e9), sum = vec4(0.0);
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
        if (i == 0 && j == 0) continue;
        vec4 s = texelFetch(t_clouds, clamp(cp + ivec2(i, j), ivec2(0), cs - 1), 0);
        mn = min(mn, s); mx = max(mx, s); sum += s * ((i == 0 || j == 0) ? 1.0 : 0.5);
    }
    c0 = clamp(c0, mn, mx);
    return (c0 * 6.0 + sum) / 12.0;   // half the weight on the pixel itself: a light soften
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
    if (u_lk_sky != 1.0 && texelFetch(t_depth, ivec2(v_uv * vec2(textureSize(t_depth, 0))), 0).r <= 0.0) c *= u_lk_sky;   // the dome only
    if (u_has_clouds != 0) { vec4 cl = clouds_at(v_uv); c = c * cl.a + cl.rgb * u_lk_clouds; }
    if (u_mist > 0.0) {   // mist between the trunks: the scene fades to a pale haze over some tens of metres (the sky a little too)
        float dr = texelFetch(t_depth, ivec2(v_uv * vec2(textureSize(t_depth, 0))), 0).r;
        float sm = dr <= 0.0 ? 25.0 : min(linear_depth(dr, u_far), 55.0);   // capped: looking out of the wood, sky and far hills kept only half their colour
        float f = min((1.0 - exp(-sm * 0.018)) * 0.62 * u_mist, 1.0);
        float al = exp2(texture(t_adapt, vec2(0.5)).r);
        c = mix(c, vec3(al) * vec3(1.0, 1.05, 1.03) * 7.0, f);   // well above the scene average: mist is lit air
    }
    if (u_ao > 0.0) { float ao = texture(t_ao, v_uv).r; c *= max(mix(1.0, ao * ao * (3.0 - 2.0 * ao), u_ao * 0.8), 0.0); }
    c += texture(t_bloom, v_uv).rgb * u_lk_bloom;
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
    float ev = mix(ev_fixed, ev_auto, k * u_lk_adapt) + u_exposure;
    // Day for night: the moonlit frame is lit like day (with a cool moon and
    // a stronger sky light, common.glsl), so grade it down a stop and a half
    // and shift it toward the eye's rod vision (Purkinje): darker tones lose
    // more colour and turn blue, while moonlit highlights keep a little.
    ev -= u_lk_night_drop * u_night;
    c *= 0.28 * exp2(ev) * u_lk_wb;
    float scot = dot(c, vec3(0.1, 0.6, 0.3));
    float rods = u_night * mix(0.85, 0.45, smoothstep(0.01, 0.25, scot));
    c = mix(c, scot * vec3(0.55, 0.78, 1.18), rods);
    // ACES per channel bleaches bright saturated colour (sunset clouds, lit
    // foliage) toward cream. Blend in the curve applied to the brightest
    // channel, which keeps hue and saturation; the brightest highlights
    // still roll off toward white.
    vec3 c0 = c;
    if (u_lk_tonemap == 0) {
        float m = max(c.r, max(c.g, c.b));
        vec3 hc = c * (aces(vec3(m)).r / max(m, 1e-5));
        hc = mix(hc, vec3(aces(vec3(m)).r), smoothstep(2.0, 12.0, m) * 0.6);
        // White point: a grey at u_lk_white maps to white (at 11.2 the curve already reached it: no change).
        c = min(mix(aces(c), hc, 0.4) / aces(vec3(u_lk_white)).r, 1.0);
    } else {
        c = clamp(tone_other(c, u_lk_tonemap) / tone_other(vec3(u_lk_white), u_lk_tonemap).g, 0.0, 1.0);
    }
    // At night a softer toe: ACES crushes the bottom stops, where a moonlit
    // scene lives; this curve keeps them (slope 1 at black) and still rolls
    // the moon and the clouds' silver linings off to white.
    c = mix(c, 1.0 - exp(-c0 * 1.15), u_night);
    // Toe and shoulder: bend the curve near black and near white (both 0: unchanged).
    if (u_lk_toe != 0.0 || u_lk_shoulder != 0.0) {
        c = clamp(c, 0.0, 1.0);
        c = pow(c, 1.0 + 0.6 * u_lk_toe * (1.0 - c) * (1.0 - c));
        c = 1.0 - pow(1.0 - c, 1.0 + 0.6 * u_lk_shoulder * c * c);
    }
    float vig = 1.0 - mix(0.18, 0.3, u_night) * u_lk_vignette * dot(v_uv - 0.5, v_uv - 0.5) * 2.5;
    c *= vig;
    c = pow(c, vec3(1.0 / 2.2));
    // A little more contrast by day, on the encoded image: mid shadows go down, lit surfaces up (the daylit
    // scene was flat, every surface a mid value). None at night, where the low end is the whole picture.
    {
        float l = dot(c, vec3(0.3, 0.59, 0.11)), s = l * l * (3.0 - 2.0 * l);
        c *= mix(1.0, s / max(l, 1e-4), u_lk_contrast * (1.0 - u_night));
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
    // Lift, gamma and gain on the display image: shadows, midtones and highlights, each with a colour.
    if (u_lk_lgg != 0) c = pow(max(c * u_lk_gain + u_lk_lift * (1.0 - c), 0.0), u_lk_gamma);
    // Greens alone (the island is mostly green): a hue shift and a saturation, weighted by how green a pixel is.
    if (u_lk_green != vec2(0.0, 1.0)) {
        vec3 h = rgb2hsv(max(c, 0.0));
        float w = smoothstep(0.1, 0.0, abs(h.x - 0.27) - 0.06) * smoothstep(0.03, 0.12, h.y);   // yellow-green to blue-green, not greys
        h.x = fract(h.x + w * u_lk_green.x / 360.0); h.y = clamp(h.y * mix(1.0, u_lk_green.y, w), 0.0, 1.0);
        c = hsv2rgb(h);
    }
    // Saturation, and vibrance: more for dull colours, less for vivid ones and for skin and warm tones.
    if (u_lk_sat != 1.0 || u_lk_vib != 0.0) {
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722)), mx = max(c.r, max(c.g, c.b)), sat = (mx - min(c.r, min(c.g, c.b))) / max(mx, 1e-4);
        float warm = smoothstep(0.12, 0.0, abs(rgb2hsv(max(c, 0.0)).x - 0.07));   // oranges and skin
        float vib = u_lk_vib * (1.0 - sat) * (u_lk_vib > 0.0 ? 1.0 - 0.7 * warm : 1.0);
        c = max(mix(vec3(l), c, u_lk_sat * (1.0 + vib)), 0.0);
    }
    // Film grain: luma noise that changes every frame, strongest in the mid-tones.
    if (u_lk_grain > 0.0) {
        float n = fract(sin(dot(gl_FragCoord.xy + float(u_frame % 97) * vec2(17.3, 41.7), vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c += n * u_lk_grain * 0.09 * (0.25 + 3.0 * l * (1.0 - l));
    }
    c += (ign(gl_FragCoord.xy, u_frame) - 0.5) / 255.0;
    o = vec4(c, 1.0);
}
