#version 330
#include "common.glsl"
// Small animals (render/critters.c): colours painted from the rest pose (so
// the pattern sticks to the animal as it bends), lit like the dinosaurs (sun
// with terrain horizon, cloud and cascade shadows, sky ambient under the
// canopy, ground bounce, aerial perspective). Fur and feathers are matte: a
// wrapped diffuse, a dim broad lobe and a soft rim of sky light; scales a
// little less matte; only the eyes catch a small highlight. Each animal's
// seed shifts its value and hue a little.
in vec3 v_world, v_n, v_rest, v_rn;
in vec4 v_seg;
in float v_w, v_seed;
uniform sampler2D t_trans, t_skyview, t_horizon0, t_horizon1, t_weather, t_misc;
uniform vec3 u_cam, u_sun;
uniform vec2 u_map;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame, u_kind;
layout(location = 0) out vec4 o;
layout(location = 1) out vec4 o_n;
vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }
#include "shadow.glsl"
#include "csm.glsl"
float hash3(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), f.x), mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x), mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// Albedo, roughness and how much light wraps past the terminator, per kind and part.
void paint(out vec3 alb, out float rough, out float wrap) {
    int part = int(v_seg.y + 0.5);
    vec3 r = v_rest; float s = v_seed;
    float belly = smoothstep(-0.1, -0.6, v_rn.y), side_up = smoothstep(0.2, 0.8, v_rn.y);
    float fine = vnoise(r * 60.0 + s * 17.0), coarse = vnoise(r * 9.0 + s * 31.0);
    rough = 0.85; wrap = 0.5;
    if (u_kind == 0) {
        // Multituberculate: agouti brown above, buff below; pinkish bare tail, feet and ears.
        vec3 back = mix(vec3(0.10, 0.085, 0.065), vec3(0.20, 0.16, 0.115), fine) * mix(0.9, 1.1, coarse);
        alb = mix(back, vec3(0.34, 0.29, 0.23), belly);
        if (r.x < -0.5) alb = mix(vec3(0.24, 0.18, 0.16), vec3(0.33, 0.25, 0.22), fine);
        if (part >= 1 && part <= 4) alb = mix(alb, vec3(0.45, 0.33, 0.28), smoothstep(0.55, 0.8, v_seg.z));
        if (part == 7) alb = mix(vec3(0.22, 0.16, 0.12), vec3(0.42, 0.29, 0.26), smoothstep(0.0, 0.5, -v_rn.x));
        if (r.x > 0.46) alb = vec3(0.35, 0.22, 0.2);   // nose
    } else if (u_kind == 1) {
        // Repenomamus: dark brown, grizzled, a paler muzzle and throat.
        float grizzle = vnoise(r * 140.0 + s * 13.0);
        alb = mix(vec3(0.06, 0.05, 0.04), vec3(0.15, 0.125, 0.095), grizzle * grizzle) * mix(0.92, 1.08, coarse);
        alb = mix(alb, vec3(0.20, 0.16, 0.12), smoothstep(0.38, 0.47, r.x) * (0.4 + 0.6 * belly));
        alb = mix(alb, vec3(0.12, 0.10, 0.08), belly * 0.6);
        if (part >= 1 && part <= 4) alb *= mix(1.0, 0.7, v_seg.z);
    } else if (u_kind == 2) {
        // Lizard: olive brown with dark cross-bands, a pale stripe along each
        // side of the back, a cream belly; fine scale speckle.
        vec3 back = mix(vec3(0.17, 0.17, 0.09), vec3(0.28, 0.27, 0.15), coarse);
        float band = smoothstep(0.35, 0.8, sin(r.x * 30.0 + s * 4.0)) * smoothstep(0.0, 0.5, v_rn.y + 0.2);
        back = mix(back, vec3(0.07, 0.07, 0.045), band * 0.7);
        float stripe = smoothstep(0.12, 0.0, abs(v_rn.y - 0.5)) * step(-0.5, r.x) * step(r.x, 0.3);
        back = mix(back, vec3(0.42, 0.40, 0.26), stripe * 0.7);
        alb = mix(back, vec3(0.40, 0.38, 0.28), belly);
        alb *= 0.8 + 0.4 * fine;
        if (part >= 1 && part <= 4) alb = mix(alb, back * 0.9, 0.5);
        rough = 0.62; wrap = 0.25;
    } else if (u_kind == 3) {
        // Small bird: mottled brown above with a darker cap, buff underparts,
        // dark flight feathers with a pale wing bar, dark bill and legs.
        vec3 up = mix(vec3(0.16, 0.11, 0.07), vec3(0.32, 0.23, 0.14), fine) * mix(0.85, 1.1, coarse);
        alb = mix(up, vec3(0.44, 0.38, 0.29), belly);
        alb = mix(alb, vec3(0.08, 0.065, 0.05), smoothstep(0.2, 0.26, r.x) * side_up);   // cap
        if (part == 5) {
            // Coverts brown at the root and leading edge, flight feathers dark toward the tip
            // and (less) along the trailing edge, a pale bar across the coverts.
            float span = v_seg.w, chord = r.x - 0.07;   // 0 at the shoulder, + toward the leading edge
            vec3 cov = mix(vec3(0.17, 0.12, 0.08), vec3(0.27, 0.20, 0.13), fine);
            vec3 flight = vec3(0.07, 0.06, 0.05);
            float dark = max(smoothstep(0.45, 0.8, span), 0.6 * smoothstep(-0.08, -0.16, chord));
            alb = mix(cov, flight, dark);
            float bar = smoothstep(0.018, 0.0, abs(chord + 0.06)) * (1.0 - smoothstep(0.35, 0.55, span));
            alb = mix(alb, vec3(0.46, 0.40, 0.30), bar);
        }
        if (part == 10) alb = mix(vec3(0.10, 0.08, 0.06), vec3(0.22, 0.16, 0.10), fine);
        if (part == 8) { alb = vec3(0.05, 0.045, 0.04); rough = 0.5; }
        if (part >= 1 && part <= 4) { alb = vec3(0.14, 0.11, 0.09); rough = 0.6; }
    } else {
        // Fish: dark olive back with faint spots, silver flanks, white belly, a
        // dusky lateral line; fins olive grey.
        float flank = smoothstep(0.55, -0.1, v_rn.y);
        vec3 back = mix(vec3(0.045, 0.06, 0.05), vec3(0.09, 0.11, 0.08), coarse);
        back *= mix(1.0, 0.55, smoothstep(0.6, 0.8, vnoise(r * 22.0 + s * 9.0)));
        alb = mix(back, vec3(0.34, 0.37, 0.35), flank);
        alb = mix(alb, vec3(0.48, 0.48, 0.45), belly);
        alb *= mix(1.0, 0.7, smoothstep(0.012, 0.0, abs(r.y - 0.012)) * step(-0.36, r.x));
        if (part == 9) alb = mix(vec3(0.10, 0.11, 0.08), vec3(0.20, 0.20, 0.15), fine);
        rough = 0.45; wrap = 0.2;
    }
    if (part == 6) { alb = vec3(0.02, 0.018, 0.016); rough = 0.22; wrap = 0.0; }
    // Individuals: +-12% value and a warm or grey cast.
    float tv = s * 2.0 - 1.0, hue = fract(s * 7.31) * 2.0 - 1.0;
    if (part != 6) alb *= (1.0 + 0.12 * tv) * mix(vec3(1.0), vec3(1.07, 1.0, 0.9), 0.5 + 0.5 * hue * 0.6);
}

