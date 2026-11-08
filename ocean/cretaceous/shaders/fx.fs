#version 330
// Combat effects (render/fx.c), lit like the scene: sun and sky light,
// aerial perspective; alpha blended over the HDR scene.
#include "common.glsl"
in vec2 v_uv; in vec4 v_col; in float v_kind; in vec3 v_world; out vec4 o;
uniform sampler2D t_trans, t_skyview;
uniform vec3 u_cam, u_sun;
void main() {
    vec2 q = v_uv * 2.0 - 1.0; float r = length(q), a; vec3 n = vec3(0.0, 1.0, 0.0);
    float gloss = 0.0;
    if (v_kind < 1.0) {                 // droplet: a soft ball, lit from its rounded side
        a = smoothstep(1.0, 0.45, r);
        n = normalize(vec3(q.x * 0.8, 0.6, q.y * 0.8)); gloss = 0.6;
    } else if (v_kind < 2.0) {          // decal: a splat with a ragged rim, spatters and a darker pooled core
        float seed = (v_kind - 1.0) * 2.0, ang = atan(q.y, q.x);
        float rim = 0.62 + 0.22 * vnoise2(vec2(ang * 1.6 + seed * 17.0, seed * 5.0)) + 0.12 * vnoise2(q * 4.0 + seed * 29.0);
        a = smoothstep(rim, rim - 0.1, r);
        vec2 cell = floor(q * 5.0 + seed * 11.0), f = fract(q * 5.0 + seed * 11.0) - 0.5;   // satellite drops
        float h = hash12(cell), dr = 0.12 + 0.18 * hash12(cell + 7.3);
        if (h > 0.72 && r < 0.98) a = max(a, smoothstep(dr, dr * 0.6, length(f)) * 0.85);
        gloss = 0.35 * smoothstep(rim * 0.8, 0.0, r);
    } else {                            // trail: soft across the ribbon
        a = 1.0 - smoothstep(0.0, 1.0, abs(v_uv.y * 2.0 - 1.0));
        a *= a;
    }
    a *= v_col.a;
    if (a < 0.004) discard;
    vec3 L = u_sun;
    vec3 sun_col = sun_transmittance(t_trans, v_world.y, L) * SUN_INTENSITY;
    vec3 amb = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L);
    vec3 col;
    if (v_kind >= 2.0) col = v_col.rgb * (amb * 0.9 + sun_col * 0.04);   // a light smear, no hard shading
    else {
        col = v_col.rgb / PI * sun_col * max(dot(n, L), 0.0) + v_col.rgb * amb * 0.8;
        vec3 vd = normalize(u_cam - v_world), hv = normalize(vd + L);    // wet blood glints
        col += sun_col * gloss * 0.04 * pow(max(dot(n, hv), 0.0), 60.0);
    }
    col = aerial(col, t_skyview, u_cam, v_world, L);
    o = vec4(col, a);
}
