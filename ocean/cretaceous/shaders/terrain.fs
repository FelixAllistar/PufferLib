#version 330
#include "common.glsl"
in vec3 v_world;
in float v_w;
out vec4 o;
uniform sampler2D t_height, t_normal, t_horizon0, t_horizon1, t_splat0, t_splat1, t_misc, t_water, t_noise, t_trans, t_skyview, t_weather, t_canopy, t_cover, t_biome;
uniform float u_veg_far;   // plant impostor range (m); the terrain paints the canopy beyond it
uniform float u_cover_range;   // ground-cover mesh range (m); the terrain carries the understorey beyond it
uniform sampler2DArray t_mat_a, t_mat_b;
uniform vec3 u_cam, u_sun;
uniform vec2 u_map;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame;
uniform int u_splat_debug;   // CRET_SPLAT_DEBUG=1: flat colour per material (rock scree dirt forest fern mud sand moss); 2, 3: t_cover (below)
uniform int u_detail;   // quality preset: 2 parallax + 4 materials, 1 no parallax, 0 also 2 materials and one triplanar scale

vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }

// Metres per texture tile, per material: rock scree dirt forest fern mud sand moss.
#ifndef CRET_UNDER_NEAR
#define CRET_UNDER_NEAR 0.55
#endif
const float TILE[8] = float[8](7.0, 2.6, 3.2, 2.8, 3.0, 2.6, 4.0, 4.5);
const bool TRIPLANAR[8] = bool[8](true, true, false, false, false, false, false, true);
// Living ground cover: ferns, mosses and litter read green, as they would
// under a Cretaceous canopy (photo sources are mostly brown leaf litter).
const vec3 TINT[8] = vec3[8](vec3(1.0), vec3(1.0), vec3(1.0), vec3(0.72, 0.66, 0.54), vec3(0.68, 0.7, 0.57), vec3(1.0), vec3(2.2, 2.05, 1.7), vec3(0.85, 0.9, 0.8));   // forest floor: brown litter; fern ground: litter with some green; sand: the photo is damp, dry beach sand is pale
// Fern and moss photos are khaki; keep only their luminance detail and take
// the hue from TINT (living fern albedo has green about twice red). The
// detail is stretched around the photo's mean luminance (RECOLOR_MEAN), so
// the sward reads as fronds and shadowed gaps, not a pale felt.
const float RECOLOR[8] = float[8](0.0, 0.0, 0.0, 0.0, 0.9, 0.0, 0.0, 0.0);
const vec3 RECOLOR_HUE = vec3(0.27, 0.5, 0.2);    // living fern sward per unit luminance (deep green, not lime)
const float RECOLOR_MEAN = 0.19, RECOLOR_CONTRAST = 1.6;

// t_mat_a: albedo (sRGB) + height in alpha. t_mat_b (DXT5nm): rough, normal y, AO, normal x.
struct Mat { vec3 albedo; float height; vec3 n; float rough; float ao; };
// Set once per pixel in main (uniform control flow): the world xz gradients
// for explicit-gradient lookups, the smooth anti-tiling warp in metres and
// the share of the rotated tile copy (sample_mat).
vec2 g_dx, g_dy, g_warp;
float g_mix;

// t_noise wraps every `period` metres of world space at the scale it is read
// at. Where its pattern is large enough to see from a distance, two lookups
// at periods 1 : 1.618 on axes 37 degrees apart are averaged (contrast
// restored to one lookup's): the sum never repeats, so no lattice of
// identical patches lines up across a slope. The low preset reads one.
vec4 noise_qp(vec2 xz, float period, float off) {
    vec4 a = texture(t_noise, xz / period + off);
    if (u_detail == 0) return a;
    vec4 b = texture(t_noise, mat2(0.8, -0.6, 0.6, 0.8) * xz / (period * 1.618) + off + 0.29);
    return 0.5 + ((a + b) * 0.5 - 0.5) * 1.41;
}

