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
    // The two baked views either side of the view azimuth are blended: they
    // are premultiplied by coverage (veg.c bake_impostors), so a linear mix is
    // the crown halfway between them. (Picking one per pixel by a dither
    // left a fixed speckle over every crown between views, with no temporal
    // filter to resolve it.) veg_imp_depth.fs runs the same coverage test.
    vec4 a = mix(texture(t_imp_albedo, vec3(v_uv, v_layer0)), texture(t_imp_albedo, vec3(v_uv, v_layer1)), v_blend);
    // Mip-aware alpha: averaged coverage shrinks in small mips, which made
    // distant crowns thin and speckled, and left a tiered conifer as a bare
    // stick once its sprays fell under a pixel. Each mip level down raises
    // the coverage by half, so a far crown keeps the body it has up close.
    vec2 tx = v_uv * 256.0; float lod = 0.5 * log2(max(dot(dFdx(tx), dFdx(tx)), dot(dFdy(tx), dFdy(tx))));
    if (a.a * (1.0 + max(lod, 0.0) * 0.5) < 0.45) discard;
    if (v_fade < 0.999 && ign(gl_FragCoord.xy + 17.0, 0) > v_fade) discard;
    // Divide the albedo by its own alpha, and the half-size normal and AO by
    // the albedo alpha one level down, which covers the same footprint.
    a.rgb /= max(a.a, 0.02);
    float cl = max(lod, 1.0);
    float cov_n = max(mix(textureLod(t_imp_albedo, vec3(v_uv, v_layer0), cl).a, textureLod(t_imp_albedo, vec3(v_uv, v_layer1), cl).a, v_blend), 0.02);
    vec4 nn = min(mix(texture(t_imp_normal, vec3(v_uv, v_layer0)), texture(t_imp_normal, vec3(v_uv, v_layer1)), v_blend) / cov_n, 1.0);
    vec3 n = nn.xyz * 2.0 - 1.0; float c = v_rot.x, s = v_rot.y;
    vec3 N = normalize(vec3(c * n.x - s * n.z, n.y, s * n.x + c * n.z));
    // Baked impostor texels mix leaves and bark; tint by greenness so trunks keep their colour.
    float leafy = smoothstep(0.0, 0.04, a.g - a.r);
    vec3 albedo = a.rgb * mix(vec3(1.0), vec3(1.0 + 0.14 * v_hue, 1.0 + 0.05 * v_hue, 1.0 - 0.12 * v_hue) * stand_tint(v_base.xz), leafy);
    // Bark texels are lit as bark (Lambert, no translucency, no green-filtered
    // sky), as the mesh lights them: shaded as a leaf, a bare trunk glowed in
    // the sun and read as a bright stick under a dark crown.
    o = vec4(veg_shade(albedo, N, nn.w, leafy, v_world, v_base, 1.0), 1.0);
}
