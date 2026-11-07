#version 330
#include "common.glsl"
in vec2 v_uv; out vec4 o;
uniform sampler2D t_src, t_clouds, t_bloom, t_depth, t_adapt;
uniform int u_has_clouds, u_frame;
uniform float u_exposure;
uniform vec3 u_sun;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main() {
    vec3 c = texture(t_src, v_uv).rgb;
    if (u_has_clouds != 0) { vec4 cl = texture(t_clouds, v_uv); c = c * cl.a + cl.rgb; }
    c += texture(t_bloom, v_uv).rgb * 0.06;
    // Exposure follows the sun so dawn, dusk and night stay readable.
    float day = smoothstep(-0.15, 0.35, u_sun.y);
    float ev_fixed = mix(1.5, -0.6, day);
    // Eye adaptation: aim the adapted average at mid-grey, but only partly
    // and within +-2.5 stops of the sun-based exposure, so night stays dark
    // and a shaded cliff or forest floor reads without blowing out the sky.
    // Brightening (shaded forest floor, under canopy) adapts further than
    // darkening, as the eye does: a sunlit view or a sunset keeps the 60%
    // response, so its sky does not wash out.
    float avg = exp2(texture(t_adapt, vec2(0.5)).r);
    float ev_auto = clamp(log2(0.18 / (0.28 * avg)), ev_fixed - 2.5, ev_fixed + 3.0);
    float k = ev_auto > ev_fixed ? mix(0.6, 0.85, smoothstep(0.0, 1.5, ev_auto - ev_fixed)) : 0.6;
    float ev = mix(ev_fixed, ev_auto, k) + u_exposure;
    c *= 0.28 * exp2(ev);
    c = aces(c);
    float vig = 1.0 - 0.18 * dot(v_uv - 0.5, v_uv - 0.5) * 2.5;
    c *= vig;
    c = pow(c, vec3(1.0 / 2.2));
    c += (ign(gl_FragCoord.xy, u_frame) - 0.5) / 255.0;
    o = vec4(c, 1.0);
}