void main() {
    vec3 albedo; float rough, wrap;
    paint(albedo, rough, wrap);
    vec3 Ng = normalize(v_n) * (gl_FrontFacing ? 1.0 : -1.0), n = Ng;
    wet_surface(albedo, rough, 0.4, Ng.y);   // rain
    vec3 p = v_world, V = normalize(u_cam - p), L = u_sun;
    float dist = length(u_cam - p);
    float shadow = terrain_sun(map_uv(p.xz), p, L) * csm_shadow(p, Ng, L, dist, 0.0);
    vec3 sun_col = sun_transmittance(t_trans, p.y, L) * SUN_INTENSITY;
    float nl = dot(n, L);
    float ndl = mix(max(nl, 0.0), max((nl + wrap) / (1.0 + wrap), 0.0), 0.6);
    // Occlusion near the ground: the underside of a body this low sees mostly soil.
    float ao = u_kind == 4 ? 1.0 : mix(0.5, 1.0, smoothstep(0.0, 0.14, v_rest.y));
    if (int(v_seg.y + 0.5) == 6) ao = 1.0;
    vec3 sky_up = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L), sky_h = sky_radiance(t_skyview, normalize(vec3(L.x, 0.15, L.z) * vec3(-1.0, 1.0, -1.0)), L);
    vec3 ground = sky_h * vec3(0.32, 0.36, 0.26);
    vec3 hemi = mix(ground, mix(sky_h, sky_up, max(n.y, 0.0)), smoothstep(-0.55, 0.35, n.y));
    vec3 ambient = hemi * 1.5 * canopy_sky(texture(t_misc, map_uv(p.xz)).a);
    vec3 bounce = sun_col * max(L.y, 0.0) * vec3(0.25, 0.27, 0.18) * 0.14 * (1.0 - n.y * 0.6) * shadow;
    vec3 col = albedo / PI * sun_col * ndl * shadow + albedo * (ambient + bounce) * ao;
    // A dim, broad lobe with roughness-limited Fresnel: no plastic hot spot.
    float rs = max(rough, mix(0.2, 0.35, u_wet));
    float fres = pow(1.0 - max(dot(n, V), 0.0), 5.0);
    float F = 0.04 + (max(1.0 - rs, 0.04) - 0.04) * fres;
    col += sun_col * ggx_spec(n, V, L, rs) * shadow * max(nl, 0.0) * F * 0.5;
    // Fur and feather rim: sky light caught at the silhouette.
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * smoothstep(-0.2, 0.6, n.y);
    col += mix(sky_h, sky_up, 0.5) * albedo * rim * (rough > 0.7 ? 0.8 : 0.4) * ao;
    col = aerial(col, t_skyview, u_cam, p, L);
    o = vec4(col, 1.0); o_n = vec4(0.0);
}
