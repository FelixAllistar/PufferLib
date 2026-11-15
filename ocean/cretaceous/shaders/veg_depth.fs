#version 330
#include "common.glsl"
// Depth prepass for plant meshes (veg.vs): the same alpha test and dithered
// fades as veg.fs, no colour. The shaded pass then runs once per pixel.
in vec3 v_world, v_rel;
in vec2 v_uv;
in float v_fade, v_leaf, v_var;
in vec3 v_cell;
flat in float v_layer;
uniform sampler2DArray t_atlas;
uniform vec3 u_cam;
uniform float u_edge_fade;
void main() {
    vec2 atx = v_uv * 256.0; float alod = 0.5 * log2(max(dot(dFdx(atx), dFdx(atx)), dot(dFdy(atx), dFdy(atx))));
    float keep = u_edge_fade > 0.0 && v_leaf > 0.5 ? veg_edge_keep(veg_card_nv(veg_card_normal(v_rel), v_rel), v_var) : 1.0;
    if (texture(t_atlas, vec3(v_uv, v_layer)).a * keep * (1.0 + 0.35 * max(alod, 0.0)) < 0.32) discard;   // as veg.fs
    float nz = veg_fade_key(v_cell);
    if (v_fade < -0.5) { if (nz <= -v_fade - 1.0) discard; }
    else if (v_fade < 0.999 && nz > v_fade) discard;
    if (v_leaf > 0.5 && length(v_world - u_cam) < 0.6) discard;
}
