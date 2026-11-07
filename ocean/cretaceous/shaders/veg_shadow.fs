#version 330
// Shadow-map pass for plant meshes (veg.vs): alpha-tested leaf cards cast
// leaf-shaped shadows. Depth only.
in vec2 v_uv;
flat in float v_layer;
uniform sampler2DArray t_atlas;
void main() {
    float a = texture(t_atlas, vec3(v_uv, v_layer), 1.0).a;   // one mip coarser: cheaper, shadows are soft anyway
    // Mip-aware alpha (as veg_imp.fs): averaged coverage in small mips would
    // thin out distant crowns' shadows.
    vec2 tx = v_uv * 256.0; float lod = 0.5 * log2(max(dot(dFdx(tx), dFdx(tx)), dot(dFdy(tx), dFdy(tx))));
    if (a * (1.0 + max(lod, 0.0) * 0.3) < 0.5) discard;
}
