#version 330
#include "common.glsl"
// Depth prepass for impostors (veg_imp.vs): the same coverage tests as veg_imp.fs.
in vec2 v_uv;
in float v_fade, v_blend;
flat in float v_layer0, v_layer1;
uniform sampler2DArray t_imp_albedo;
void main() {
    float layer = ign(gl_FragCoord.xy, 0) < v_blend ? v_layer1 : v_layer0;
    float a = texture(t_imp_albedo, vec3(v_uv, layer)).a;
    vec2 tx = v_uv * 256.0; float lod = 0.5 * log2(max(dot(dFdx(tx), dFdx(tx)), dot(dFdy(tx), dFdy(tx))));
    a *= 1.0 + max(lod, 0.0) * 0.3;
    if (a < 0.45) discard;
    if (v_fade < 0.999 && ign(gl_FragCoord.xy + 17.0, 0) > v_fade) discard;
}
