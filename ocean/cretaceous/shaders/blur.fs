#version 330
in vec2 v_uv; out vec4 o;
uniform sampler2D t_src; uniform vec2 u_dir;
void main() {
    const float w[5] = float[5](0.227, 0.194, 0.121, 0.054, 0.016);
    vec3 c = texture(t_src, v_uv).rgb * w[0];
    for (int i = 1; i < 5; i++) c += (texture(t_src, v_uv + u_dir * float(i)).rgb + texture(t_src, v_uv - u_dir * float(i)).rgb) * w[i];
    o = vec4(c, 1.0);
}
