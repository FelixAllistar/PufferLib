// Foliage and bark lighting: soft wrapped diffuse on crown-bent normals,
// transmission when backlit, hemisphere sky/ground light scaled by baked
// crown occlusion, terrain/cloud/cascade shadows, contact shadows under
// animals. No specular lobe: a leaf card or frond strip has one normal, so
// any highlight lights the whole strip at once and reads as plastic. Rain
// only adds a faint, broad sky sheen.
uniform sampler2D t_trans, t_skyview, t_horizon0, t_horizon1, t_weather, t_misc;
uniform sampler2D t_biome;    // R conifer forest, G dry open grassland (view.c build_terrain)
uniform float u_understory;   // 1 for ground cover (canopy-shaded sky light), 0 for trees and impostors
uniform vec3 u_cam, u_sun;
uniform vec2 u_map;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame;
// The look's foliage (Look studio): saturation, brightness, hue (degrees) and backlight, as offsets (0 today).
uniform vec4 u_lk_leaf;
vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }
#include "shadow.glsl"
#include "csm.glsl"
// How much of the light behind a leaf this pixel lets through, relative to the leaf's mean (veg.fs sets
// it from the material atlas; 1 elsewhere): scales the transmission below.
float veg_trans = 1.0;
// Ground bounce on bark, relative to the leaves' (veg.fs: more for trunks under their own low crown).
float veg_bounce = 1.0;
// How dead the leaf is (veg.fs, 0 green .. 1 dead): a dead frond is not filtered green like living foliage.
float veg_dry = 0.0;
// Height of the point up its plant, 0 at the foot .. 1 at the top (veg.fs, veg_imp.fs; trees only): the stand
// shade below darkens the lower crown, which the neighbouring crowns hide.
float veg_relh = 1.0;
// albedo, N (bent toward the crown's outside), ao (baked crown occlusion:
// low deep inside and underneath), leaf 0 bark .. 1 leaf, p world position,
// base the plant's foot, card 1 for an impostor (0 for a mesh), thru how squarely the sun lights the
// blade's far face (0..1, from the card's own normal; below 0: judged by N).
vec3 veg_shade(vec3 albedo, vec3 N, float ao, float leaf, vec3 p, vec3 base, float card, float thru) {
    vec3 L = u_sun, V = normalize(u_cam - p);
    // Leaves a touch more saturated under a high sun: pale photo greens under
    // a blue sky otherwise read as mint in full sun. Barely with a low sun,
    // when the shaded crowns would turn emerald. The atlas grade (veg.c)
    // already made them deeper and more varied.
    float sat = mix(0.0, 0.1, smoothstep(0.15, 0.55, L.y)) - 0.33 * u_understory;   // ground cover less: sunlit ferns and fern prairie went neon lime (round 3: -0.22 -> -0.33,
                                                                                 // and the trees' boost 0.05-0.2 -> 0-0.1: sunlit broadleaf crowns drew lime)
    albedo = max(mix(vec3(dot(albedo, vec3(0.3, 0.59, 0.11))), albedo, 1.0 + sat * leaf), 0.0);
    // Biome: in dry open country the grass and ground plants are straw and gold and the bushes olive-yellow.
    {
        float dry = texture(t_biome, map_uv(base.xz)).g * leaf;
        float l = dot(albedo, vec3(0.3, 0.59, 0.11));
        albedo = mix(albedo, l * mix(vec3(1.5, 1.32, 0.7), vec3(1.6, 1.38, 0.78), u_understory), dry * mix(0.45, 0.62, u_understory));   // golden-green, not all straw
    }
    // Deeper and more olive than the photos graded for an even green cover: against bare soil the leaves
    // read as dark shapes on light ground, not a lime sheet.
    albedo *= mix(vec3(1.0), mix(vec3(0.77, 0.81, 0.67), vec3(0.7, 0.74, 0.62), u_understory), leaf * (1.0 - 0.6 * veg_dry));   // crowns a little lighter: they drew as dark masses on the hills
    if (u_lk_leaf.xyz != vec3(0.0)) {   // the look: rotated about the grey axis for the hue, then saturation and brightness
        float a = radians(u_lk_leaf.z); const vec3 K = vec3(0.57735);
        vec3 h = albedo * cos(a) + cross(K, albedo) * sin(a) + K * dot(K, albedo) * (1.0 - cos(a));
        h = max(mix(vec3(dot(h, vec3(0.3, 0.59, 0.11))), h, 1.0 + u_lk_leaf.x), 0.0) * (1.0 + u_lk_leaf.y);
        albedo = mix(albedo, h, leaf);
    }
    // Rain: bark soaks dark, leaves darken a little.
    albedo *= mix(1.0, mix(0.6, 0.85, leaf), u_wet);
    float shadow = terrain_sun(map_uv(base.xz), p, L);
    // Sun shadow maps: leaves get the softer, sun-biased lookup. Light that
    // passes through leaves is only partly blocked by other leaves.
    float dcam = length(u_cam - p);
    float cs = csm_shadow(p, N, L, dcam, leaf);
    // Stand shade (trees, past 15-40 m): in a forest the neighbouring crowns hide the sky from a trunk and from
    // the lower part of each crown. stand_low is 1 for a trunk and the foot of a crown under a full canopy and
    // falls to 0 at the crown's top. Past the shadow cascades (800 m on high) the crowns cast no shadows on
    // each other at all, so there it also takes most of the sun from the lower crowns and the trunks: a far
    // forest was lit as evenly as a lawn, one flat green with no shade between its crowns.
    float stand_low = u_understory > 0.0 ? 0.0 : clamp(texture(t_misc, map_uv(base.xz)).a, 0.0, 1.0) * smoothstep(15.0, 40.0, dcam)
                      * mix(1.0, pow(1.0 - clamp(veg_relh, 0.0, 1.0), 1.5), leaf);
    float no_csm = u_csm_count > 0 ? smoothstep(u_csm_split[u_csm_count - 1] * 0.8, u_csm_split[u_csm_count - 1], dcam) : 1.0;
    cs *= 1.0 - 0.7 * stand_low * no_csm;
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
    float back = (pow(max(dot(-V, L), 0.0), 4.0) * 0.8 + (thru < 0.0 ? max(-nl, 0.0) : thru) * 0.25) * leaf * self * veg_trans * (1.0 + u_lk_leaf.w);
    vec3 trans_col = mix(vec3(1.0), albedo * vec3(4.0, 4.6, 2.2), 0.6);
    // Ground contact: animals shade the sky above the ground cover and trunks.
    float contact = contact_occlusion(p);
    vec3 sky_up = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L);
    vec3 sky_h = sky_radiance(t_skyview, normalize(vec3(-L.x, 0.15, -L.z)), L);
    // Hemisphere: up-facing blades see the sky, down-facing ones the dark
    // forest floor; the crown's own depth (ao) hides both.
    vec3 ground = sky_h * vec3(0.3, 0.34, 0.24);
    vec3 amb = mix(ground, mix(sky_h, sky_up, max(N.y, 0.0)), smoothstep(-0.6, 0.4, N.y)) * mix(1.35, 1.15, u_understory) * ao * contact;   // less fill: the shaded side and the inside of a plant fall properly dark
    // Ground cover: neighbouring blades hide much of the sky as well (more
    // than one plant's baked occlusion), so a sunlit sward keeps dark gaps.
    if (u_understory > 0.0) amb *= canopy_sky(texture(t_misc, map_uv(base.xz)).a * u_understory) * mix(1.0, 1.15, u_understory);   // lit like the ground it stands on (terrain.fs uses 1.5x sky)
    // Tree bark in a forest: the crowns round it hide the sky, and its sides face other trunks and the
    // understorey, not the open horizon. Judged by its own plant alone it took the whole sky and stood out of
    // a shaded forest as pale blue-grey poles at 20-60 m (round 2). The canopy closure over its foot, as the
    // floor takes it, and a little more on its sides.
    else if (leaf < 0.5) { float cnp = texture(t_misc, map_uv(base.xz)).a; amb *= canopy_sky(cnp) * mix(1.0, 0.7, cnp * (1.0 - max(N.y, 0.0))); }
    // A trunk in a stand sees the horizon through the other trunks and crowns, not open sky: its sky light
    // falls with the stand's canopy, as on the ground, the props and the animals beside it, and so does the
    // lower crown's (stand_low above). Without it the bare trunks under a forest's crowns were lit by the full
    // horizon sky and stood out of far slopes as pale grey poles. Faded in from 15 to 40 m, so the trunks you
    // stand beside keep their near shading.
    // (Merged with the near track: bark takes the canopy closure above at every distance, so this
    // term is the lower crown's alone.)
    else amb *= mix(vec3(1.0), vec3(0.42, 0.5, 0.36), stand_low);
    // Sky light reaching a leaf has mostly bounced through other leaves
    // first, which filters it green (and keeps sunlit ferns from going mint
    // under a blue sky). Gentler with a low sun: in the shade at golden hour
    // the full filter turned crowns a saturated emerald.
    // Ground cover takes a lighter share (round 3): ferns in a forest's shade, filled mostly by this sky light,
    // drew emerald against the dull floor.
    amb *= mix(vec3(1.0), mix(vec3(0.84, 0.96, 0.76), vec3(0.74, 1.0, 0.56), smoothstep(0.15, 0.55, L.y)), leaf * mix(0.75, 0.4, u_understory) * (1.0 - 0.6 * veg_dry));
    // Ground bounce lights undersides and trunks (forest floor albedo ~0.15).
    // veg_bounce: the trunks of cycads, palms and tree ferns take more (veg.fs; terrain.fs takes about three
    // times as much): standing under their own low crown, the lit ground round them is most of what shows
    // their form, and with the leaves' share they drew near black.
    vec3 bounce = sun * max(L.y, 0.0) * 0.15 * (0.5 - 0.5 * N.y) * mix(0.6, 1.0, shadow) / PI * contact * (1.0 + u_lk_bounce) * mix(veg_bounce, 1.0, leaf);
    // Shaded ground cover: light from the sunlit floor and trunks all round fills it too. Without it ferns in a
    // forest's shade drew a third as bright as the soil beside them, black cut-outs. Not in the sun, where the
    // open sward keeps its dark gaps.
    amb *= 1.0 + 1.6 * u_understory * (1.0 - shadow * cs);
    vec3 col = albedo / PI * sun * shadow * (ndl * self * cs + back * trans_col * 0.7 * mix(0.3, 1.0, cs)) + albedo * (amb + bounce * ao);
    // Sky light through the blade: from under a crown you see the leaves' lower faces, lit by the sky on
    // their upper ones; a leaf lets a fifth to a third of it through, yellow-green. Without it a crown seen
    // from beneath was a flat dark cut-out against the sky. Only when looking up at the leaf (meshes, not
    // impostors or ground cover), the crown's depth (ao) hiding the leaves deep inside.
    float below = smoothstep(0.05, 0.6, -V.y) * leaf * (1.0 - card) * (1.0 - u_understory);
    col += albedo * sky_up * vec3(1.5, 1.8, 0.75) * (1.2 * below * veg_trans * ao * contact);
    // Rain: a faint, broad sheen of grey sky on wet leaves and bark at grazing angles.
    float nv = abs(dot(N, V));
    col += mix(sky_h, sky_up, 0.6) * (u_wet * 0.05 * pow(1.0 - nv, 3.0) * ao);
    // Haze over foliage: the in-scattered light (the part aerial() adds, here aerial of black) keeps a fifth
    // of its colour (the rest grey). Over the dark crowns of a far forest the full horizon-sky blue turned the
    // stands a saturated teal (hue 180-200), where photos of forest across a valley go blue-grey with
    // distance. Its brightness is kept, so the depth cue stays. (The two calls share their lookups.)
    vec3 ins = aerial(vec3(0.0), t_skyview, u_cam, p, L);
    vec3 hz = aerial(col, t_skyview, u_cam, p, L) - ins;
    // Past 0.6-3 km the colour comes back (to 70%): there the stands are small marks on hills hazed by the
    // terrain's own (full colour) haze, and grey they drew as near-black blotches on blue hills.
    return hz + mix(vec3(dot(ins, vec3(0.3, 0.59, 0.11))), ins, mix(0.2, 0.7, smoothstep(600.0, 3000.0, dcam)));
}
