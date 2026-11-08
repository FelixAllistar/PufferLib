#version 330
#include "common.glsl"
#include "csm.glsl"
in vec3 v_world;
in float v_w, v_depth;
out vec4 o;
uniform sampler2D t_height, t_water, t_misc, t_noise, t_scene, t_depth, t_trans, t_skyview, t_weather;
uniform vec3 u_cam, u_sun;
uniform mat4 u_viewproj;
uniform vec2 u_screen;
uniform float u_far, u_time;
uniform int u_mode, u_frame;
#include "terrain_height.glsl"
// Ripple slopes from the tiling noise texture (B/A: wave normal xz). A single
// lookup repeats every tile and has one dominant bump size, and sharp sun
// glints map that out as an even grid of soft blobs. So every ripple layer
// sums three octaves at incommensurate scales (x 2.17, x 4.71), each rotated
// by its own angle and shifted, with each slope rotated back into world
// space; the finer octaves break each glint into glitter. Layers use
// different angles.
vec2 slope(vec2 uv) { return texture(t_noise, uv).zw * 2.0 - 1.0; }
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }
vec2 ripples(vec2 xz, float scale, float ang) {
    mat2 r0 = rot(ang), r1 = rot(ang + 2.03), r2 = rot(ang + 4.11);
    return transpose(r0) * slope(r0 * xz / scale) + transpose(r1) * slope(r1 * xz * (2.17 / scale) + vec2(0.37, 0.71)) * 0.55
         + transpose(r2) * slope(r2 * xz * (4.71 / scale) + vec2(0.83, 0.19)) * 0.35;
}
// Foam breakup noise (R/G fbm, about 0.5 +- 0.15), untiled the same way.
vec2 foam_noise(vec2 xz, float scale, float ang) {
    vec2 a = texture(t_noise, rot(ang) * xz / scale).xy, b = texture(t_noise, rot(ang + 2.03) * xz * (2.17 / scale) + vec2(0.37, 0.71)).xy;
    return (a * 0.65 + b * 0.35 - 0.5) * 1.3 + 0.5;
}
// Screen-space reflection against the scene drawn before the water (banks,
// plants, props). Marches the reflected ray in world space with growing
// steps, refines the first crossing behind the depth buffer, and fades near
// the screen border and for rays heading back at the camera. Misses fall
// back to the sky.
vec4 ssr(vec3 p, vec3 R, float dist) {
    float t = 0.4 + dist * 0.015, tp = 0.0;
    for (int i = 0; i < 22; i++) {
        vec4 c = u_viewproj * vec4(p + R * t, 1.0);
        if (c.w <= 0.05) break;
        vec2 uv = c.xy / c.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
        float sw = linear_depth(textureLod(t_depth, uv, 0.0).r, u_far);
        if (c.w > sw && c.w - sw < max((t - tp) * 1.5, 0.6)) {
            float a = tp, b = t;
            for (int k = 0; k < 5; k++) {
                float m = 0.5 * (a + b); vec4 cm = u_viewproj * vec4(p + R * m, 1.0); vec2 um = cm.xy / cm.w * 0.5 + 0.5;
                if (cm.w > linear_depth(textureLod(t_depth, um, 0.0).r, u_far)) { b = m; uv = um; } else a = m;
            }
            vec2 e = smoothstep(vec2(0.0), vec2(0.08), uv) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - uv);
            return vec4(textureLod(t_scene, uv, 0.0).rgb, e.x * e.y);
        }
        tp = t; t *= 1.33;
    }
    return vec4(0.0);
}
void main() {
    vec3 p = v_world;
    // The sheet continues under banks; keep it only where it is real water
    // or hidden below ground (the depth test then cuts the shoreline).
    float wdepth = 60.0;
    if (u_mode == 0) {
        vec2 wm = texture(t_water, map_uv(p.xz)).rg;
        wdepth = wm.r - sample_cubic(t_height, p.xz);
        if (wdepth < -1.5 || (wm.g < 0.5 && wdepth > -0.02)) discard;
    }
    vec3 V = u_cam - p; float dist = length(V); V /= dist;
    vec2 suv = gl_FragCoord.xy / u_screen;
    // Normals. t_misc.xy is the water velocity from render/water_flow.h:
    // direction x strength, 1 in rivers and fading smoothly out into lakes and
    // the sea past a mouth. Flowing water advects ripples along it (two-phase
    // flow map: two copies half a cycle apart, each reset while invisible, so
    // diverging or turning flow never tears); still water uses crossing wind
    // ripples. The two blend by strength, so a mouth has no seam.
    vec4 misc = u_mode == 0 ? texture(t_misc, map_uv(p.xz)) : vec4(0.5, 0.5, 0.0, 0.0);
    vec2 flow = (misc.xy - 0.5) * 2.0; float speed = misc.z * 2.5;
    float fs = u_mode == 0 ? length(flow) : 0.0;
    float river = smoothstep(0.05, 0.85, fs);        // ripple style: flowing .. wind chop
    float plume = smoothstep(0.35, 0.95, fs);        // silt and the end of the breakers: only close to the mouth
    // Gusts: the ripple strength varies in broad patches (cat's paws), so
    // even an untiled pattern does not read as an even carpet of glints.
    vec2 gp = rot(0.6) * p.xz + vec2(u_time * 0.7, u_time * 0.4);
    float gust = mix(0.35, 1.35, smoothstep(0.25, 0.75, texture(t_noise, gp / 317.0).r * 0.6 + texture(t_noise, rot(1.7) * gp / 71.0).g * 0.4));
    vec2 sf = vec2(0.0), ss = vec2(0.0);
    if (river > 0.0) {
        // The two flow-map phases also differ in scale and angle, so the
        // cross-fade never shows one pattern pulsing in place.
        float ph = u_time * 0.35, f0 = fract(ph), f1 = fract(ph + 0.5);
        vec2 drift = flow * speed * 15.6;   // metres travelled over one cycle
        vec2 a = ripples(p.xz - drift * f0, 13.0, 0.3), b = ripples(p.xz - drift * f1 + vec2(5.3, 2.9), 10.4, 1.9);
        sf = mix(a, b, abs(f0 - 0.5) * 2.0) * (0.22 + 0.3 * speed) * mix(1.0, gust, 0.6);
    }
    if (river < 1.0) {
        // Sea: wind chop on a long swell. Lakes and pools are calmer.
        float sea_k = 1.0 - smoothstep(0.5, 2.0, abs(p.y));
        vec2 a = ripples(p.xz + vec2(u_time * 0.25, u_time * 0.16), 23.0, 0.9);
        vec2 b = ripples(p.xz - vec2(u_time * 0.14, -u_time * 0.12), 9.0, 2.6);
        vec2 c = slope(rot(0.4) * p.xz / 160.0 + vec2(u_time * 0.004, 0.0));
        ss = (a + b * 0.6) * mix(0.18, 0.45, sea_k) * gust + c * mix(0.25, 0.7, sea_k);
    }
    vec2 sl = mix(ss, sf, river);
    // Close up, a fine chop turns the sun glints into sparkle instead of soft
    // blobs where the texture is magnified.
    float near = 1.0 - smoothstep(12.0, 50.0, dist);
    if (near > 0.0) sl += transpose(rot(1.3)) * slope(rot(1.3) * (p.xz - flow * speed * u_time * 2.0) / 2.3 + vec2(u_time * 0.05, 0.0)) * 0.22 * near * gust;
    vec3 n = vec3(sl.x, 1.0, sl.y);
    // Rain: raindrop rings stipple the surface close by (two flickering
    // high-frequency layers, gone by ~60 m where they would only alias).
    if (u_wet > 0.0 && dist < 60.0) {
        vec2 r1 = texture(t_noise, p.xz / 0.9 + vec2(floor(u_time * 6.0) * 0.37)).zw * 2.0 - 1.0;
        vec2 r2 = texture(t_noise, p.xz / 1.7 + vec2(floor(u_time * 4.0 + 0.5) * 0.61)).zw * 2.0 - 1.0;
        n.xz += (r1 + r2 * 0.6) * 0.35 * u_wet * (1.0 - smoothstep(20.0, 60.0, dist));
    }
    n.xz *= mix(1.0, 0.25, smoothstep(30.0, 900.0, dist));   // calmer with distance (less aliasing)
    n = normalize(n);
    // Refraction with depth-aware fallback.
    float scene_w = linear_depth(texture(t_depth, suv).r, u_far);
    float path = max(scene_w - v_w, 0.0) * dist / max(v_w, 1e-3);
    if (u_mode == 1) path = 200.0;
    vec2 ruv = suv + n.xz * 0.025 * clamp(path / 3.0, 0.0, 1.0);
    float rw = linear_depth(texture(t_depth, ruv).r, u_far);
    if (rw < v_w) ruv = suv;
    vec3 scene = texture(t_scene, ruv).rgb;
    // Absorption + in-scattering. Rivers carry silt; the sea is clear and blue.
    float turbid = plume * 0.8;
    vec3 sigma = mix(vec3(0.42, 0.075, 0.055), vec3(0.55, 0.45, 0.42), turbid);
    vec3 scatter = mix(vec3(0.012, 0.075, 0.085), vec3(0.075, 0.07, 0.045), turbid);
    // Plant and prop shadows on the surface: no glint, less sunlit silt.
    float cs = csm_shadow(p, vec3(0.0, 1.0, 0.0), u_sun, length(u_cam - p), 0.0);
    vec3 sun_col = sun_transmittance(t_trans, p.y, u_sun) * SUN_INTENSITY * cs;
    vec3 amb = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), u_sun);
    vec3 T = exp(-sigma * path);
    vec3 body = scatter * (sun_col * max(u_sun.y, 0.0) + amb) * 0.35;
    vec3 refr = scene * T + body * (1.0 - T);
    // Reflection: sky + sun glint.
    vec3 R = reflect(-V, n); R.y = abs(R.y) + 0.01;
    vec3 refl = sky_radiance(t_skyview, normalize(R), u_sun) * mix(1.0, NIGHT_DOME, u_night);   // the dome as seen (sky.fs)
    // Past ~2 km the chop hides reflected detail; skip the march there.
    float ssr_k = 1.0 - smoothstep(1200.0, 2000.0, dist);
    if (ssr_k > 0.0 && (u_mode == 0 || dist < 400.0)) {
        // Reflect off a smoothed normal: ripples blur the reflection instead of shredding it.
        vec3 Rs = reflect(-V, normalize(mix(vec3(0.0, 1.0, 0.0), n, 0.45)));
        if (Rs.y > 0.0) { vec4 h = ssr(p, Rs, dist); refl = mix(refl, h.rgb, h.a * ssr_k); }
    }
    float F = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    float spec = ggx_spec(n, V, u_sun, mix(0.04, 0.07, smoothstep(10.0, 300.0, dist))) * max(dot(n, u_sun), 0.0);
    vec3 col = mix(refr, refl, F) + sun_col * spec * F * 1.5;
    // Foam: shorelines and rapids.
    float fn = foam_noise(p.xz + vec2(u_time * 0.18, 0.0), 6.0, 0.7).x;
    // Breakers only on the sea; lakes get a thin wet line at the very edge.
    float sea = 1.0 - smoothstep(0.5, 2.0, abs(p.y));
    // Sea: breaker lines that roll shoreward over the shallows (depth is the
    // distance-to-shore proxy), broken up by noise, plus the swash line.
    float fn2 = foam_noise(p.xz - vec2(0.0, u_time * 0.34), 17.0, 2.3).y;
    float ph = wdepth * 2.4 + u_time * 0.8 + fn2 * 2.5;
    float breaker = pow(0.5 + 0.5 * sin(ph), 8.0) * smoothstep(3.0, 0.4, wdepth) * smoothstep(0.35, 0.65, fn * 0.6 + fn2 * 0.6);
    float swash = smoothstep(0.25, 0.02, wdepth) * smoothstep(0.35, 0.6, fn);
    float shore = mix(smoothstep(0.08, 0.0, wdepth) * 0.4 * smoothstep(0.45, 0.7, fn), max(breaker, swash), sea) * (1.0 - plume);
    // Rapids: white water in streaks along the current (noise stretched 4:1
    // in flow coordinates), not round spots.
    vec2 fd = fs > 1e-3 ? flow / fs : vec2(1.0, 0.0), fq = p.xz - flow * u_time * 1.5;
    float rapids = river * smoothstep(0.6, 0.95, misc.z) * smoothstep(0.55, 0.8, foam_noise(vec2(dot(fq, fd) * 0.25, dot(fq, vec2(-fd.y, fd.x))), 1.2, 0.0).y);
    float foam = clamp(shore * 0.6 + rapids * 0.7, 0.0, 1.0);
    col = mix(col, (sun_col * max(u_sun.y, 0.0) / PI + amb) * 0.8, foam * 0.85);
    // Soft edge where the surface meets the bank.
    float edge = smoothstep(0.0, 0.25, path);
    col = aerial(col, t_skyview, u_cam, p, u_sun);
    o = vec4(mix(scene, col, u_mode == 1 ? 1.0 : edge), 1.0);
}
