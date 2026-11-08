#version 330
#include "common.glsl"
in vec2 v_uv; out vec4 o;
uniform sampler2D t_trans, t_skyview;
uniform mat4 u_inv_viewproj;
uniform vec3 u_cam, u_sun;
uniform float u_time;
void main() {
    vec4 p = u_inv_viewproj * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w - u_cam);
    // Below the horizontal the sky pass only shows past the end of the ocean
    // tiles (the sea is flat, the planet is not): paint it as fully hazed sea,
    // the colour aerial() fades distant water to, so no bright band shows
    // between the ocean's edge and the horizon.
    vec3 col = sky_radiance(t_skyview, dir.y < 0.02 ? normalize(vec3(dir.x, 0.02, dir.z)) : dir, u_sun);
    // At night the dome is darker than the light it gives (common.glsl); at
    // the horizon it meets the haze that distant ground and sea fade to.
    col *= mix(1.0, mix(NIGHT_HAZE, NIGHT_DOME, smoothstep(0.02, 0.2, dir.y)), u_night);
    // Sun disk with limb darkening; at night u_sun is the moon (view.c).
    float c = dot(dir, u_sun);
    if (u_night < 0.5) {
        float disk = smoothstep(0.99996, 0.999985, c);
        if (disk > 0.0 && dir.y > -0.01) {
            vec3 tr = sun_transmittance(t_trans, u_cam.y, dir);
            col += tr * SUN_INTENSITY * 2500.0 * disk * (0.6 + 0.4 * sqrt(max(0.0, 1.0 - (1.0 - c) / 1.5e-5)));
        }
    } else if (dir.y > -0.01) {
        // Full moon: a little larger, grey maria, and a faint halo.
        float disk = smoothstep(0.99993, 0.99996, c);
        vec3 tr = sun_transmittance(t_trans, u_cam.y, dir);
        vec3 t = normalize(cross(u_sun, vec3(0.0, 1.0, 0.0))), b = cross(t, u_sun);
        vec2 mp = vec2(dot(dir, t), dot(dir, b)) / 0.0085;
        float maria = smoothstep(0.35, 0.1, length(mp - vec2(0.25, 0.2))) * 0.35 + smoothstep(0.3, 0.05, length(mp - vec2(-0.3, -0.1))) * 0.3 + smoothstep(0.25, 0.05, length(mp - vec2(0.1, -0.4))) * 0.25;
        col += tr * SUN_INTENSITY * 60.0 * (disk * (1.0 - maria) + 0.02 * exp(-(1.0 - c) * 3000.0));
        // Stars: one per cell of a fine direction grid, a few bright ones,
        // warm to blue-white, twinkling a little; faded by the sky's own glow.
        float sky_l = dot(col, vec3(0.3, 0.59, 0.11)), vis = smoothstep(0.0, 0.12, dir.y) * smoothstep(0.35, 0.9, u_night) / (1.0 + sky_l * 0.5);
        vec3 q = dir * 300.0, cell = floor(q);
        float h = fract(sin(dot(cell, vec3(127.1, 311.7, 74.7))) * 43758.5453);
        if (h > 0.972) {
            vec3 sp = cell + 0.5 + (vec3(fract(h * 17.0), fract(h * 31.0), fract(h * 47.0)) - 0.5) * 0.6;
            float d = length(q - sp), mag = pow(fract(h * 113.0), 5.0) + 0.04;
            float tw = 0.75 + 0.25 * sin(u_time * (2.0 + 5.0 * fract(h * 71.0)) + h * 400.0);
            vec3 tint = mix(vec3(1.0, 0.85, 0.7), vec3(0.75, 0.85, 1.0), fract(h * 53.0));
            col += tint * mag * tw * 90.0 * exp(-d * d * 28.0) * vis;
        }
        // Milky Way: a faint, mottled band across the sky.
        vec3 bn = normalize(vec3(0.35, 0.45, 0.82));
        float band = exp(-pow(dot(dir, bn) / 0.2, 2.0));
        if (band > 0.02) {
            vec3 m = dir * 9.0; vec3 mi = floor(m), mf = fract(m); mf = mf * mf * (3.0 - 2.0 * mf);
            float n = 0.0;
            for (int k = 0; k < 8; k++) { vec3 oc = vec3(k & 1, (k >> 1) & 1, k >> 2); vec3 w = mix(1.0 - mf, mf, oc); n += fract(sin(dot(mi + oc, vec3(12.9898, 78.233, 37.719))) * 43758.5453) * w.x * w.y * w.z; }
            col += vec3(0.07, 0.08, 0.11) * band * smoothstep(0.25, 0.85, n) * vis;
        }
    }
    o = vec4(col, 1.0);
}
