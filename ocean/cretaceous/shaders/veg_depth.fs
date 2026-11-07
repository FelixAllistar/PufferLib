#version 330
#include "common.glsl"
// Depth prepass for plant meshes (veg.vs): the same alpha test and dithered
// fades as veg.fs, no colour. The shaded pass then runs once per pixel.
in vec3 v_world;
in vec2 v_uv;
in float v_fade;
flat in float v_layer;
uniform sampler2DArray t_atlas;
uniform vec3 u_cam;
uniform vec4 u_occ;
void main() {
    if (texture(t_atlas, vec3(v_uv, v_layer)).a < 0.5) discard;
    float nz = ign(gl_FragCoord.xy, 0);
    if (v_fade < -0.5) { if (nz <= -v_fade - 1.0) discard; }
    else if (v_fade < 0.999 && nz > v_fade) discard;
    if (ign(gl_FragCoord.xy + 31.0, 0) > occ_keep(v_world, u_cam, u_occ)) discard;
}
