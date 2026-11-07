#version 330
in vec2 v_uv;
flat in float v_layer;
uniform sampler2DArray t_imp_albedo;
void main() {
    float a = texture(t_imp_albedo, vec3(v_uv, v_layer)).a;
    vec2 tx = v_uv * 256.0; float lod = 0.5 * log2(max(dot(dFdx(tx), dFdx(tx)), dot(dFdy(tx), dFdy(tx))));
    if (a * (1.0 + max(lod, 0.0) * 0.3) < 0.45) discard;
}
