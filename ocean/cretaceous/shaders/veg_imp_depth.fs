#version 330
#include "common.glsl"
// Depth prepass for impostors (veg_imp.vs): the same coverage tests as veg_imp.fs.
in vec2 v_uv;
in float v_fade, v_blend;
flat in float v_layer0, v_layer1;
uniform sampler2DArray t_imp_albedo;
uniform int u_imp_blend;   // as veg_imp.fs
const float IMP_BIAS = -1.0;   // as veg_imp.fs
void main() {
    float wb = smoothstep(0.2, 0.8, v_blend), a;
    if (u_imp_blend == 0) a = texture(t_imp_albedo, vec3(v_uv, ign(gl_FragCoord.xy, 0) < v_blend ? v_layer1 : v_layer0), IMP_BIAS).a;
    else if (wb <= 0.0 || wb >= 1.0) a = texture(t_imp_albedo, vec3(v_uv, wb >= 1.0 ? v_layer1 : v_layer0), IMP_BIAS).a;
    else a = mix(texture(t_imp_albedo, vec3(v_uv, v_layer0), IMP_BIAS).a, texture(t_imp_albedo, vec3(v_uv, v_layer1), IMP_BIAS).a, wb);
    vec2 tx = v_uv * 256.0; float lod = 0.5 * log2(max(dot(dFdx(tx), dFdx(tx)), dot(dFdy(tx), dFdy(tx)))) + IMP_BIAS;
    a *= 1.0 + max(lod, 0.0) * 0.5;
    if (a < 0.45) discard;
    // Fades by cells of the card (1/40 of it, 25-50 cm of crown) rather than a screen dither, as the meshes
    // do (common.glsl veg_fade_key): the meshes fade out by their own cells and the card fills in at the same rate.
    float fk = veg_fade_key(vec3(v_uv * 40.0, 0.0));
    if (v_fade < 0.999) { if (fk <= 1.0 - v_fade) discard; }
    else if (v_fade < 1.999 && veg_fade_key(vec3(v_uv * 40.0 + 17.0, 3.0)) > v_fade - 1.0) discard;
}
