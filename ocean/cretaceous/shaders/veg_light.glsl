// Foliage and bark lighting: wrapped diffuse, leaf transmission, sky ambient
// scaled by baked crown occlusion, terrain/cloud shadow at the plant base.
uniform sampler2D t_trans, t_skyview, t_horizon0, t_horizon1, t_weather, t_misc;
uniform float u_understory;   // 1 for ground cover (canopy-shaded sky light), 0 for trees and impostors
uniform vec3 u_cam, u_sun;
uniform vec2 u_map;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame;
float veg_gloss = 1.0;   // per-fragment specular boost (glossy cycad leaflets)
vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }
#include "shadow.glsl"
#include "csm.glsl"
vec3 veg_shade(vec3 albedo, vec3 N, float ao, float leaf, vec3 p, vec3 base) {
    vec3 L = u_sun, V = normalize(u_cam - p);
    float shadow = terrain_sun(map_uv(base.xz), p, L);
    // Sun shadow maps: leaves get the softer, sun-biased lookup. Light that
    // passes through leaves is only partly blocked by other leaves.
    float cs = csm_shadow(p, N, L, length(u_cam - p), leaf);
    if (u_csm_debug == 1) return csm_debug_tint(p) * (0.15 + cs * 2.0) * (leaf > 0.5 ? vec3(0.6, 1.0, 0.6) : vec3(1.0));
    vec3 sun = sun_transmittance(t_trans, p.y, L) * SUN_INTENSITY;
    float ndl = mix(max(dot(N, L), 0.0), clamp(dot(N, L) * 0.55 + 0.45, 0.0, 1.0), leaf);
    // Inner crown sees less sun. With shadow maps the leaves above do most of
    // that, so the baked occlusion only adds a little on top.
    float self = mix(1.0, smoothstep(0.3, 0.95, ao), leaf * (u_csm_count > 0 ? 0.45 : 1.0));
    float back = pow(max(dot(-V, L), 0.0), 3.0) * leaf * self;
    vec3 sky_up = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L);
    vec3 sky_h = sky_radiance(t_skyview, normalize(vec3(-L.x, 0.15, -L.z)), L);
    vec3 amb = mix(sky_h, sky_up, N.y * 0.5 + 0.5) * 1.5 * ao;
    if (u_understory > 0.0) amb *= canopy_sky(texture(t_misc, map_uv(base.xz)).a * u_understory);
    // Ground bounce lights undersides and trunks (forest floor albedo ~0.15).
    vec3 bounce = sun * max(L.y, 0.0) * 0.15 * (0.5 - 0.5 * N.y) * mix(0.6, 1.0, shadow) / PI;
    vec3 col = albedo / PI * sun * shadow * (ndl * self * cs + back * vec3(0.55, 0.8, 0.4) * 0.65 * mix(0.3, 1.0, cs)) + albedo * (amb + bounce * ao);
    col += sun * shadow * cs * self * ggx_spec(N, V, L, mix(0.5, 0.3, step(1.5, veg_gloss))) * 0.012 * veg_gloss * leaf;
    return aerial(col, t_skyview, u_cam, p, L);
}
