#version 330
#include "common.glsl"
in vec3 v_world;
in float v_w;
out vec4 o;
uniform sampler2D t_height, t_normal, t_horizon0, t_horizon1, t_splat0, t_splat1, t_misc, t_water, t_noise, t_trans, t_skyview, t_weather, t_vegmix;
uniform float u_veg_far;   // plant impostor range (m); the terrain paints the canopy beyond it
uniform sampler2DArray t_mat_a, t_mat_b;
uniform vec3 u_cam, u_sun;
uniform vec2 u_map;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame;
uniform int u_splat_debug;   // CRET_SPLAT_DEBUG=1: flat colour per material (rock scree dirt forest fern mud sand moss)
uniform int u_detail;   // quality preset: 2 parallax + 4 materials, 1 no parallax, 0 also 2 materials and one triplanar scale

vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }

// Metres per texture tile, per material: rock scree dirt forest fern mud sand moss.
const float TILE[8] = float[8](7.0, 2.6, 3.2, 2.8, 3.0, 2.6, 4.0, 4.5);
const bool TRIPLANAR[8] = bool[8](true, true, false, false, false, false, false, true);
// Living ground cover: ferns, mosses and litter read green, as they would
// under a Cretaceous canopy (photo sources are mostly brown leaf litter).
const vec3 TINT[8] = vec3[8](vec3(1.0), vec3(1.0), vec3(1.0), vec3(0.72, 0.85, 0.62), vec3(0.75, 0.9, 0.6), vec3(1.0), vec3(1.0), vec3(0.85, 0.9, 0.8));
// Fern and moss photos are khaki; keep only their luminance detail and take
// the hue from TINT (living fern albedo has green about twice red).
const float RECOLOR[8] = float[8](0.0, 0.0, 0.0, 0.0, 0.7, 0.0, 0.0, 0.0);
const vec3 RECOLOR_HUE = vec3(0.28, 0.43, 0.17);   // living fern sward, per unit luminance x2 (olive, not lime)

