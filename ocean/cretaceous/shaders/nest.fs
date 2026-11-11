#version 330
#include "common.glsl"
// Nests and eggs (render/nest.c): soil, leaf and fern litter, sticks and egg
// shells, lit as the terrain is (sun with the cascaded shadows, sky ambient,
// ground bounce, the terrain's horizon and cloud shadows, less sky under a
// forest canopy) and matte: no specular, shells included. The ground
// material photos give the detail; each part takes its hue from a base
// colour, so a nest reads as soil and litter on any ground.
in vec3 v_world, v_n; in vec2 v_k; in float v_w;
out vec4 o;
uniform sampler2D t_trans, t_skyview, t_horizon0, t_horizon1, t_weather, t_misc, t_normal;
uniform sampler2DArray t_mat_a;   // ground albedo (view.c load_materials): rock scree dirt forest fern mud sand moss
uniform vec3 u_cam, u_sun;
uniform vec2 u_map;
uniform float u_time, u_cloud_cover;
#include "shadow.glsl"
#include "csm.glsl"
vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }
uniform vec3 u_egg;   // shell albedo of the egg being drawn (material 6)

// The photo's luminance against its own average: detail around 1.
float detail(int k, vec2 uv) {
    vec3 c = texture(t_mat_a, vec3(uv, float(k))).rgb, m = textureLod(t_mat_a, vec3(0.5, 0.5, float(k)), 14.0).rgb;
    const vec3 Y = vec3(0.2126, 0.7152, 0.0722);
    return clamp(dot(c, Y) / max(dot(m, Y), 0.01), 0.25, 2.4);
}

// Value noise and its slope, for clods and leaf scatter at scales the photos do not have.
float nz(vec2 q) { return vnoise2(q); }
vec2 nz_grad(vec2 q) { const float e = 0.15; return vec2(nz(q + vec2(e, 0.0)) - nz(q - vec2(e, 0.0)), nz(q + vec2(0.0, e)) - nz(q - vec2(0.0, e))) / (2.0 * e); }

void main() {
    vec3 n = normalize(v_n), p = v_world;
    float kind = v_k.x, ao = v_k.y;
    vec3 albedo;
    if (kind < 2.0) {          // packed soil (0) to leaf litter (1): the dirt and forest-floor photos
        // Litter lies in drifts and single leaves, not as a wash: thresholded
        // noise at leaf size decides where soil shows through.
        float leaf = nz(p.xz * 11.0) * 0.6 + nz(p.xz * 3.1 + 7.3) * 0.4;
        float lit = smoothstep(0.42, 0.58, leaf + (clamp(kind, 0.0, 1.0) - 0.5) * 0.9);
        vec3 soil = vec3(0.11, 0.083, 0.06) * detail(2, p.xz / 3.2) * mix(0.7, 1.2, nz(p.xz * 5.0 + 2.1));
        // Leaves and dead fern pinnae (the fern sward photo, browned), mixed in patches.
        vec3 leaves = mix(vec3(0.15, 0.105, 0.06), vec3(0.2, 0.15, 0.08), nz(p.xz * 23.0)) * mix(detail(3, p.xz / 2.8), detail(4, p.xz / 1.4), step(0.55, nz(p.xz * 4.0 + 3.7)));
        albedo = mix(soil, leaves, lit);
        ao *= mix(0.75, 1.0, lit);   // soil showing between leaves lies a little deeper
        vec2 g = nz_grad(p.xz * 6.0) * 0.06 + nz_grad(p.xz * 19.0) * 0.025 * (1.0 - lit) + nz_grad(p.xz * 31.0) * 0.02 * lit;
        n = normalize(n - vec3(g.x, 0.0, g.y) * 6.0);
    } else if (kind < 4.0) {   // sticks: bark grain from the dirt photo along the stick's sides; tone 0 dark .. 1 weathered
        float tone = kind - 2.0;
        vec3 a = abs(n); vec2 uv = a.x > a.z ? p.zy : p.xy;
        albedo = mix(vec3(0.05, 0.035, 0.024), vec3(0.12, 0.09, 0.062), tone) * detail(2, uv * vec2(1.6, 0.35)) * mix(0.8, 1.15, nz(uv * vec2(3.0, 40.0)));
    } else {                   // an egg: a dull shell with faint mottling
        albedo = u_egg * mix(0.9, 1.06, nz(p.xz * 60.0 + p.y * 37.0));
    }
    // The ground's own light at this spot (terrain.fs): canopy shade and tint, the baked sky occlusion.
    vec2 uv = map_uv(p.xz);
    float canopy = texture(t_misc, uv).a, sky_ao = mix(0.45, 1.0, texture(t_normal, uv).z);
    albedo = mix(albedo, albedo * vec3(0.8, 0.86, 0.7), canopy * 0.4);
    float rough = 0.9;
    wet_surface(albedo, rough, 1.0, n.y);
    vec3 L = u_sun;
    float shadow = L.y > -0.05 ? terrain_sun(uv, p, L) * csm_shadow(p, n, L, length(u_cam - p), 0.0) : 0.0;
    vec3 sun_col = sun_transmittance(t_trans, p.y, L) * SUN_INTENSITY;
    float ndl = max(dot(n, L), 0.0);
    vec3 sky_up = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L), sky_h = sky_radiance(t_skyview, normalize(vec3(L.x, 0.15, L.z) * vec3(-1.0, 1.0, -1.0)), L);
    float contact = contact_occlusion(p);
    vec3 ambient = mix(sky_h, sky_up, n.y * 0.5 + 0.5) * 1.5 * canopy_sky(canopy) * sky_ao * contact;
    vec3 bounce = sun_col * max(L.y, 0.0) * vec3(0.12, 0.14, 0.09) * (0.55 - 0.45 * n.y);
    vec3 col = albedo / PI * sun_col * ndl * shadow + albedo * (ambient * ao + bounce * contact * ao);   // matte: no specular term
    col = aerial(col, t_skyview, u_cam, p, L);
    o = vec4(col, 1.0);
}
