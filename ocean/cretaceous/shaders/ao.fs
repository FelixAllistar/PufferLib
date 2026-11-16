#version 330
#include "common.glsl"
in vec2 v_uv; out vec4 o;
uniform sampler2D t_depth;
uniform mat4 u_inv_viewproj;
uniform vec3 u_cam;
uniform float u_far;
uniform float u_lk_radius;   // the look (Look studio): the world radius, minus one
// Ambient occlusion from the depth buffer (in the spirit of Alchemy/SAO): points of the surface round a
// pixel that rise above its tangent plane, within a world radius of half a metre (near) to a few metres
// (far), darken it. Grounds animals, rocks, trunks and plants in contact shade, and deepens creases,
// which the per-object baked terms cannot do between objects.
//
// Twelve samples per pixel are too few on their own: with a random rotation per pixel the result was a
// visible stipple round every plant and fine stripes on bare ground at full resolution. The rotation
// instead follows a 4x4 interleaved pattern, so every 4x4 block of pixels together covers 16 rotations
// (192 directions), and ao_blur.fs averages each 4x4 block back to a smooth term. R: occlusion (1 open),
// G: linear depth for the blur's edge test.
vec3 ao_dir(vec2 uv) { vec4 pf = u_inv_viewproj * vec4(uv * 2.0 - 1.0, 1.0, 1.0); return normalize(pf.xyz / pf.w - u_cam); }
vec3 ao_pos(vec2 uv, vec3 fwd, out float raw) {
    ivec2 ds = textureSize(t_depth, 0);
    raw = texelFetch(t_depth, clamp(ivec2(uv * vec2(ds)), ivec2(0), ds - 1), 0).r;
    vec3 dir = ao_dir(uv);
    return u_cam + dir * (linear_depth(raw, u_far) / max(dot(dir, fwd), 1e-3));
}
void main() {
    vec4 cf = u_inv_viewproj * vec4(0.0, 0.0, 1.0, 1.0); vec3 fwd = normalize(cf.xyz / cf.w - u_cam);
    float raw; vec3 P = ao_pos(v_uv, fwd, raw);
    if (raw <= 0.0) { o = vec4(1.0, u_far, 0.0, 1.0); return; }
    float lin = linear_depth(raw, u_far);
    vec2 px = 1.0 / vec2(textureSize(t_depth, 0)); float r0;
    vec3 xr = ao_pos(v_uv + vec2(px.x, 0.0), fwd, r0) - P, xl = P - ao_pos(v_uv - vec2(px.x, 0.0), fwd, r0);
    vec3 yu = ao_pos(v_uv + vec2(0.0, px.y), fwd, r0) - P, yd = P - ao_pos(v_uv - vec2(0.0, px.y), fwd, r0);
    vec3 dx = dot(xr, xr) < dot(xl, xl) ? xr : xl, dy = dot(yu, yu) < dot(yd, yd) ? yu : yd;   // the side on this surface
    vec3 N = normalize(cross(dx, dy)); if (dot(N, u_cam - P) < 0.0) N = -N;
    float dist = length(P - u_cam), R = clamp(dist * 0.035, 0.5, 3.0) * (1.0 + u_lk_radius);
    float ang = length(ao_dir(v_uv + vec2(px.x, 0.0)) - ao_dir(v_uv));   // radians a pixel spans
    float rpx = min(R / max(dist * ang, 1e-4), 90.0);                    // the radius in pixels, capped (close-ups)
    if (rpx < 1.5) { o = vec4(1.0, lin, 0.0, 1.0); return; }
    // 4x4 Bayer order: neighbouring pixels get rotations far apart, and the radii are staggered too.
    ivec2 q = ivec2(gl_FragCoord.xy) & 3;
    const float B[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    float k = (B[q.y * 4 + q.x] + 0.5) / 16.0;
    float a = 0.0, rot = k * 6.2832;
    const int NS = 12;
    for (int i = 0; i < NS; i++) {
        float t = (float(i) + fract(k * 7.0 + 0.5)) / float(NS), th = rot + float(i) * 2.39996;
        vec2 off = vec2(cos(th), sin(th)) * sqrt(t) * rpx * px;
        float rs; vec3 v = ao_pos(v_uv + off, fwd, rs) - P;
        if (rs <= 0.0) continue;
        float vv = dot(v, v);
        a += max(dot(v, N) - 0.01 * dist, 0.0) / (vv + 0.05 * R * R) * (1.0 - smoothstep(R * R, 4.0 * R * R, vv));
    }
    o = vec4(clamp(1.0 - 1.6 * R * a / float(NS), 0.0, 1.0), lin, 0.0, 1.0);
}