// t_mat_a: albedo (sRGB) + height in alpha. t_mat_b (DXT5nm): rough, normal y, AO, normal x.
struct Mat { vec3 albedo; float height; vec3 n; float rough; float ao; };

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
        float s2 = s * 0.27; bool macro = u_detail > 0;
        vec4 a = vec4(0.0), a2 = vec4(0.0), bx = vec4(0.0), by = vec4(0.0), bz = vec4(0.0);
        if (w.x > 0.0) {
            a += texture(t_mat_a, vec3(p.zy * s, k)) * w.x; bx = texture(t_mat_b, vec3(p.zy * s, k));
            if (macro) a2 += texture(t_mat_a, vec3(p.zy * s2 + 0.31, k)) * w.x;
        }
        if (w.y > 0.0) {
            a += texture(t_mat_a, vec3(p.xz * s, k)) * w.y; by = texture(t_mat_b, vec3(p.xz * s, k));
            if (macro) a2 += texture(t_mat_a, vec3(mat2(0.8, -0.6, 0.6, 0.8) * p.xz * s2, k)) * w.y;
        }
        if (w.z > 0.0) {
            a += texture(t_mat_a, vec3(p.xy * s, k)) * w.z; bz = texture(t_mat_b, vec3(p.xy * s, k));
            if (macro) a2 += texture(t_mat_a, vec3(p.xy * s2 + 0.57, k)) * w.z;
        }
        // A second, larger and rotated projection breaks the tile grid on cliffs.
        if (macro) a = mix(a, a2, 0.45);
        // Whiteout-blended triplanar normals into world space.
        vec2 nx = bx.wy * 2.0 - 1.0, ny = by.wy * 2.0 - 1.0, nz = bz.wy * 2.0 - 1.0;
        vec3 tx = vec3(0.0, nx.y, nx.x) * sign(nrm.x), ty = vec3(ny.x, 0.0, ny.y), tz = vec3(nz.x, nz.y, 0.0) * sign(nrm.z);
        m.n = normalize(nrm + tx * w.x + ty * w.y + tz * w.z);
        m.albedo = a.rgb; m.height = a.a; m.rough = bx.x * w.x + by.x * w.y + bz.x * w.z; m.ao = bx.z * w.x + by.z * w.y + bz.z * w.z;
    } else {
        // Two rotated scales hide tiling: detail plus a large, rotated sample.
        vec2 uv = p.xz * s;
        vec2 uv2 = mat2(0.8, -0.6, 0.6, 0.8) * p.xz * s * 0.29 + 0.37;
        vec4 a = texture(t_mat_a, vec3(uv, k)), a2 = texture(t_mat_a, vec3(uv2, k));
        vec4 b = texture(t_mat_b, vec3(uv, k));
        float far_mix = smoothstep(20.0, 160.0, dist) * 0.6 + 0.2;
        m.albedo = mix(a.rgb, a2.rgb, far_mix); m.height = mix(a.a, a2.a, 0.3);
        vec2 tn = (b.wy * 2.0 - 1.0) * 1.2;
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
    vec2 rc = texture(t_noise, p.xz / 9.0 + 0.71).rg;   // and coarser, so mid-distance borders are ragged too
    vec4 rag = (vec4(ra, rb) - 0.5) * 0.7 + (vec4(rc, rc.yx) - 0.5) * 0.6;
    for (int j = 0; j < 4; j++) {
        if (j < nmat && (j == 0 || (ww[j] > 0.02 && ww[j] + 0.6 + 0.17 > best - 0.3))) { mats[j] = sample_mat(id[j], ps, N, dist); hsum[j] = ww[j] + mats[j].height * 0.6 + rag[j]; best = max(best, hsum[j]); }
    }
    vec3 albedo = vec3(0.0), n = vec3(0.0); float rough = 0.0, wt = 0.0, mao = 0.0;
    for (int j = 0; j < 4; j++) {
        if (hsum[j] < 0.0) continue;
        int k = id[j];
        float b = max(hsum[j] - best + 0.3, 0.0);
        vec3 al = mix(mats[j].albedo, dot(mats[j].albedo, vec3(0.3, 0.59, 0.11)) * RECOLOR_HUE * 1.45, RECOLOR[k]);
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
    vec3 patch_tint = mix(vec3(0.82, 1.0, 0.78), vec3(1.12, 1.06, 0.82), pa);   // dark lush .. yellow-green
    patch_tint = mix(patch_tint, vec3(1.2, 0.92, 0.66), pb * 0.55);               // bronze dying fronds
    albedo *= mix(vec3(1.0), patch_tint, clamp(live, 0.0, 1.0));
    // Soil next to living cover is dark humus and frond litter, not the bare photo's orange.
    albedo = mix(albedo, albedo * vec3(0.78, 0.8, 0.62), clamp(w[2] * (w[4] + w[3]) * 3.0, 0.0, 0.7));
    // Under trees the floor is shaded litter; beyond the impostor range the
    // terrain itself carries the forest canopy colour.
    albedo = mix(albedo, albedo * vec3(0.7, 0.8, 0.55), canopy * 0.6);
    // Far forest. Between distant crowns the ground is the shaded floor, and
    // past the impostor range the terrain is the canopy: coloured by the
    // stand's species mix (dark blue-green conifers, yellow-green broadleaf,
    // olive palms), varied per stand and dappled per crown.
    float cf = clamp(canopy * 1.4, 0.0, 1.0), vfar = max(u_veg_far, 10.0);
    float beyond = smoothstep(vfar * 0.8, vfar, dist);
    float gaps = smoothstep(400.0, 2000.0, dist) * (1.0 - beyond);
    float far_canopy = 0.0;
    if (cf * max(gaps, beyond) > 0.0) {
        vec4 vm = texture(t_vegmix, uv);
        vec3 cc = (vm.r * vec3(0.026, 0.056, 0.034) + vm.g * vec3(0.058, 0.098, 0.03) + vm.b * vec3(0.05, 0.105, 0.038) + vm.a * vec3(0.075, 0.09, 0.04))
                / max(dot(vm, vec4(1.0)), 1e-3);
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
    // Past the sward ring, clumps of low plants and their shadows mottle the
    // ground at a few metres, so distant slopes are not a smooth lawn.
    // Scales grow with distance (a few metres near, shrub stands far) so the
    // mottle never averages out to a flat lawn.
    float c_s = texture(t_noise, p.xz / 4.3 + 0.41).r * 0.6 + texture(t_noise, p.xz / 1.7 + 0.13).g * 0.4;
    float c_m = texture(t_noise, p.xz / 13.0 + 0.23).g, c_l = texture(t_noise, p.xz / 38.0 + 0.87).r;
    float clump = mix(mix(c_s, c_m, smoothstep(40.0, 140.0, dist)), c_l, smoothstep(180.0, 600.0, dist) * 0.6);
    albedo *= mix(1.0, mix(0.58, 1.18, smoothstep(0.4, 0.6, clump)), clamp(live, 0.0, 1.0) * smoothstep(18.0, 40.0, dist) * (1.0 - smoothstep(1200.0, 2500.0, dist)));
    // Rock reads grey-brown, not orange: desaturate the source photo.
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
    float reach = 1.1 + 0.5 * (nz2.z - 0.5);
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
        if (u_csm_debug == 1) { o = vec4(csm_debug_tint(p) * (0.15 + cs * 2.0), 1.0); return; }
    }
    vec3 sun_col = sun_transmittance(t_trans, p.y, L) * SUN_INTENSITY;
    float ndl = max(dot(n, L), 0.0) * smoothstep(-0.05, 0.05, dot(N, L));
    vec3 sky_up = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L), sky_h = sky_radiance(t_skyview, normalize(vec3(L.x, 0.15, L.z) * vec3(-1.0, 1.0, -1.0)), L);
    // Animals shade the sky above the ground right under them (contact shadows).
    float contact = dist < 70.0 ? contact_occlusion(p) : 1.0;
    vec3 ambient = mix(sky_h, sky_up, n.y * 0.5 + 0.5) * 1.5 * canopy_sky(canopy) * contact;
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
