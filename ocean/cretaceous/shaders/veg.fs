#version 330
#include "common.glsl"
#include "veg_light.glsl"
in vec3 v_world, v_normal, v_base;
in vec2 v_uv;
in float v_ao, v_leaf, v_hue, v_fade, v_w, v_var;
flat in float v_layer;
flat in float v_bark;
// Bark tone per species on top of the per-species bark layer (photo or
// painted, already coloured): default, tree fern, palm, conifer, cycad,
// angiosperm, then (render/veg_gen.h) twig, frond rachis, cone, crozier, untinted.
const vec3 BARK[11] = vec3[11](vec3(0.85, 0.78, 0.68), vec3(1.0), vec3(0.88, 0.8, 0.68), vec3(0.88, 0.84, 0.8), vec3(0.78, 0.72, 0.62), vec3(0.85, 0.78, 0.68),
                               vec3(0.8, 0.72, 0.6), vec3(0.78, 0.86, 0.52), vec3(0.85, 0.62, 0.42), vec3(0.72, 0.64, 0.46), vec3(1.0));
uniform sampler2DArray t_atlas;
uniform int u_bake;
layout(location = 0) out vec4 o;
layout(location = 1) out vec4 o_n;
void main() {
    vec4 a = texture(t_atlas, vec3(v_uv, v_layer));
    if (a.a < 0.5) discard;
    // fade in [0,1]: keep where noise <= fade. fade in [-2,-1]: LOD partner,
    // keep the complementary half (noise > -fade - 1).
    if (u_bake == 0) {
        float nz = ign(gl_FragCoord.xy, 0);
        if (v_fade < -0.5) { if (nz <= -v_fade - 1.0) discard; }
        else if (v_fade < 0.999 && nz > v_fade) discard;
        if (v_leaf > 0.5 && length(v_world - u_cam) < 0.6) discard;   // only leaves right at the lens (the camera boom stops at trunks)
    }
    vec3 albedo = a.rgb * mix(vec3(1.0), vec3(1.0 + 0.14 * v_hue, 1.0 + 0.05 * v_hue, 1.0 - 0.12 * v_hue) * (u_bake == 1 ? vec3(1.0) : stand_tint(v_base.xz)), v_leaf);
    if (v_leaf > 0.5) {
        // attr.w on leaves is frond dryness: old fronds yellow, then brown.
        float dry = v_bark / 255.0, l = dot(albedo, vec3(0.3, 0.59, 0.11));
        vec3 straw = l * vec3(1.75, 1.4, 0.6), dead = l * vec3(1.3, 0.98, 0.62);
        albedo = mix(albedo, mix(straw, dead, smoothstep(0.5, 1.0, dry)), smoothstep(0.0, 0.5, dry));
    } else {
        int tone = int(v_bark + 0.5);
        albedo *= BARK[clamp(tone, 0, 10)];
        // Trunk surface patterns from the tube uv (u around, v along, in m).
        vec2 q = v_uv;
        if (tone == 4) {            // cycad: spiral armour of diamond leaf bases
            vec2 g = vec2(q.x * 7.0 + q.y * 5.0, q.x * 7.0 - q.y * 5.0);
            vec2 f = abs(fract(g) - 0.5);
            float edge = smoothstep(0.36, 0.5, max(f.x, f.y));
            albedo *= mix(1.0, 0.45, edge) * (0.9 + 0.2 * fract(sin(dot(floor(g), vec2(12.9, 78.2))) * 43758.5));
        } else if (tone == 1) {     // tree fern: fibrous trunk with oval frond scars
            vec2 g = vec2(q.x * 5.0 + q.y * 2.2, q.y * 4.0);
            vec2 f = fract(g) - 0.5;
            float scar = smoothstep(0.22, 0.12, length(f * vec2(1.0, 1.6)));
            albedo *= mix(1.0, 0.55, scar) * (0.85 + 0.3 * fract(q.x * 37.0 + sin(q.y * 23.0)));
        } else if (tone == 2) {     // palm: horizontal leaf-base rings
            albedo *= 0.85 + 0.2 * smoothstep(0.0, 0.25, abs(fract(q.y * 6.0) - 0.5));
        }
    }
    vec3 N = normalize(v_normal);
    if (u_bake == 1) {
        o = vec4(albedo, 1.0); o_n = vec4(N * 0.5 + 0.5, v_ao);
        return;
    }
    // Leaf to leaf: +-12% value and a slight yellow/blue shift.
    float lv = v_var * 2.0 - 1.0;
    albedo *= mix(vec3(1.0), (1.0 + 0.12 * lv) * vec3(1.0 + 0.04 * lv, 1.0, 1.0 - 0.05 * lv), v_leaf);
    // Ground cover: blades darken toward their base, where the sward hides
    // the sky (the baked occlusion is per plant, this is per stand).
    float ao = v_ao;
    if (u_understory > 0.0) ao *= mix(0.5, 1.0, smoothstep(0.0, 0.5, v_world.y - v_base.y));
    o = vec4(veg_shade(albedo, N, ao, v_leaf, v_world, v_base, 0.0), 1.0); o_n = vec4(0.0);
}
