#version 330
#include "common.glsl"
// Dinosaur skin: albedo+ao and DXT5nm normal+roughness+metal per material,
// lit like the props and terrain (sun with terrain horizon, cloud and cascade
// shadows, sky ambient under the canopy, bounce, aerial perspective). Skin
// adds a soft wrap and a little back-scatter at thin edges, wets below the
// waterline, and each animal gets a slight tint.
in vec3 v_world, v_n, v_t;
in vec2 v_uv;
in float v_w, v_bs, v_tint, v_water, v_fade;
uniform sampler2D t_albedo, t_detail;
uniform sampler2D t_trans, t_skyview, t_horizon0, t_horizon1, t_weather, t_misc;
uniform vec3 u_cam, u_sun, u_tint;
uniform vec2 u_map;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame;
layout(location = 0) out vec4 o;
layout(location = 1) out vec4 o_n;
vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }
#include "shadow.glsl"
#include "csm.glsl"
void main() {
    // LOD crossfade (render/dino.c): fade in [0,1] keeps noise <= fade; in
    // [-2,-1] the outgoing LOD keeps the complement.
    float nz = ign(gl_FragCoord.xy + 7.0, 0);
    if (v_fade < -0.5) { if (nz <= -v_fade - 1.0) discard; }
    else if (v_fade < 0.999 && nz > v_fade) discard;
    vec4 a = texture(t_albedo, v_uv), d = texture(t_detail, v_uv);
    vec3 Ng = normalize(v_n) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 T = normalize(v_t - Ng * dot(Ng, v_t)), B = cross(Ng, T) * v_bs;
    vec2 tn = d.wy * 2.0 - 1.0;
    vec3 n = normalize(T * tn.x + B * tn.y + Ng * sqrt(max(1.0 - dot(tn, tn), 0.0)));
    vec3 albedo = a.rgb * u_tint; float ao = mix(1.0, a.a, 0.85), rough = clamp(d.x, 0.3, 1.0), metal = d.z;
    // Individual variation: +-8% value, slight warm/cool shift.
    float tv = v_tint * 2.0 - 1.0;
    albedo *= (1.0 + 0.08 * tv) * mix(vec3(1.0), vec3(1.06, 1.0, 0.92), 0.5 + 0.5 * tv);
    // Wet below the waterline, with a darker band just above it.
    float wet = 1.0 - smoothstep(v_water - 0.02, v_water + 0.12, v_world.y);
    albedo *= mix(1.0, 0.55, wet); rough = mix(rough, 0.18, wet);
    wet_surface(albedo, rough, 0.3, Ng.y);   // rain
    vec3 p = v_world, V = normalize(u_cam - p), L = u_sun;
    float dist = length(u_cam - p);
    float shadow = terrain_sun(map_uv(p.xz), p, L) * csm_shadow(p, Ng, L, dist, 0.0);
    vec3 sun_col = sun_transmittance(t_trans, p.y, L) * SUN_INTENSITY;
    float nl = dot(n, L);
    float wrap = max((nl + 0.3) / 1.3, 0.0) * smoothstep(-0.25, 0.15, dot(Ng, L) + 0.15);
    float ndl = mix(max(nl, 0.0), wrap, 0.35);
    // Back-scatter through thin parts (frills, plates, edges) when backlit.
    float thin = pow(max(dot(-V, L), 0.0), 4.0) * (1.0 - abs(dot(Ng, V))) * 0.35;
    vec3 sky_up = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L), sky_h = sky_radiance(t_skyview, normalize(vec3(L.x, 0.15, L.z) * vec3(-1.0, 1.0, -1.0)), L);
    // Hemisphere: the belly and the undersides of the limbs see the ground
    // (dark green-brown), not the horizon sky; a body this large shades its
    // own underside far more than a flat terrain patch does.
    vec3 ground = sky_h * vec3(0.32, 0.36, 0.26);
    vec3 hemi = mix(ground, mix(sky_h, sky_up, max(n.y, 0.0)), smoothstep(-0.55, 0.35, n.y));
    vec3 ambient = hemi * 1.5 * canopy_sky(texture(t_misc, map_uv(p.xz)).a);
    vec3 bounce = sun_col * max(L.y, 0.0) * vec3(0.25, 0.27, 0.18) * 0.14 * (1.0 - n.y * 0.6) * shadow;
    vec3 diffuse = albedo * (1.0 - metal) / PI * sun_col * (ndl + thin * vec3(1.0, 0.55, 0.4)) * shadow + albedo * (ambient + bounce) * ao;
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    float fres = pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 F = F0 + (1.0 - F0) * fres;
    vec3 col = diffuse + sun_col * ggx_spec(n, V, L, rough) * shadow * max(nl, 0.0) * F;
    // Sky reflection: rough skin only gets a faint grazing sheen (roughness-limited Fresnel).
    vec3 Fr = F0 + (max(vec3(1.0 - rough), F0) - F0) * fres;
    col += sky_radiance(t_skyview, normalize(reflect(-V, n) + vec3(0.0, 0.2, 0.0)), L) * Fr * (1.0 - rough) * (1.0 - rough) * 0.3 * ao;
    col = aerial(col, t_skyview, u_cam, p, L);
    o = vec4(col, 1.0); o_n = vec4(0.0);
}
