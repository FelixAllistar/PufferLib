#version 330
// Auto-exposure, step 1: centre-weighted log luminance of the composited
// scene (terrain, plants, sky and clouds), averaged by the mip chain.
in vec2 v_uv; out vec4 o;
uniform sampler2D t_src, t_clouds;
uniform int u_has_clouds;
void main() {
    vec3 c = texture(t_src, v_uv).rgb;
    if (u_has_clouds != 0) { vec4 cl = texture(t_clouds, v_uv); c = c * cl.a + cl.rgb; }
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    vec2 d = v_uv - 0.5; float w = 1.0 - 0.7 * clamp(dot(d, d) * 4.0, 0.0, 1.0);
    o = vec4(log2(max(l, 1e-4)) * w, w, 0.0, 1.0);
}
