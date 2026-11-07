#version 330
#include "common.glsl"
#include "csm.glsl"
in vec3 v_world;
in float v_w, v_depth;
out vec4 o;
uniform sampler2D t_height, t_water, t_misc, t_noise, t_scene, t_depth, t_trans, t_skyview, t_weather;
uniform vec3 u_cam, u_sun;
uniform vec2 u_screen;
uniform float u_far, u_time;
uniform int u_mode, u_frame;
#include "terrain_height.glsl"
vec3 wave_normal(vec2 uv) { vec2 n = texture(t_noise, uv).zw * 2.0 - 1.0; return vec3(n.x, 1.0, n.y); }
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
    // Normals: rivers advect ripples along the baked flow (two-phase flow map);
    // still water uses two crossing ripple layers.
    vec4 misc = u_mode == 0 ? texture(t_misc, map_uv(p.xz)) : vec4(0.5, 0.5, 0.0, 0.0);
    vec2 flow = (misc.xy - 0.5) * 2.0; float speed = misc.z * 2.5;
    float river = smoothstep(0.1, 0.4, length(flow));
    vec3 n;
    if (river > 0.0 && u_mode == 0) {
        float ph = u_time * 0.35, f0 = fract(ph), f1 = fract(ph + 0.5);
        vec2 off = flow * speed * 0.06;
        vec3 a = wave_normal(p.xz / 13.0 - off * f0 * 20.0), b = wave_normal(p.xz / 13.0 - off * f1 * 20.0 + 0.5);
        n = mix(a, b, abs(f0 - 0.5) * 2.0);
        n.xz *= 0.45 + 0.4 * speed;
    } else {
        vec3 a = wave_normal(p.xz / 23.0 + vec2(u_time * 0.011, u_time * 0.007));
        vec3 b = wave_normal(p.xz / 9.0 - vec2(u_time * 0.016, -u_time * 0.013));
        vec3 c = wave_normal(p.xz / 160.0 + vec2(u_time * 0.004, 0.0));
        n = a + b + c * 1.5;
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
    float turbid = u_mode == 0 ? river * 0.8 : 0.0;
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
    vec3 refl = sky_radiance(t_skyview, normalize(R), u_sun);
    float F = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    float spec = ggx_spec(n, V, u_sun, 0.06) * max(dot(n, u_sun), 0.0);
    vec3 col = mix(refr, refl, F) + sun_col * spec * F * 1.5;
    // Foam: shorelines and rapids.
    float fn = texture(t_noise, p.xz / 6.0 + vec2(u_time * 0.03, 0.0)).x;
    // Breakers only on the sea; lakes get a thin wet line at the very edge.
    float sea = 1.0 - smoothstep(0.5, 2.0, abs(p.y));
    float shore = mix(smoothstep(0.08, 0.0, wdepth) * 0.4, smoothstep(0.5, 0.05, wdepth), sea) * smoothstep(0.45, 0.7, fn) * (1.0 - river);
    float rapids = river * smoothstep(0.6, 0.95, misc.z) * smoothstep(0.55, 0.8, texture(t_noise, p.xz / 3.0 - flow * u_time * 0.5).y);
    float foam = clamp(shore * 0.6 + rapids * 0.7, 0.0, 1.0);
    col = mix(col, (sun_col * max(u_sun.y, 0.0) / PI + amb) * 0.8, foam * 0.85);
    // Soft edge where the surface meets the bank.
    float edge = smoothstep(0.0, 0.25, path);
    col = aerial(col, t_skyview, u_cam, p, u_sun);
    o = vec4(mix(scene, col, u_mode == 1 ? 1.0 : edge), 1.0);
}
