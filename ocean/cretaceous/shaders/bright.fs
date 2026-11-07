#version 330
in vec2 v_uv; out vec4 o;
uniform sampler2D t_src, t_clouds;
uniform int u_has_clouds;
void main() {
    vec3 c = texture(t_src, v_uv).rgb;
    if (u_has_clouds != 0) { vec4 cl = texture(t_clouds, v_uv); c = c * cl.a + cl.rgb; }
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    o = vec4(c * smoothstep(6.0, 30.0, l) / max(l, 1e-3) * min(l, 200.0), 1.0);
}
