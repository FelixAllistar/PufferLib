#version 330
#include "common.glsl"
in vec2 v_uv; out vec4 o;
uniform sampler2D t_src, t_clouds, t_bloom, t_depth, t_adapt;
uniform int u_has_clouds, u_frame;
uniform float u_exposure;
uniform vec3 u_sun;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
// Depth-aware upsample of the half-res clouds: each half-res texel was
// marched against the depth of the top-left pixel of its 2x2 block; weight
// the four nearest by how well that depth matches this pixel's, so cloud lit
// behind a leaf gap does not bleed onto the leaves (white speckles).
uniform float u_far;
vec4 clouds_up(vec2 uv) {
    ivec2 hs = textureSize(t_clouds, 0), ds = textureSize(t_depth, 0);
    float d0 = texelFetch(t_depth, clamp(ivec2(uv * vec2(ds)), ivec2(0), ds - 1), 0).r;
    if (d0 <= 0.0) return texture(t_clouds, uv);   // sky pixel (reverse-Z 0): plain bilinear
    float dz = linear_depth(d0, u_far);
    vec2 f = uv * vec2(hs) - 0.5; ivec2 i0 = ivec2(floor(f)); vec2 fr = f - floor(f);
    vec4 acc = vec4(0.0); float wsum = 0.0;
    for (int k = 0; k < 4; k++) {
        ivec2 o = ivec2(k & 1, k >> 1), q = clamp(i0 + o, ivec2(0), hs - 1);
        float bw = (o.x == 1 ? fr.x : 1.0 - fr.x) * (o.y == 1 ? fr.y : 1.0 - fr.y);
        float dq = linear_depth(texelFetch(t_depth, min(q * 2, ds - 1), 0).r, u_far);
        float w = bw * (1.0 / (1e-3 + abs(dq - dz) / max(dz, 1.0) * 40.0)) + 1e-6;
        acc += texelFetch(t_clouds, q, 0) * w; wsum += w;
    }
    return acc / wsum;
}
void main() {
    vec3 c = texture(t_src, v_uv).rgb;
    if (u_has_clouds != 0) { vec4 cl = clouds_up(v_uv); c = c * cl.a + cl.rgb; }
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
    // ACES per channel bleaches bright saturated colour (sunset clouds, lit
    // foliage) toward cream. Blend in the curve applied to the brightest
    // channel, which keeps hue and saturation; the brightest highlights
    // still roll off toward white.
    float m = max(c.r, max(c.g, c.b));
    vec3 hc = c * (aces(vec3(m)).r / max(m, 1e-5));
    hc = mix(hc, vec3(aces(vec3(m)).r), smoothstep(2.0, 12.0, m) * 0.6);
    c = mix(aces(c), hc, 0.4);
    float vig = 1.0 - 0.18 * dot(v_uv - 0.5, v_uv - 0.5) * 2.5;
    c *= vig;
    c = pow(c, vec3(1.0 / 2.2));
    c += (ign(gl_FragCoord.xy, u_frame) - 0.5) / 255.0;
    o = vec4(c, 1.0);
}
