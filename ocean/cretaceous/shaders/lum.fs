#version 330
// Auto-exposure, step 1: centre-weighted log luminance of the composited
// scene (terrain, plants, sky and clouds), averaged by the mip chain.
#include "common.glsl"
in vec2 v_uv; out vec4 o;
uniform sampler2D t_src, t_clouds, t_depth;
uniform int u_has_clouds;
uniform mat4 u_inv_viewproj; uniform vec3 u_cam; uniform float u_far;
// Clouds are a sky image: none in front of a surface that hides the layer (common.glsl).
vec4 clouds_at(vec2 uv) {
    ivec2 ds = textureSize(t_depth, 0);
    float d0 = texelFetch(t_depth, clamp(ivec2(uv * vec2(ds)), ivec2(0), ds - 1), 0).r;
    if (d0 > 0.0) {
        vec4 pf = u_inv_viewproj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
        vec3 dir = normalize(pf.xyz / pf.w - u_cam);
        vec4 cf = u_inv_viewproj * vec4(0.0, 0.0, 1.0, 1.0);
        vec3 fwd = normalize(cf.xyz / cf.w - u_cam);
        if (cloud_blocked(d0, u_far, u_cam, dir, fwd)) return vec4(0.0, 0.0, 0.0, 1.0);
    }
    return texture(t_clouds, uv);
}
void main() {
    vec3 c = texture(t_src, v_uv).rgb;
    if (u_has_clouds != 0) { vec4 cl = clouds_at(v_uv); c = c * cl.a + cl.rgb; }
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    vec2 d = v_uv - 0.5; float w = 1.0 - 0.7 * clamp(dot(d, d) * 4.0, 0.0, 1.0);
    o = vec4(log2(max(l, 1e-4)) * w, w, 0.0, 1.0);
}
