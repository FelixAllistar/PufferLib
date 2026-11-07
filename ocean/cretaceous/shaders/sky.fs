#version 330
#include "common.glsl"
in vec2 v_uv; out vec4 o;
uniform sampler2D t_trans, t_skyview;
uniform mat4 u_inv_viewproj;
uniform vec3 u_cam, u_sun;
void main() {
    vec4 p = u_inv_viewproj * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w - u_cam);
    vec3 col = sky_radiance(t_skyview, dir, u_sun);
    // Sun disk with limb darkening.
    float c = dot(dir, u_sun), disk = smoothstep(0.99996, 0.999985, c);
    if (disk > 0.0 && dir.y > -0.01) {
        vec3 tr = sun_transmittance(t_trans, u_cam.y, dir);
        col += tr * SUN_INTENSITY * 2500.0 * disk * (0.6 + 0.4 * sqrt(max(0.0, 1.0 - (1.0 - c) / 1.5e-5)));
    }
    o = vec4(col, 1.0);
}
