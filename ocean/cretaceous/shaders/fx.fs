#version 330
// Combat effects (render/fx.c), lit like the scene: sun (terrain, cloud and
// cascade shadows) and sky light (canopy, contact occlusion under animals),
// aerial perspective; alpha blended over the HDR scene.
#include "common.glsl"
in vec2 v_uv; in vec4 v_col; in float v_kind; in vec3 v_world; out vec4 o;
uniform sampler2D t_trans, t_skyview, t_horizon0, t_horizon1, t_weather, t_misc;
uniform vec3 u_cam, u_sun;
uniform vec2 u_map;
uniform float u_time, u_cloud_cover;
#include "shadow.glsl"
#include "csm.glsl"
vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }
void main() {
    vec2 q = v_uv * 2.0 - 1.0; float r = length(q), a; vec3 n = vec3(0.0, 1.0, 0.0);
    float gloss = 0.0;
    if (v_kind < 1.0) {                 // droplet: a soft ball, lit from its rounded side
        a = smoothstep(1.0, 0.45, r);
        n = normalize(vec3(q.x * 0.8, 0.6, q.y * 0.8)); gloss = 1.0;
    } else if (v_kind >= 1.75 && v_kind < 2.0) {   // dung: a few dark lumps
        float seed = (v_kind - 1.75) * 4.0; a = 0.0;
        for (int k = 0; k < 4; k++) {
            vec2 c = (vec2(hash12(vec2(seed * 91.0, float(k) * 3.1)), hash12(vec2(seed * 37.0 + 5.0, float(k) * 7.7))) - 0.5) * 0.9;
            a = max(a, smoothstep(0.34, 0.2, length(q - c)));
        }
        n = normalize(vec3(q.x * 0.5, 0.8, q.y * 0.5));
    } else if (v_kind >= 1.5 && v_kind < 2.0) {    // footprint: a heel pad and three toes pointing along +x (the way it went)
        float heel = smoothstep(0.3, 0.14, length((q - vec2(-0.42, 0.0)) * vec2(1.0, 1.15)));
        float toes = 0.0;
        for (int k = -1; k <= 1; k++) {   // each toe a long lobe from the heel, the middle one longest, ending in a point
            float ang = float(k) * 0.5, len = k == 0 ? 0.62 : 0.52; vec2 d = vec2(cos(ang), sin(ang)), l = q - (vec2(-0.42, 0.0) + d * len);
            float al = dot(l, d) / len, ac = dot(l, vec2(-d.y, d.x)) / (0.14 * (1.0 - 0.45 * clamp(al, 0.0, 1.0)));
            toes = max(toes, smoothstep(1.0, 0.55, length(vec2(al, ac))));
        }
        a = max(heel, toes) * (0.75 + 0.25 * vnoise2(q * 7.0 + v_kind * 40.0));   // pressed unevenly
    } else if (v_kind < 2.0) {          // decal: a splat with a ragged rim, spatters and a darker pooled core
        float seed = (v_kind - 1.0) * 2.0, ang = atan(q.y, q.x);
        float rim = 0.62 + 0.22 * vnoise2(vec2(ang * 1.6 + seed * 17.0, seed * 5.0)) + 0.12 * vnoise2(q * 4.0 + seed * 29.0);
        a = smoothstep(rim, rim - 0.1, r);
        vec2 cell = floor(q * 5.0 + seed * 11.0), f = fract(q * 5.0 + seed * 11.0) - 0.5;   // satellite drops
        float h = hash12(cell), dr = 0.12 + 0.18 * hash12(cell + 7.3);
        if (h > 0.72 && r < 0.98) a = max(a, smoothstep(dr, dr * 0.6, length(f)) * 0.85);
        gloss = 0.35 * smoothstep(rim * 0.8, 0.0, r);
    } else {                            // trail: strongest toward the tip, soft at both edges
        a = smoothstep(0.0, 0.8, v_uv.y) * (1.0 - smoothstep(0.85, 1.0, v_uv.y));
        a *= a;
    }
    a *= v_col.a;
    if (a < (v_kind >= 1.0 && v_kind < 2.0 ? 0.004 : 0.03)) discard;   // droplets and trails write depth (fx.c): no faint fringes
    vec3 L = u_sun, p = v_world; float dist = length(u_cam - p);
    float shadow = terrain_sun(map_uv(p.xz), p, L) * csm_shadow(p, n, L, dist, 0.0);
    float contact = dist < 70.0 ? contact_occlusion(p) : 1.0;
    vec3 sun_col = sun_transmittance(t_trans, p.y, L) * SUN_INTENSITY * shadow;
    vec3 amb = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L) * canopy_sky(texture(t_misc, map_uv(p.xz)).a) * contact;
    vec3 col;
    if (v_kind >= 2.0) col = v_col.rgb * (amb * 1.2 + sun_col * 0.08);   // a light smear, no hard shading
    else {
        col = v_col.rgb / PI * sun_col * max(dot(n, L), 0.0) + v_col.rgb * amb * 0.8;
        vec3 vd = normalize(u_cam - v_world), hv = normalize(vd + L);    // wet blood glints
        col += sun_col * gloss * 0.04 * pow(max(dot(n, hv), 0.0), 60.0);
    }
    col = aerial(col, t_skyview, u_cam, v_world, L);
    o = vec4(col, a);
}
