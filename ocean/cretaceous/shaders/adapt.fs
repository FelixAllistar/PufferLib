#version 330
// Auto-exposure, step 2: average log luminance, adapted over time like an eye.
in vec2 v_uv; out vec4 o;
uniform sampler2D t_lum, t_prev;
uniform float u_rate;    // 0..1 blend toward the new value this frame (1 = reset)
void main() {
    vec4 s = textureLod(t_lum, vec2(0.5), 12.0);
    float now = s.r / max(s.g, 1e-4), prev = texture(t_prev, vec2(0.5)).r;
    o = vec4(mix(prev, now, u_rate), 0.0, 0.0, 1.0);
}
