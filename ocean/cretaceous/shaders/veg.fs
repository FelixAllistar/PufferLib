#version 330
#include "common.glsl"
#include "veg_light.glsl"
in vec3 v_world, v_normal, v_base, v_rel;
in vec2 v_uv;
in float v_ao, v_leaf, v_hue, v_fade, v_w, v_var;
in vec3 v_cell;
flat in float v_layer;
flat in float v_bark;
// Bark tone per species on top of the per-species bark layer (photo or
// painted, already coloured): default, tree fern, palm, conifer, cycad,
// angiosperm, then (render/veg_gen.h) twig, frond rachis, cone, crozier,
// untinted (fruit, flowers, fungi: their own atlas colours), dead wood, plane (mottled, below).
// Conifers and angiosperms are 1: their bark layers are by species and already carry its colour.
const vec3 BARK[13] = vec3[13](vec3(0.85, 0.78, 0.68), vec3(1.0), vec3(0.88, 0.8, 0.68), vec3(1.0), vec3(0.78, 0.72, 0.62), vec3(1.0),
                               vec3(0.8, 0.72, 0.6), vec3(0.78, 0.86, 0.52), vec3(0.85, 0.62, 0.42), vec3(0.72, 0.64, 0.46), vec3(1.0), vec3(1.0), vec3(1.0));