Mat sample_mat(int k, vec3 p, vec3 nrm, float dist) {
    float s = 1.0 / TILE[k];
    Mat m;
    // Steep ground is triplanar for every material: a planar projection
    // stretches into streaks down the fall line.
    if ((TRIPLANAR[k] && nrm.y < 0.92) || nrm.y < 0.78) {
        vec3 w = pow(abs(nrm), vec3(4.0)); w /= dot(w, vec3(1.0));
        // Drop projections that contribute under ~3%: most cliff pixels then
        // sample two planes, not three (6 of 9 fetches per layer).
        w = max(w - 0.03, 0.0); w /= dot(w, vec3(1.0));
        // A second projection breaks the tile grid. On rock it is 3.7x larger
        // and always half mixed in: cliff rock reads at any scale and the
        // large copy gives faces their big structure. Other materials on
        // steep ground take the planar branch's scheme (below): the same
        // tile, rotated, under the noise mask, since magnified litter and
        // soil photos read as brush strokes.
        bool rock = k == 0;
        float s2 = rock ? s * 0.27 : s, wm = rock ? 0.45 : g_mix; bool macro = u_detail > 0 && wm > 0.0;
        const mat2 R = mat2(0.8, -0.6, 0.6, 0.8);
        vec4 a = vec4(0.0), a2 = vec4(0.0), bx = vec4(0.0), by = vec4(0.0), bz = vec4(0.0);
        if (w.x > 0.0) {
            a += texture(t_mat_a, vec3(p.zy * s, k)) * w.x; bx = texture(t_mat_b, vec3(p.zy * s, k));
            if (macro) a2 += texture(t_mat_a, vec3(R * p.zy * s2 + 0.31, k)) * w.x;
        }
        if (w.y > 0.0) {
            a += texture(t_mat_a, vec3(p.xz * s, k)) * w.y; by = texture(t_mat_b, vec3(p.xz * s, k));
            if (macro) a2 += texture(t_mat_a, vec3(R * p.xz * s2, k)) * w.y;
        }
        if (w.z > 0.0) {
            a += texture(t_mat_a, vec3(p.xy * s, k)) * w.z; bz = texture(t_mat_b, vec3(p.xy * s, k));
            if (macro) a2 += texture(t_mat_a, vec3(R * p.xy * s2 + 0.57, k)) * w.z;
        }
        if (macro) a = mix(a, a2, wm);
        // Whiteout-blended triplanar normals into world space.
        vec2 nx = bx.wy * 2.0 - 1.0, ny = by.wy * 2.0 - 1.0, nz = bz.wy * 2.0 - 1.0;
        vec3 tx = vec3(0.0, nx.y, nx.x) * sign(nrm.x), ty = vec3(ny.x, 0.0, ny.y), tz = vec3(nz.x, nz.y, 0.0) * sign(nrm.z);
        m.n = normalize(nrm + tx * w.x + ty * w.y + tz * w.z);
        m.albedo = a.rgb; m.height = a.a; m.rough = bx.x * w.x + by.x * w.y + bz.x * w.z; m.ao = bx.z * w.x + by.z * w.y + bz.z * w.z;
    } else {
        // Tiling is hidden by a second copy of the tile at the same scale,
        // rotated 37 degrees and read through a smooth warp, mixed in by a
        // noise mask a few metres across (g_mix): no tile corner lines up
        // with the next, and no period lines up into rows. (A copy magnified
        // 3.4x used to do this; on the litter photos it blew twigs up into
        // metre-long strokes that read as brush marks at 50-150 m.) Far off,
        // the mips average the photo to its mean colour and the landform and
        // cover vary the ground instead. Explicit gradients: the material
        // loop is not uniform control flow.
        vec2 uv = p.xz * s, gx = g_dx * s, gy = g_dy * s;
        vec4 a = textureGrad(t_mat_a, vec3(uv, k), gx, gy), b = textureGrad(t_mat_b, vec3(uv, k), gx, gy);
        if (g_mix > 0.0) {
            const mat2 R = mat2(0.8, -0.6, 0.6, 0.8);
            vec4 a2 = textureGrad(t_mat_a, vec3(R * (p.xz + g_warp) * s + 0.37, k), R * gx, R * gy);
            a = mix(a, a2, g_mix);
        }
        m.albedo = a.rgb; m.height = a.a;
        // Detail normals fade as their relief falls under a pixel; past that
        // they only add sparkle and a flat sheen at grazing angles.
        vec2 tn = (b.wy * 2.0 - 1.0) * 1.2 * (1.0 - 0.7 * smoothstep(60.0, 300.0, dist));
        vec3 t = normalize(vec3(1.0, 0.0, 0.0) - nrm * nrm.x), bt = cross(nrm, t);
        m.n = normalize(nrm + t * tn.x - bt * tn.y);
        m.rough = b.x; m.ao = b.z;
    }
    return m;
}

