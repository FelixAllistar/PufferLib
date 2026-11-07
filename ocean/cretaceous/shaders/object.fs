#version 330
#include "common.glsl"
#include "csm.glsl"
in vec3 v_world, v_n; in float v_w; out vec4 o;
uniform sampler2D t_trans, t_skyview, t_horizon0, t_horizon1;
uniform vec3 u_cam, u_sun, u_color;
uniform float u_far;
void main() {
    vec3 n = normalize(v_n), L = u_sun;
    vec3 sun_col = sun_transmittance(t_trans, v_world.y, L) * SUN_INTENSITY;
    vec3 amb = mix(sky_radiance(t_skyview, vec3(L.x, 0.1, L.z), L), sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L), n.y * 0.5 + 0.5);
    float sh = csm_shadow(v_world, n, L, length(u_cam - v_world), 0.0);
    vec3 col = u_color / PI * sun_col * max(dot(n, L), 0.0) * sh + u_color * amb * 0.8;
    col = aerial(col, t_skyview, u_cam, v_world, L);
    o = vec4(col, 1.0);
}
