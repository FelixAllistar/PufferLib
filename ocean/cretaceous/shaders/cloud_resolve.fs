#version 330
// Temporal accumulation for the half-resolution cloud march.
in vec2 v_uv; out vec4 o;
uniform sampler2D t_cur, t_hist;
uniform mat4 u_inv_viewproj, u_prev_viewproj;
uniform vec3 u_cam;
uniform vec2 u_screen;
uniform int u_hist_valid;
void main() {
    vec4 c = texture(t_cur, v_uv);
    if (u_hist_valid == 0) { o = c; return; }
    vec4 pf = u_inv_viewproj * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(pf.xyz / pf.w - u_cam);
    vec4 pc = u_prev_viewproj * vec4(u_cam + dir * 20000.0, 1.0);
    vec2 puv = pc.xy / pc.w * 0.5 + 0.5;
    if (pc.w <= 0.0 || any(lessThan(puv, vec2(0.0))) || any(greaterThan(puv, vec2(1.0)))) { o = c; return; }
    vec2 px = 1.0 / u_screen;
    vec4 mn = c, mx = c;
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
        vec4 s = texture(t_cur, v_uv + vec2(x, y) * px); mn = min(mn, s); mx = max(mx, s);
    }
    vec4 h = clamp(texture(t_hist, puv), mn, mx);
    o = mix(h, c, 0.1);
}
