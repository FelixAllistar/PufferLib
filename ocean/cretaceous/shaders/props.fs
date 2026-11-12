#version 330
#include "common.glsl"
// Scanned props: albedo+ao and normal+roughness arrays, lit like the terrain
// (sun with terrain horizon + cloud shadow, sky ambient, aerial perspective).
// Moss grows on up-facing surfaces where the ground is mossy or shaded.
in vec3 v_world, v_n, v_t, v_base;
in vec2 v_uv;
in float v_w, v_fade, v_moss, v_tint, v_hfrac, v_bs;
flat in float v_layer;
uniform sampler2DArray t_albedo, t_detail;
uniform sampler2D t_trans, t_skyview, t_horizon0, t_horizon1, t_weather, t_misc;
uniform vec3 u_cam, u_sun;
uniform vec2 u_map;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame;
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
void main() {
    float nz = ign(gl_FragCoord.xy, 0);
    if (v_fade < -0.5) { if (nz <= -v_fade - 1.0) discard; }
    else if (v_fade < 0.999 && nz > v_fade) discard;
    vec4 a = texture(t_albedo, vec3(v_uv, v_layer)), d = texture(t_detail, vec3(v_uv, v_layer));
    vec3 Ng = normalize(v_n), T = normalize(v_t - Ng * dot(Ng, v_t)), B = cross(Ng, T) * v_bs;
    vec2 tn = d.wy * 2.0 - 1.0;   // t_detail is DXT5nm: roughness, normal y, -, normal x
    vec3 n = normalize(T * tn.x + B * tn.y + Ng * sqrt(max(1.0 - dot(tn, tn), 0.0)));
    vec3 albedo = a.rgb; float ao = a.a, rough = d.x;
    // Driftwood: sun- and salt-bleached grey.
    float lum = dot(albedo, vec3(0.3, 0.59, 0.11));
    vec3 bleached = vec3(0.22, 0.2, 0.17) * clamp(lum / 0.08, 0.5, 1.4);
    albedo = mix(albedo, bleached, v_tint * 0.9); rough = mix(rough, 0.85, v_tint);
    // Moss and lichen on up-facing surfaces, broken up by noise.
    // Coverage follows up-facing surfaces (geometric normal, so it drapes
    // smoothly) with soft low-frequency breakup; the texture shows through thin moss.
    float mn = vnoise(v_world * 0.9) * 0.7 + vnoise(v_world * 3.1) * 0.3;
    float up = Ng.y * 0.7 + n.y * 0.3;
    // Weathered tops: lichen, grime and rain-darkened crust. The top face
    // takes the full sky and the sun, so a bright scan read chalky there.
    albedo *= mix(1.0, 0.72, smoothstep(0.35, 0.9, up) * (1.0 - v_tint));
    float moss = v_moss * smoothstep(0.15, 0.7, up + (mn - 0.5) * 0.6 + v_moss * 0.25);
    // The scan's own value detail shows through (cracks stay dark), and the
    // rock's brown through thin moss, so a mossy boulder is not a mint felt.
    vec3 moss_col = vec3(0.04, 0.075, 0.022) * (0.6 + 0.7 * vnoise(v_world * 9.0)) * clamp(pow(lum / 0.125, 0.9), 0.35, 1.8);
    albedo = mix(albedo, moss_col, moss * 0.75); rough = mix(rough, 0.95, moss);
    // Far off, the normal map's relief averages away in its mips and the
    // reduced meshes lose their facets: the rough surface that relief stood
    // for becomes roughness, so a distant rock or log does not keep the
    // sheen of a smooth, plastic one.
    float dist = length(u_cam - v_world);
    rough = max(rough, 0.92 * smoothstep(25.0, 150.0, dist));
    wet_surface(albedo, rough, 0.7, Ng.y);
    // Ground contact: soil-stained and occluded where the prop meets the ground.
    float above = v_world.y - (v_base.y + 0.0);
    float contact = 1.0 - smoothstep(0.0, 0.25, v_hfrac);
    albedo *= mix(1.0, 0.65, contact * (1.0 - v_tint * 0.5)); ao *= mix(1.0, 0.55, contact);
    vec3 p = v_world, V = normalize(u_cam - p), L = u_sun;
    float shadow = terrain_sun(map_uv(p.xz), p, L) * csm_shadow(p, Ng, L, dist, 0.0);
    vec3 sun_col = sun_transmittance(t_trans, p.y, L) * SUN_INTENSITY;
    float ndl = max(dot(n, L), 0.0) * smoothstep(-0.05, 0.1, dot(Ng, L) + 0.1);
    vec3 sky_up = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L), sky_h = sky_radiance(t_skyview, normalize(vec3(L.x, 0.15, L.z) * vec3(-1.0, 1.0, -1.0)), L);
    float beside = contact_occlusion(p);   // animals beside the rock
    vec3 ambient = mix(sky_h, sky_up, n.y * 0.5 + 0.5) * 1.5 * canopy_sky(texture(t_misc, map_uv(v_base.xz)).a) * beside;
    vec3 bounce = sun_col * max(L.y, 0.0) * vec3(0.25, 0.27, 0.18) * 0.12 * (1.0 - n.y * 0.6) * shadow;
    vec3 diffuse = albedo / PI * (sun_col * ndl * shadow) + albedo * (ambient * ao + bounce * ao);
    // Rock, bark and wood are matte: a broad, dim lobe (roughness >= 0.7 dry,
    // ~0.45 under a rain film) with roughness-limited Fresnel, so no edge or
    // facet ever flashes white. Rain adds a faint sky sheen.
    float rs = max(rough, mix(0.7, 0.45, u_wet));
    float fres = pow(1.0 - max(dot(n, V), 0.0), 5.0);
    float F = 0.04 + (max(1.0 - rs, 0.04) - 0.04) * fres;
    vec3 col = diffuse + sun_col * ggx_spec(n, V, L, rs) * shadow * ndl * F * 0.6;
    col += mix(sky_h, sky_up, 0.6) * (F * u_wet * 0.25 * ao);   // wet sheen
    col = aerial(col, t_skyview, u_cam, p, L);
    o = vec4(col, 1.0); o_n = vec4(0.0);
}