#include "shadow.glsl"
#include "csm.glsl"

// Parallax occlusion on the dominant planar material near the camera: march
// its height map along the view ray, shift every material's lookup by the
// hit offset, then march toward the sun for contact self-shadowing.
// Relief depth in metres per material (rock scree dirt forest fern mud sand moss).
const float RELIEF[8] = float[8](0.0, 0.12, 0.05, 0.05, 0.05, 0.04, 0.025, 0.06);
float pom_height(int k, vec2 xz, vec2 gx, vec2 gy) {
    float s = 1.0 / TILE[k];
    return textureGrad(t_mat_a, vec3(xz * s, k), gx * s, gy * s).a;
}
vec2 pom(int k, vec2 xz, vec3 V, float fade, out float hit_h) {
    vec2 gx = dFdx(xz), gy = dFdy(xz);
    float depth = RELIEF[k] * fade;
    vec2 dir = -V.xz / max(V.y, 0.25) * depth;     // world xz shift for a full-depth hit
    const int STEPS = 14;
    float layer = 1.0 / float(STEPS), cur = 0.0;
    vec2 off = vec2(0.0); float h = pom_height(k, xz, gx, gy), prev_h = h, prev_cur = 0.0;
    for (int i = 0; i < STEPS && cur < 1.0 - h; i++) {
        prev_h = h; prev_cur = cur;
        cur += layer; off = dir * cur;
        h = pom_height(k, xz + off, gx, gy);
    }
    // Linear refinement between the last two samples.
    float a = (1.0 - h) - cur, b = (1.0 - prev_h) - prev_cur;
    float t = clamp(b / max(b - a, 1e-4), 0.0, 1.0);
    float c = mix(prev_cur, cur, t);
    hit_h = 1.0 - c;
    return dir * c;
}
float pom_shadow(int k, vec2 xz, float h0, vec3 L, float fade) {
    if (L.y <= 0.02 || fade <= 0.0) return 1.0;
    vec2 gx = dFdx(xz), gy = dFdy(xz);
    float depth = RELIEF[k] * fade;
    vec2 dir = L.xz / max(L.y, 0.15) * depth;
    float occ = 0.0;
    for (int i = 1; i <= 6; i++) {
        float t = float(i) / 6.0 * (1.0 - h0);
        float h = pom_height(k, xz + dir * t, gx, gy);
        occ = max(occ, (h - (h0 + t)) * 8.0);
    }
    return 1.0 - clamp(occ, 0.0, 1.0) * 0.7;
}