uniform sampler2DArray t_atlas;
uniform int u_bake;
uniform float u_edge_fade;   // 1: leaf cards erode away edge on and glow by their own normal (render/veg_look.h VEG_LOOK_EDGE)
// Leaf material atlas (render/veg.c atlas_store), layer for layer with t_atlas: RG the surface's tilt
// along +u and +v of the layer, B the share of light from behind it lets through (thin blade high, veins
// and stems low). u_leaf_detail: x 1 lights through the tilt, y 1 lets light through by B (render/veg.h).
uniform sampler2DArray t_atlas_mat;
uniform vec2 u_leaf_detail;
// How much steeper than scanned leaf and bark tilts are taken (see main): bark furrows are shallow in the
// scans and a trunk is mostly in shade, lit by the sky, so it takes more.
const float LEAF_TILT_GAIN = 1.6, LEAF_TILT_BARK = 1.8;
// Tilts N by t (along +u, +v of the atlas layer) in a frame from the screen derivatives of the position
// and the atlas coordinates (the meshes carry no tangents). dp and duv: (dFdx, dFdy) of each.
vec3 detail_normal(vec3 N, vec2 t, vec3 dpx, vec3 dpy, vec2 dux, vec2 duy) {
    vec3 a = cross(dpy, N), b = cross(N, dpx);
    vec3 T = a * dux.x + b * duy.x, B = a * dux.y + b * duy.y;
    float s = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-24));
    return normalize(T * (s * t.x) + B * (s * t.y) + N * sqrt(max(1.0 - dot(t, t), 0.0)));
}
layout(location = 0) out vec4 o;
layout(location = 1) out vec4 o_n;
void main() {
    vec4 a = texture(t_atlas, vec3(v_uv, v_layer));
    // Fuller leaves: the cut is lower than half, and rises with the mip level, so thin pinnae and blades keep
    // their width up close and do not thin away to wisps with distance (veg_depth.fs cuts the same).
    vec2 atx = v_uv * 256.0; float alod = 0.5 * log2(max(dot(dFdx(atx), dFdx(atx)), dot(dFdy(atx), dFdy(atx))));   // mip level, as veg_shadow.fs
    // A leaf card seen nearly edge on erodes away instead of drawing as a line (render/veg_look.h); each
    // card leaves at its own angle (v_var). veg_depth.fs cuts the same.
    vec3 ng = veg_card_normal(v_rel);
    // The leaf material and the derivatives for its frame, read before any pixel of the quad is discarded.
    vec4 mt = texture(t_atlas_mat, vec3(v_uv, v_layer));
    vec3 dpx = dFdx(v_rel), dpy = dFdy(v_rel); vec2 dux = dFdx(v_uv), duy = dFdy(v_uv);
    float keep = u_edge_fade > 0.0 && v_leaf > 0.5 && u_bake == 0 ? veg_edge_keep(veg_card_nv(ng, v_rel), v_var) : 1.0;
    if (a.a * keep * (1.0 + 0.35 * max(alod, 0.0)) < 0.32) discard;
    // fade in [0,1]: keep where noise <= fade. fade in [-2,-1]: LOD partner,
    // keep the complementary half (noise > -fade - 1).
    if (u_bake == 0) {
        float nz = veg_fade_key(v_cell);
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
        albedo *= BARK[clamp(tone, 0, 12)];
        // Dead wood: bark weathers grey and pale, with dark checks along the grain.
        if (tone == 11) albedo = mix(vec3(dot(albedo, vec3(0.3, 0.59, 0.11))), albedo, 0.3) * vec3(1.18, 1.14, 1.08) * (0.8 + 0.2 * smoothstep(0.1, 0.3, abs(fract(v_uv.x * 9.0 + sin(v_uv.y * 3.0) * 0.3) - 0.5)));
        // Trunk surface patterns from the tube uv (u around, v along, in m).
        vec2 q = v_uv;
        if (tone == 4) {            // cycad: spiral armour of diamond leaf bases
            vec2 g = vec2(q.x * 7.0 + q.y * 5.0, q.x * 7.0 - q.y * 5.0);
            vec2 f = abs(fract(g) - 0.5);
            // Each leaf base a soft dark-rimmed pad, not a wire net: a broad, shallow rim, and the whole pattern
            // fades out as it gets smaller than a few pixels (it drew as a crawling mesh on far trunks).
            float edge = smoothstep(0.26, 0.5, max(f.x, f.y)), fine = clamp(1.0 - 3.0 * max(fwidth(g.x), fwidth(g.y)), 0.0, 1.0);
            albedo *= mix(1.0, mix(1.0, 0.68, edge) * (0.9 + 0.2 * fract(sin(dot(floor(g), vec2(12.9, 78.2))) * 43758.5)), fine) * mix(0.84, 1.0, fine);
        } else if (tone == 1) {     // tree fern: fibrous trunk with oval frond scars
            vec2 g = vec2(q.x * 5.0 + q.y * 2.2, q.y * 4.0);
            vec2 f = fract(g) - 0.5;
            float scar = smoothstep(0.22, 0.12, length(f * vec2(1.0, 1.6))), fine = clamp(1.0 - 3.0 * max(fwidth(g.x), fwidth(g.y)), 0.0, 1.0);
            albedo *= mix(1.0, mix(1.0, 0.62, scar) * (0.88 + 0.24 * fract(q.x * 37.0 + sin(q.y * 23.0))), fine) * mix(0.86, 1.0, fine);
        } else if (tone == 12) {    // plane: bark shed in plates, a camouflage of cream, grey-green and fawn patches
            // In metres round the plant (no seam where u wraps), stretched along the trunk; two octaves of
            // 15-40 cm patches with sharp edges. They stay as large shapes at a distance, as on a real plane.
            vec3 m = (v_world - v_base) * vec3(4.0, 2.2, 4.0);
            float n1 = 0.5 * (vnoise2(m.xy) + vnoise2(m.zy + 17.0)), n2 = 0.5 * (vnoise2(m.xy * 2.3 + 5.0) + vnoise2(m.zy * 2.3 + 31.0));
            float pa = smoothstep(0.5, 0.54, n1 * 0.7 + n2 * 0.3), pb = smoothstep(0.56, 0.6, n2 * 0.65 + n1 * 0.35);
            float l = dot(albedo, vec3(0.3, 0.59, 0.11));
            albedo = mix(albedo, l * vec3(1.75, 1.72, 1.45), pa);    // fresh cream inner bark
            albedo = mix(albedo, l * vec3(1.15, 1.2, 0.95), pb * (1.0 - pa));   // grey-green
        } else if (tone == 2) {     // palm: horizontal leaf-base rings
            albedo *= 0.85 + 0.2 * smoothstep(0.0, 0.25, abs(fract(q.y * 6.0) - 0.5));
        }
    }
    vec3 N = normalize(v_normal);
    // Ground cover is lit as a low mass of blades, not card by card: a two-sided frond seen from below
    // otherwise faced the dark floor and drew as a black cut-out on lit soil.
    if (u_understory > 0.0 && u_bake == 0) N = normalize(mix(N * (dot(N, u_cam - v_world) < 0.0 ? -1.0 : 1.0), vec3(0.0, 1.0, 0.0), 0.6));
    // The scans' surface: leaflets, veins, folds and bark furrows tilt the normal, after the ground-cover mix
    // above, so a fern frond still shades leaflet by leaflet (the material and derivatives are read at the top).
    // Leaves take their tilt a little steeper than scanned: the wrapped, sky-heavy foliage light flattens small
    // tilts, and a fern frond lit by a high sun otherwise shades as one plane. Where the scan's surface turns
    // steeply away (a leaflet's curled edge, the folds either side of a midrib, a bark furrow) it also sees
    // less light: a cavity term from the same tilt darkens it. Both fade with distance as the mips average
    // the tilts out, and impostors bake them in.
    if (u_leaf_detail.x > 0.0) {
        vec2 tl = mt.xy * 2.0 - 1.0; float tn = length(tl);
        tl *= min(mix(LEAF_TILT_BARK, LEAF_TILT_GAIN, v_leaf), 0.9 / max(tn, 1e-4));
        N = detail_normal(N, tl, dpx, dpy, dux, duy);
        albedo *= 1.0 - 0.5 * smoothstep(0.12, 0.7, tn) * mix(0.35, 1.0, v_leaf);   // bark scans carry their furrow shade already
    }
    veg_trans = u_leaf_detail.y > 0.0 ? mt.b : 1.0;   // a thin blade lets all of it through, as before; veins and stems less
    if (u_bake == 1) {
        o = vec4(albedo, 1.0); o_n = vec4(N * 0.5 + 0.5, v_ao);
        return;
    }
    // Leaf to leaf: +-12% value and a slight yellow/blue shift.
    float lv = v_var * 2.0 - 1.0;
    albedo *= mix(vec3(1.0), (1.0 + 0.12 * lv) * vec3(1.0 + 0.04 * lv, 1.0, 1.0 - 0.05 * lv), v_leaf * (1.0 - 0.3 * u_understory));   // a sward reads as one mass: less blade-to-blade spread
    // Ground cover: blades darken toward their base, where the sward hides
    // the sky (the baked occlusion is per plant, this is per stand).
    float ao = v_ao;
    if (u_understory > 0.0) ao *= mix(0.38, 1.0, smoothstep(0.0, 0.7, v_world.y - v_base.y));   // dark at the foot of each plant
    // Sun through the blade: the card's own normal says which face the sun is on, so a leaf seen from
    // its shaded face glows, whichever way the bent normal leans (-1: veg_shade judges by the bent normal).
    float thru = -1.0;
    if (u_edge_fade > 0.0 && dot(ng, ng) > 0.0) { vec3 nu = normalize(ng); thru = clamp(-dot(nu, u_sun) * sign(dot(nu, -v_rel)), 0.0, 1.0); }
    o = vec4(veg_shade(albedo, N, ao, v_leaf, v_world, v_base, 0.0, thru), 1.0); o_n = vec4(0.0);
}
