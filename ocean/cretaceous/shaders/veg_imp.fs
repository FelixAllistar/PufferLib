#version 330
#include "common.glsl"
#include "veg_light.glsl"
in vec3 v_world, v_base;
in vec2 v_uv;
in float v_fade, v_w, v_hue, v_blend;
flat in float v_layer0, v_layer1;
flat in vec2 v_rot;
uniform sampler2DArray t_imp_albedo, t_imp_normal;
out vec4 o;
void main() {
    float layer = ign(gl_FragCoord.xy, 0) < v_blend ? v_layer1 : v_layer0;
    vec4 a = texture(t_imp_albedo, vec3(v_uv, layer));
    // Mip-aware alpha: averaged coverage shrinks in small mips, which made
    // distant crowns thin and speckled.
    vec2 tx = v_uv * 256.0; float lod = 0.5 * log2(max(dot(dFdx(tx), dFdx(tx)), dot(dFdy(tx), dFdy(tx))));
    a.a *= 1.0 + max(lod, 0.0) * 0.3;
    if (a.a < 0.45) discard;
    if (v_fade < 0.999 && ign(gl_FragCoord.xy + 17.0, 0) > v_fade) discard;
    vec4 nn = texture(t_imp_normal, vec3(v_uv, layer));
    vec3 n = nn.xyz * 2.0 - 1.0; float c = v_rot.x, s = v_rot.y;
    vec3 N = normalize(vec3(c * n.x - s * n.z, n.y, s * n.x + c * n.z));
    // Baked impostor texels mix leaves and bark; tint by greenness so trunks keep their colour.
    float leafy = smoothstep(0.0, 0.04, a.g - a.r);
    vec3 albedo = a.rgb * mix(vec3(1.0), vec3(1.0 + 0.14 * v_hue, 1.0 + 0.05 * v_hue, 1.0 - 0.12 * v_hue) * stand_tint(v_base.xz), leafy);
    o = vec4(veg_shade(albedo, N, nn.w, 1.0, v_world, v_base), 1.0);
}
