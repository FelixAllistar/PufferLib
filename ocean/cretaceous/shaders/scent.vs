#version 330
// The scent view (render/scentfx.c): camera-facing quads drawn over the
// finished frame. kind 0: a wisp, kind 1: a pulse ring, kind 3: a halo round a sensed animal, kind 2: the whole
// screen (the darkened edges).
layout(location = 0) in vec3 a_centre;
layout(location = 1) in vec2 a_corner;
layout(location = 2) in vec4 a_col;
layout(location = 3) in vec4 a_par;    // size (m), kind, seed, phase
uniform mat4 u_viewproj;
uniform vec3 u_right, u_up;
out vec2 v_uv;
out vec4 v_col, v_par;
out float v_w;
void main() {
    v_uv = a_corner * 0.5 + 0.5; v_col = a_col; v_par = a_par;
    if (a_par.y > 1.5 && a_par.y < 2.5) { v_w = 0.0; gl_Position = vec4(a_corner, 0.0, 1.0); return; }
    vec3 p = a_centre + (u_right * a_corner.x + u_up * a_corner.y) * a_par.x;
    vec4 c = u_viewproj * vec4(p, 1.0);
    v_w = c.w; gl_Position = c;
}
