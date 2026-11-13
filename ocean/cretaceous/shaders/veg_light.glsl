// Foliage and bark lighting: soft wrapped diffuse on crown-bent normals,
// transmission when backlit, hemisphere sky/ground light scaled by baked
// crown occlusion, terrain/cloud/cascade shadows, contact shadows under
// animals. No specular lobe: a leaf card or frond strip has one normal, so
// any highlight lights the whole strip at once and reads as plastic. Rain
// only adds a faint, broad sky sheen.
uniform sampler2D t_trans, t_skyview, t_horizon0, t_horizon1, t_weather, t_misc;
uniform float u_understory;   // 1 for ground cover (canopy-shaded sky light), 0 for trees and impostors
uniform vec3 u_cam, u_sun;
uniform vec2 u_map;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame;
vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }
#include "shadow.glsl"
#include "csm.glsl"
// albedo, N (bent toward the crown's outside), ao (baked crown occlusion:
// low deep inside and underneath), leaf 0 bark .. 1 leaf, p world position,
// base the plant's foot, card 1 for an impostor (0 for a mesh).
vec3 veg_shade(vec3 albedo, vec3 N, float ao, float leaf, vec3 p, vec3 base, float card) {
    vec3 L = u_sun, V = normalize(u_cam - p);
    // Leaves a touch more saturated under a high sun: pale photo greens under
    // a blue sky otherwise read as mint in full sun. Barely with a low sun,
    // when the shaded crowns would turn emerald. The atlas grade (veg.c)
    // already made them deeper and more varied.
    float sat = mix(0.05, 0.2, smoothstep(0.15, 0.55, L.y));
    albedo = max(mix(vec3(dot(albedo, vec3(0.3, 0.59, 0.11))), albedo, 1.0 + sat * leaf), 0.0);
    // Deeper and more olive than the photos graded for an even green cover: against bare soil the leaves
    // read as dark shapes on light ground, not a lime sheet.
    albedo *= mix(vec3(1.0), vec3(0.7, 0.74, 0.62), leaf);
    // Rain: bark soaks dark, leaves darken a little.
    albedo *= mix(1.0, mix(0.6, 0.85, leaf), u_wet);
    float shadow = terrain_sun(map_uv(base.xz), p, L);
    // Sun shadow maps: leaves get the softer, sun-biased lookup. Light that
    // passes through leaves is only partly blocked by other leaves.
    float cs = csm_shadow(p, N, L, length(u_cam - p), leaf);
    if (u_csm_debug == 1) return csm_debug_tint(p) * (0.15 + cs * 2.0) * (leaf > 0.5 ? vec3(0.6, 1.0, 0.6) : vec3(1.0));
    vec3 sun = sun_transmittance(t_trans, p.y, L) * SUN_INTENSITY;
    float nl = dot(N, L);
    // Leaves: squared half-Lambert. A crown then shades as one soft volume
    // (bright sun side, gentle falloff, dark far side) instead of flat cards.
    float hl = nl * 0.5 + 0.5;
    float ndl = mix(max(nl, 0.0), hl * hl, leaf);
    // Inner crown sees less sun. With shadow maps the leaves above do most of
    // that on a mesh, so the baked occlusion only adds a little on top. An
    // impostor is one flat card that cannot shadow its own crown, so it takes
    // the whole baked term (with less it drew far crowns lighter than near).
    // The same holds for an impostor's trunk under its crown, which the card's
    // shadow cannot reach either.
    float self = mix(1.0, smoothstep(0.3, 0.95, ao), max(leaf * (u_csm_count > 0 ? mix(0.45, 1.0, card) : 1.0), card));
    // Transmission: sun through the leaf blade, strongest looking toward the
    // sun and on blades facing away from it; tinted by the blade (yellow-green).
    float back = (pow(max(dot(-V, L), 0.0), 4.0) * 0.8 + max(-nl, 0.0) * 0.25) * leaf * self;
    vec3 trans_col = mix(vec3(1.0), albedo * vec3(4.0, 4.6, 2.2), 0.6);
    // Ground contact: animals shade the sky above the ground cover and trunks.
    float contact = contact_occlusion(p);
    vec3 sky_up = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L);
    vec3 sky_h = sky_radiance(t_skyview, normalize(vec3(-L.x, 0.15, -L.z)), L);
    // Hemisphere: up-facing blades see the sky, down-facing ones the dark
    // forest floor; the crown's own depth (ao) hides both.
    vec3 ground = sky_h * vec3(0.3, 0.34, 0.24);
    vec3 amb = mix(ground, mix(sky_h, sky_up, max(N.y, 0.0)), smoothstep(-0.6, 0.4, N.y)) * 1.15 * ao * contact;   // less fill: the shaded side and the inside of a plant fall properly dark
    // Ground cover: neighbouring blades hide much of the sky as well (more
    // than one plant's baked occlusion), so a sunlit sward keeps dark gaps.
    if (u_understory > 0.0) amb *= canopy_sky(texture(t_misc, map_uv(base.xz)).a * u_understory) * mix(1.0, 0.8, u_understory);
    // Sky light reaching a leaf has mostly bounced through other leaves
    // first, which filters it green (and keeps sunlit ferns from going mint
    // under a blue sky). Gentler with a low sun: in the shade at golden hour
    // the full filter turned crowns a saturated emerald.
    amb *= mix(vec3(1.0), mix(vec3(0.84, 0.96, 0.76), vec3(0.74, 1.0, 0.56), smoothstep(0.15, 0.55, L.y)), leaf * 0.75);
    // Ground bounce lights undersides and trunks (forest floor albedo ~0.15).
    vec3 bounce = sun * max(L.y, 0.0) * 0.15 * (0.5 - 0.5 * N.y) * mix(0.6, 1.0, shadow) / PI * contact;
    vec3 col = albedo / PI * sun * shadow * (ndl * self * cs + back * trans_col * 0.7 * mix(0.3, 1.0, cs)) + albedo * (amb + bounce * ao);
    // Rain: a faint, broad sheen of grey sky on wet leaves and bark at grazing angles.
    float nv = abs(dot(N, V));
    col += mix(sky_h, sky_up, 0.6) * (u_wet * 0.05 * pow(1.0 - nv, 3.0) * ao);
    return aerial(col, t_skyview, u_cam, p, L);
}