void main() {
    vec3 p = v_world;
    vec3 V = u_cam - p; float dist = length(V); V /= dist;
    vec2 uv = map_uv(p.xz);
    g_dx = dFdx(p.xz); g_dy = dFdy(p.xz);
    g_warp = u_detail > 0 ? (texture(t_noise, p.xz / 610.0 + 0.13).rg - 0.5) * 70.0 : vec2(0.0);   // the low preset skips the warp
    g_mix = smoothstep(0.42, 0.58, noise_qp(p.xz, 6.3, 0.47).r) * mix(0.4, 1.0, smoothstep(12.0, 40.0, dist));
    vec4 nt = texture(t_normal, uv);
    vec2 nxz = nt.xy * 2.0 - 1.0;
    vec3 N = normalize(vec3(nxz.x, sqrt(max(0.0, 1.0 - dot(nxz, nxz))), nxz.y));
    // Baked AO is 1 - sin(horizon) per direction, which ignores the surface's own
    // tilt and goes near black on steep faces; keep a sky floor.
    float ao = mix(0.45, 1.0, nt.z), hard = nt.w;
    // Macro variation: break up large-scale uniformity.
    vec4 nz1 = texture(t_noise, p.xz / 420.0), nz2 = texture(t_noise, p.xz / 97.0 + 0.3);
    // Splat weights with a little noise so material borders are irregular.
    vec2 juv = uv + (nz2.xy - 0.5) * 3.0 / u_map.x;
    vec4 s0 = texture(t_splat0, juv), s1 = texture(t_splat1, juv);
    float w[8] = float[8](s0.x, s0.y, s0.z, s0.w, s1.x, s1.y, s1.z, s1.w);
    if (u_splat_debug == 1) {
        const vec3 DC[8] = vec3[8](vec3(0.5), vec3(0.85), vec3(0.6, 0.35, 0.15), vec3(0.1, 0.35, 0.1), vec3(0.45, 0.9, 0.2), vec3(0.3, 0.2, 0.1), vec3(0.95, 0.85, 0.5), vec3(0.2, 0.6, 0.6));
        vec3 c = vec3(0.0); for (int k = 0; k < 8; k++) c += DC[k] * w[k];
        o = vec4(c * (0.4 + 0.6 * max(dot(N, u_sun), 0.0)), 1.0); return;
    }
    // CRET_SPLAT_DEBUG=2: t_cover, understorey density (red), its woody share
    // (green) and drainage wetness (blue); 3: curvature (grey, hollows dark).
    if (u_splat_debug >= 2) {
        vec4 cv = texture(t_cover, uv);
        o = vec4((u_splat_debug == 2 ? cv.rgb : cv.aaa) * (0.4 + 0.6 * max(dot(N, u_sun), 0.0)), 1.0); return;
    }
    // Parallax on the dominant layer (planar ones only; cliffs are triplanar).
    int dom = 0; for (int k = 1; k < 8; k++) if (w[k] > w[dom]) dom = k;
    float pfade = float(u_detail >= 2) * (1.0 - smoothstep(18.0, 35.0, dist)) * step(0.0, RELIEF[dom] - 0.001) * smoothstep(0.85, 0.95, N.y);
    vec3 ps = p; float pom_h = 1.0, self_sh = 1.0;
    if (pfade > 0.0) {
        ps.xz += pom(dom, p.xz, V, pfade, pom_h);
        self_sh = pom_shadow(dom, ps.xz, pom_h, u_sun, pfade);
    }
    // Height-aware blend: materials with taller relief win at the edges.
    // Only the four largest splat weights are sampled (more than four above
    // 0.02 is rare), and a layer whose weight is more than 0.85 below the top
    // one can never win the blend (hsum <= w + 0.6 < best - 0.25), so it is
    // skipped exactly. Fewer live materials keep register use low.
    ivec4 id = ivec4(0); vec4 ww = vec4(-1.0);
    for (int k = 0; k < 8; k++) {
        float x = w[k];
        if (x <= ww.w) continue;
        if (x > ww.x) { ww = vec4(x, ww.xyz); id = ivec4(k, id.xyz); }
        else if (x > ww.y) { ww = vec4(ww.x, x, ww.yz); id = ivec4(id.x, k, id.yz); }
        else if (x > ww.z) { ww.zw = vec2(x, ww.z); id.zw = ivec2(k, id.z); }
        else { ww.w = x; id.w = k; }
    }
    // The running best only grows, so testing each layer against it as the
    // layers are sampled (largest weight first) is still exact, and skips
    // more than the fixed bound: the top layer's own height raises it.
    Mat mats[4]; float best = 0.0; vec4 hsum = vec4(-1.0);
    int nmat = u_detail > 0 ? 4 : 2;
    // Fine noise per layer makes borders ragged (tufts of one material
    // poking into the next) instead of a smooth contour line.
    vec2 ra = texture(t_noise, p.xz / 1.9 + 0.17).rg, rb = texture(t_noise, p.xz / 2.7 + 0.53).rg;   // R/G: fbm, ~0.5 +- 0.15
    vec2 rc = noise_qp(p.xz, 9.0, 0.71).rg;   // and coarser, so mid-distance borders are ragged too
    vec4 rag = (vec4(ra, rb) - 0.5) * 0.7 + (vec4(rc, rc.yx) - 0.5) * 0.6;
    for (int j = 0; j < 4; j++) {
        if (j < nmat && (j == 0 || (ww[j] > 0.02 && ww[j] + 0.6 + 0.17 > best - 0.3))) { mats[j] = sample_mat(id[j], ps, N, dist); hsum[j] = ww[j] + mats[j].height * 0.6 + rag[j]; best = max(best, hsum[j]); }
    }
    // Within a few metres the recolour is eased: there the drawn ferns stand
    // on the ground, and under them the photo's litter stays brown with its
    // moss green, instead of every twig and leaf turning bright green.
    // The plants now stand in patches with open ground between (veg_place.h), so up close the ground is
    // what the photos show, brown litter and soil with a little moss; it only goes green with distance,
    // where the patches of plants are no longer drawn one by one.
    float recolor_near = mix(0.1, 0.8, smoothstep(8.0, 70.0, dist));
    vec3 albedo = vec3(0.0), n = vec3(0.0); float rough = 0.0, wt = 0.0, mao = 0.0;
    for (int j = 0; j < 4; j++) {
        if (hsum[j] < 0.0) continue;
        int k = id[j];
        float b = max(hsum[j] - best + 0.3, 0.0);
        float rl = RECOLOR_MEAN * pow(dot(mats[j].albedo, vec3(0.3, 0.59, 0.11)) / RECOLOR_MEAN, RECOLOR_CONTRAST);
        vec3 al = mix(mats[j].albedo, rl * RECOLOR_HUE, RECOLOR[k] * recolor_near);
        albedo += al * TINT[k] * b; n += mats[j].n * b; rough += mats[j].rough * b; mao += mats[j].ao * b; wt += b;
    }
    albedo /= max(wt, 1e-4); n = normalize(n); rough = clamp(rough / max(wt, 1e-4), 0.2, 1.0);
    // Large-scale tint: drier/redder patches, greener under canopy.
    vec4 misc = texture(t_misc, uv);
    float canopy = misc.a;
    albedo *= mix(0.82, 1.15, nz1.x) * mix(0.9, 1.08, nz2.y);
    // Living ground cover varies in patches of 5-40 m: lush dark-green
    // stands, yellower sunlit sward and bronze patches of dying fronds.
    float live = w[3] + w[4] + w[7];
    vec4 pz = texture(t_noise, p.xz / 23.0 + 0.61), pz2 = texture(t_noise, p.xz / 7.3 + 0.29);
    float pa = smoothstep(0.38, 0.62, pz.x * 0.7 + pz2.y * 0.3), pb = smoothstep(0.56, 0.68, pz.y * 0.6 + pz2.x * 0.4);
    vec3 patch_tint = mix(vec3(0.8, 0.98, 0.84), vec3(1.1, 1.05, 0.86), pa);   // dark lush .. yellow-green
    patch_tint = mix(patch_tint, vec3(1.2, 0.92, 0.66), pb * 0.55);               // bronze dying fronds
    albedo *= mix(vec3(1.0), patch_tint, clamp(live, 0.0, 1.0));
    albedo *= mix(vec3(1.0), sqrt(stand_tint(p.xz)), clamp(live, 0.0, 1.0) * 0.7);   // the hillside patchwork, gentler on the ground
    // Soil next to living cover is dark humus and frond litter, not the bare photo's orange.
    albedo = mix(albedo, albedo * vec3(0.78, 0.8, 0.62), clamp(w[2] * (w[4] + w[3]) * 3.0, 0.0, 0.7));
    // Under trees the floor is shaded litter; beyond the impostor range the
    // terrain itself carries the forest canopy colour.
    albedo = mix(albedo, albedo * vec3(0.6, 0.58, 0.46), canopy * 0.75);   // dark, brown: shaded litter
    // Biomes (t_biome): under conifers a red-brown needle litter; in dry open country bleached soil and straw.
    {
        vec4 bio = texture(t_biome, uv); float bl = dot(albedo, vec3(0.3, 0.59, 0.11));
        albedo = mix(albedo, bl * vec3(1.32, 0.86, 0.6), bio.r * 0.8);   // red-brown, a little darker than the soil
        albedo = mix(albedo, bl * vec3(1.65, 1.35, 0.78) * 1.15, bio.g * 0.8);
    }
    // Far forest. Between distant crowns the ground is the shaded floor, and
    // past the impostor range the terrain is the canopy: coloured by the
    // stand's species mix (dark blue-green conifers, yellow-green broadleaf,
    // olive palms), varied per stand and dappled per crown. t_canopy
    // (view.c build_canopy) holds that colour and the share of ground under
    // the drawn crowns, so the painted forest is as dense as the drawn one.
    vec4 cn = texture(t_canopy, uv);
    float cf = cn.a, vfar = max(u_veg_far, 10.0);
    float beyond = smoothstep(vfar * 0.8, vfar, dist);
    float gaps = smoothstep(400.0, 2000.0, dist) * (1.0 - beyond);
    float far_canopy = 0.0;
    if (cf * max(gaps, beyond) > 0.0) {
        vec3 cc = cn.rgb * 0.12 * stand_tint(p.xz);   // the drawn crowns' per-stand tint (veg.fs), so the two match
        cc *= mix(0.72, 1.3, nz1.y) * vec3(mix(0.88, 1.12, nz2.x), 1.0, mix(1.1, 0.9, nz2.x));
        float crown = texture(t_noise, p.xz / 31.0 + 0.7).g;
        cc *= mix(0.55, 1.35, smoothstep(0.25, 0.75, crown));
        albedo = mix(albedo, cc * 0.6, cf * gaps * 0.85);
        albedo = mix(albedo, cc, cf * beyond);
        far_canopy = cf * max(gaps, beyond);
        rough = mix(rough, 1.0, far_canopy);
    }
    // Past the ground-cover mesh range the fern sward is carried by the
    // terrain colour, so meadows do not turn to bare soil at 90 m.
    albedo = mix(albedo, vec3(0.05, 0.085, 0.03) * mix(0.8, 1.2, nz2.x) * patch_tint, w[4] * smoothstep(40.0, 120.0, dist) * 0.7);
    // Inside it, the soil between the sward's tufts is their shaded thatch: darker and olive, so the gaps do not
    // flash pale photo soil between every frond (the sward read as crumpled paper on a light floor).
    albedo = mix(albedo, albedo * vec3(0.56, 0.6, 0.42), clamp(w[4] + 0.8 * w[7] + 0.35 * w[3], 0.0, 1.0) * (1.0 - smoothstep(40.0, 120.0, dist)) * 0.6);
    // Past the sward ring, clumps of low plants and their shadows mottle the
    // ground at a few metres, so distant slopes are not a smooth lawn.
    // Scales grow with distance (a few metres near, shrub stands far) so the
    // mottle never averages out to a flat lawn.
    float c_s = texture(t_noise, p.xz / 4.3 + 0.41).r * 0.6 + texture(t_noise, p.xz / 1.7 + 0.13).g * 0.4;
    float c_m = noise_qp(p.xz, 13.0, 0.23).g, c_l = texture(t_noise, p.xz / 38.0 + 0.87).r;
    float clump = mix(mix(c_s, c_m, smoothstep(40.0, 140.0, dist)), c_l, smoothstep(180.0, 600.0, dist) * 0.6);
    albedo *= mix(1.0, mix(0.58, 1.18, smoothstep(0.4, 0.6, clump)), clamp(live, 0.0, 1.0) * smoothstep(18.0, 40.0, dist) * (1.0 - smoothstep(1200.0, 2500.0, dist)));
    // Landform and drainage (t_cover, mipmapped): hollows and drainage lines
    // hold moisture and humus, darker and a little greener; spurs and ridges
    // are drier, paler and rougher. Varies the ground at 20-200 m, the scale
    // a tile repeat would otherwise show at.
    vec4 cov = texture(t_cover, uv);
    float curv = clamp((cov.a - 0.5) * 2.0, -1.0, 1.0), damp = smoothstep(0.35, 0.8, cov.b);
    albedo *= (1.0 + 0.14 * curv) * mix(vec3(1.0), vec3(0.8, 0.88, 0.76), damp * 0.7);
    rough = clamp(rough + 0.12 * curv - 0.1 * damp, 0.2, 1.0);
    // Understorey past the ground-cover meshes. Ferns, horsetails, scrub and
    // cycads are drawn out to u_cover_range and dither out over its last
    // 30%; from there the ground takes on their look, so the edge of the
    // drawn plants is not a line and far forest floor is not bare soil: a
    // dark green sward (olive-brown where scrub dominates), mottled by clumps
    // and gaps, matte, and shading itself (a stand of fronds hides much of
    // its own ground from the sky and, with a low sun, from the sun).
    float rocky = clamp(w[0] + w[1] + w[6], 0.0, 1.0);
    // Near the camera too, partly: the soil between drawn plants is shaded thatch and roots, not the bare
    // photo. Pale soil showing through every gap in the cover read as speckle across the whole ground.
    float under = cov.r * (1.0 - rocky) * max(smoothstep(u_cover_range * 0.45, u_cover_range * 0.85, dist), CRET_UNDER_NEAR);
    if (under > 0.0) {
        // Clumps of fronds a few metres across with gaps between them, and
        // stands of 10-40 m. The clump noise keeps its contrast out to where
        // its mips average it away, so mid-distance cover is textured, not
        // smooth paint. Its slope tilts the shading normal: clumps facing the
        // sun light up and their far sides fall dark, as a sward does.
        // The low preset reuses the clump noise above and draws no relief.
        vec4 q = vec4(c_s, 0.5, 0.5, 0.5), q2 = vec4(0.5, c_m, 0.5, 0.5);
        if (u_detail > 0) { q = texture(t_noise, p.xz / 3.1 + 0.19); q2 = texture(t_noise, p.xz / 11.0 + 0.83); }
        float cl = q.r * 0.55 + q2.g * 0.45;
        float stand = mix(c_m, c_l, smoothstep(150.0, 600.0, dist));
        vec3 ucol = mix(vec3(0.045, 0.072, 0.03), vec3(0.05, 0.055, 0.032), cov.g) * mix(0.55, 1.2, smoothstep(0.32, 0.68, cl)) * mix(0.8, 1.15, stand)
                  * mix(vec3(1.0), patch_tint, 0.6);
        albedo = mix(albedo, ucol, under * 0.85);
        rough = mix(rough, 1.0, under);
        vec2 tilt = (q.ba - 0.5) * 2.0 * 0.8 + (q2.ba - 0.5) * 2.0 * 0.4;
        n = normalize(n + vec3(tilt.x, 0.0, tilt.y) * under);
    }
    // Rock reads grey-brown, not orange: desaturate the source photo.
    // Dry grassland seen from afar is straw too, not the green understorey painted above (not on rock or sand).
    {
        float dryf = texture(t_biome, uv).g * (1.0 - clamp(w[0] + w[1] + w[6], 0.0, 1.0)) * smoothstep(20.0, 90.0, dist);
        float dl = dot(albedo, vec3(0.3, 0.59, 0.11));
        albedo = mix(albedo, max(dl, 0.045) * vec3(1.55, 1.3, 0.72) * 1.25, dryf * 0.85);
    }
    float lum = dot(albedo, vec3(0.3, 0.55, 0.15));
    albedo = mix(albedo, vec3(lum) * vec3(1.02, 0.98, 0.93), 0.45 * (w[0] + w[1]));
    // Rock strata tint by hardness.
    albedo *= mix(vec3(1.0), mix(vec3(0.95, 0.9, 0.85), vec3(1.08, 1.0, 0.92), hard), w[0]);
    // Wet banks near water.
    float wl = texture(t_water, uv).r;
    // Narrow damp band: the extended level also sits just under flat dry shelves.
    float wet = smoothstep(0.45, 0.0, p.y - wl) * step(wl, p.y + 3.0);
    // Beaches: the swash wets a wider band of sand above the sea, with a
    // ragged upper edge (the last wave's reach).
    float sea_l = 1.0 - smoothstep(0.5, 2.0, abs(wl));
    // Up to about half a metre above the sea: on the broad 1.2 % beaches (world.c beaches_build) a metre
    // wetted 90 m of sand and the whole beach read dark brown.
    float reach = 0.45 + 0.25 * (nz2.z - 0.5);
    wet = max(wet, smoothstep(reach, 0.1, p.y - wl) * step(wl, p.y + 3.0) * sea_l * 0.8);
    albedo *= mix(1.0, 0.55, wet); rough = mix(rough, 0.18, wet);
    wet_surface(albedo, rough, 1.0 - 0.6 * (w[0] + w[6]), N.y);   // rain: rock and sand darken less
    // Lighting.
    vec3 L = u_sun;
    float shadow = 1.0;
    if (L.y > -0.05) {
        float ha = horizon_angle(uv, L), e = asin(clamp(L.y, -1.0, 1.0));
        shadow = smoothstep(ha - 0.035, ha + 0.035, e) * cloud_shadow(p, L);
        float cs = csm_shadow(p, N, L, dist, 0.0);   // plants and props
        shadow *= cs;
        // Past the shadow cascades (where csm_shadow fades to 1) the trees
        // still shade the forest floor: the share of ground in crown shadow,
        // from the canopy cover, larger with a low sun. Without it far forest
        // floor between the impostors is lit as open ground.
        float cs_end = u_csm_count > 0 ? u_csm_split[u_csm_count - 1] : 30.0;
        float shade = clamp(cf * (1.0 + 0.8 * (1.0 - max(L.y, 0.0))), 0.0, 1.0);
        shadow *= 1.0 - 0.7 * shade * smoothstep(cs_end * 0.7, cs_end, dist);
        // A frond stand hides much of its own floor from a low sun.
        shadow *= 1.0 - under * mix(0.2, 0.55, 1.0 - max(L.y, 0.0));
        if (u_csm_debug == 1) { o = vec4(csm_debug_tint(p) * (0.15 + cs * 2.0), 1.0); return; }
    }
    vec3 sun_col = sun_transmittance(t_trans, p.y, L) * SUN_INTENSITY;
    float ndl = max(dot(n, L), 0.0) * smoothstep(-0.05, 0.05, dot(N, L));
    vec3 sky_up = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L), sky_h = sky_radiance(t_skyview, normalize(vec3(L.x, 0.15, L.z) * vec3(-1.0, 1.0, -1.0)), L);
    // Animals shade the sky above the ground right under them (contact shadows).
    float contact = dist < 70.0 ? contact_occlusion(p) : 1.0;
    vec3 ambient = mix(sky_h, sky_up, n.y * 0.5 + 0.5) * 1.5 * canopy_sky(canopy) * contact;
    ambient *= 1.0 - 0.35 * under;   // and from the sky
    // Light reflected by the sunlit landscape (albedo ~0.13, green-brown) onto
    // faces that see the ground: on steep shaded faces it rivals the sky.
    vec3 bounce = sun_col * max(L.y, 0.0) * vec3(0.12, 0.14, 0.09) * (0.55 - 0.45 * n.y);
    // Material AO (crevices between stones, under litter) fades out with
    // distance as the texture detail it describes becomes sub-pixel.
    float micro_ao = mix(1.0, mao / max(wt, 1e-4), 1.0 - smoothstep(60.0, 400.0, dist));
    vec3 diffuse = albedo / PI * (sun_col * ndl * shadow * self_sh * mix(1.0, micro_ao, 0.5)) + albedo * (ambient * ao * micro_ao + bounce * contact);
    // Ground is matte: soil, litter, rock and sand keep a dim, broad lobe
    // (roughness >= 0.75 dry; the damp band at the water's edge and rain
    // lower it), with roughness-limited Fresnel so grazing views do not glare.
    // A canopy seen from afar is a volume of self-shadowing leaves: no sheen.
    float rs = max(rough, mix(0.75, 0.4, max(wet, u_wet)));
    float fres = pow(1.0 - max(dot(n, V), 0.0), 5.0);
    float F = 0.04 + (max(1.0 - rs, 0.04) - 0.04) * fres;
    float spec = ggx_spec(n, V, L, rs) * shadow * ndl * (1.0 - far_canopy);
    vec3 col = diffuse + sun_col * spec * F * 0.5;
    // Wet sheen: the grey sky mirrored in the water film, strongest at grazing angles.
    col += mix(sky_h, sky_up, 0.6) * F * u_wet * 0.3 * ao * (1.0 - far_canopy);
    col = aerial(col, t_skyview, u_cam, p, L);
    o = vec4(col, 1.0);
}
