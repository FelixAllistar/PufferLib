#version 330
#include "common.glsl"
// Volumetric cloud sky, ray-marched one full-resolution pixel per 4x4 block
// each frame and accumulated into a full-res history (cloud_resolve.fs).
//
// Shape follows the production recipe (Schneider 2015, Hillaire 2016):
// a weather map (view.c) gives coverage, cloud type and a high-cloud mask;
// the type picks a height profile (flat stratocumulus .. cumulus .. towering);
// Perlin-Worley base shape with the coverage remap carves clumps of every
// size out of continuous fields; Worley detail, pushed around by a curl
// field, erodes the surface into cauliflower turrets near the top and wisps
// at the flat base. Lighting: Beer-powder with three multiple-scattering
// octaves, sun through the low air, sky ambient graded top to base.
// Above the cumulus sit two thin analytic layers: an altocumulus deck and
// cirrus wisps, both driven by the cloud cover.
in vec2 v_uv; out vec4 o;
uniform sampler2D t_depth, t_weather, t_trans, t_skyview, t_noise;
uniform sampler3D t_shape, t_detail;
uniform vec3 u_cam, u_sun;
uniform vec2 u_screen;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame;
uniform float u_quality;           // 0 mobile .. 1 desktop
const float CB = CLOUD_BASE, CT = CLOUD_TOP;   // common.glsl
const float SIGMA = 0.09;                 // extinction per unit density (1/m)
const float ALT_H = 5600.0, CIR_H = 9500.0;   // thin layers (m)
vec2 wind() { return cloud_wind(u_time); }
// Safe against b == a (a zero mask once made NaN pixels that spread through the history).
float remap(float v, float a, float b, float c, float d) { return c + (v - a) / max(b - a, 1e-4) * (d - c); }

// Weather at xz: x coverage (0..1 after the cover remap and the overcast
// floor), y type (0 flat .. 1 towering), z high-cloud mask, w the height of
// this cloud's base above CLOUD_BASE (m).
vec4 weather(vec2 xz) {
    vec4 w = textureLod(t_weather, (xz + wind()) / 30000.0, 0.0);
    // Clear below 1 - 0.85 cover (real gaps: a fair sky at 0.4 is more than
    // half empty), rising to solid cores over the next 0.45 of the field.
    float lo = 1.0 - u_cloud_cover * 0.75;
    float c = clamp(remap(w.r, lo, lo + 0.45, 0.0, 1.0), 0.0, 1.0);
    // Overcast: the field closes into a continuous flat deck.
    c = mix(c, max(c, 0.82), u_overcast);
    float type = mix(w.g, 0.08, u_overcast * 0.9);
    // Bases at one altitude everywhere read as clouds sliced by a plane and,
    // toward the horizon, as a single line. Each field condenses at its own
    // level; an overcast deck is level.
    return vec4(c, type, w.b, w.a * CLOUD_LIFT * (1.0 - u_overcast));
}
// Top of the cloud (fraction of the layer above its base) for this coverage
// and type: big dense cells tower, thin ones stay flat. The height falls to
// a few percent at the rim of a cloud, so its sides round over like a heap;
// at half height there, as this was, every cloud ended in a wall.
float cloud_top(float c, float type) {
    float dome = pow(smoothstep(0.0, 0.8, c), 0.7);
    return mix(0.22, 1.0, type * sqrt(type)) * mix(0.06, 1.0, mix(dome, 1.0, u_overcast));
}
// Smooth cloud envelope (coverage and type, no noise): the coarse march
// looks for clouds with it without skipping small ones.
bool envelope(vec3 p) {
    if (p.y < CB || p.y > CT) return false;
    vec4 w = weather(p.xz);
    float h = (p.y - CB - w.w) / (CT - CB - w.w);
    return w.x > 0.05 && h >= 0.0 && h < cloud_top(w.x, w.y);
}
float g_lod = 1.0;   // detail weight for the current ray sample (fades with distance)
// Explicit mip level for the noise lookups. Implicit derivatives are useless
// in a jittered ray march (neighbouring pixels sample far-apart points), and
// picked blurry mips per 2x2 quad, which showed as horizontal dashes.
float g_mip = 0.0;
float density(vec3 p, bool cheap) {
    if (p.y < CB || p.y > CT) return 0.0;
    vec4 w = weather(p.xz);
    if (w.x <= 0.0) return 0.0;
    float h = (p.y - CB - w.w) / (CT - CB - w.w);
    if (h < 0.0) return 0.0;
    float top = cloud_top(w.x, w.y);
    float hr = h / top;
    if (hr >= 1.0) return 0.0;
    // Height profile: a sharp flat base, then coverage that falls toward the
    // top (flat types keep more of it), so turrets grow where the noise and
    // the coverage both allow it, not where a dome says so.
    float roll = mix(0.72, 0.4, w.y);
    float grad = smoothstep(0.0, 0.16, hr) * (1.0 - smoothstep(roll, 1.0, hr));   // the base thins over its lowest sixth: ragged, not sliced
    float cov = w.x * grad;
    if (cov <= 0.0) return 0.0;
    vec3 sp = (p + vec3(wind().x, 0.0, wind().y) * 1.2) / 1100.0;
    vec4 s = textureLod(t_shape, sp, g_mip);
    float wfbm = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
    float base = clamp(remap(s.r, wfbm - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
    // Coverage remap: how much of the shape noise survives. Density stays
    // full inside (not scaled by coverage) so small clouds are still solid.
    float d = clamp(remap(base, 1.0 - cov, 1.0, 0.0, 1.0), 0.0, 1.0) * smoothstep(0.0, 0.35, cov);
    if (cheap || d <= 0.0 || g_lod <= 0.0) return d;
    // Curl: the wave-normal channels of the tiling noise are a gradient; its
    // 90-degree turn is divergence-free, so the detail swirls instead of
    // shearing. Stronger at the base (wisps) than in the billows.
    vec2 cn = textureLod(t_noise, (p.xz + wind() * 0.6) / 2600.0, 0.0).ba * 2.0 - 1.0;
    vec3 dp = p + vec3(cn.y, 0.25 * (cn.x - cn.y), -cn.x) * mix(90.0, 35.0, smoothstep(0.0, 0.35, hr));
    vec3 dd = textureLod(t_detail, dp / 280.0 + vec3(u_time * 0.004, -u_time * 0.002, 0.0), max(g_mip + 1.0, 0.0)).rgb;
    float dn = dd.r * 0.625 + dd.g * 0.25 + dd.b * 0.125;
    // Wispy (inverted) detail at the base, cauliflower billows above.
    dn = mix(1.0 - dn, dn, smoothstep(0.03, 0.3, hr));
    // Erosion bites hardest where the base density is thin (edges and
    // turret tips) and leaves the interior solid. Overcast decks stay smooth.
    float erode = dn * mix(0.55, 0.22, u_overcast) * g_lod * (1.0 - 0.5 * d);
    d = clamp(remap(d, erode, 1.0, 0.0, 1.0), 0.0, 1.0);
    return d;
}
// Thin analytic layers above the cumulus. Each is a plane: a 2D density
// from the noise textures masked by the weather map's high-cloud channel,
// lit by the sun through the air at its altitude plus sky light, and faded
// into the horizon haze like the cumulus.
void thin_layer(float H, vec3 dir, vec3 L, vec3 amb, float ph_fwd, inout vec3 S, inout float T) {
    if (dir.y <= 0.02 || u_cam.y >= H) return;
    float t = (H - u_cam.y) / dir.y;
    vec3 p = u_cam + dir * t;
    float mask = textureLod(t_weather, (p.xz + wind() * 1.5) / 30000.0, 0.0).b;
    float dens;
    bool cirrus = H > 8000.0;
    if (cirrus) {
        // Long combed wisps along the wind; present under fair and mixed skies.
        float amount = smoothstep(0.08, 0.4, u_cloud_cover) * (1.0 - smoothstep(0.65, 0.88, u_cloud_cover)) * 0.45;
        vec2 q = p.xz + wind() * 2.0;
        float n = textureLod(t_noise, vec2(q.x / 42000.0, q.y / 5500.0), 1.0).r;
        float n2 = textureLod(t_noise, vec2(q.x / 14000.0, q.y / 1800.0) + 0.37, 1.0).g;
        float m = clamp(remap(mask, 1.0 - amount, 1.0, 0.0, 1.0), 0.0, 1.0);
        dens = clamp(remap(n * 0.6 + n2 * 0.4, 1.0 - m * 0.7, 1.0, 0.0, 1.0), 0.0, 1.0) * 0.55;
    } else {
        // Altocumulus: a deck of small cells, closing with the cover and
        // merging under the overcast cumulus deck.
        float amount = smoothstep(0.4, 0.8, u_cloud_cover) * 0.25 * (1.0 - 0.6 * u_overcast);
        vec2 q = p.xz + wind() * 1.3;
        vec4 s = textureLod(t_shape, vec3(q.x / 2600.0, 0.31, q.y / 2600.0), 0.0);
        float cells = s.g * 0.6 + s.b * 0.4;
        float m = clamp(remap(mask, 1.0 - amount, 1.0, 0.0, 1.0), 0.0, 1.0);
        dens = clamp(remap(cells, 1.0 - m * 0.7, 1.0, 0.0, 1.0), 0.0, 1.0) * 1.2;
    }
    if (dens <= 0.0) return;
    float Tl = exp(-dens * (cirrus ? 1.0 : 1.8));
    vec3 sc = sun_transmittance(t_trans, H, L) * SUN_INTENSITY * (1.0 - 0.6 * u_overcast);
    // Thin and bright: mostly sky-lit white, a forward glow toward the sun;
    // only the thicker altocumulus cells shade a little.
    float lit = exp(-dens * (cirrus ? 0.3 : 0.7));
    vec3 lum = sc * lit * (cirrus ? ph_fwd * 0.9 + 0.1 : ph_fwd * 0.5 + 0.16) + amb * 1.3 * mix(0.75, 1.0, lit);
    float fade = exp(-t / (cirrus ? 60000.0 : 40000.0)) * smoothstep(0.0, 0.06, dir.y);
    vec3 sky = sky_radiance(t_skyview, dir, L);
    vec3 Sl = mix(sky, lum, fade) * (1.0 - Tl);
    S += T * Sl; T *= Tl;
}
// Sparse march: with u_cb.x >= 0 this pass runs at a quarter of the full-res
// cloud buffer in each direction and each texel marches the full-res pixel
// at offset u_cb inside its 4x4 block (another one every frame);
// cloud_resolve.fs fills the rest from reprojected history.
uniform ivec2 u_cb;
uniform vec2 u_full;   // full-res cloud buffer size
void main() {
    ivec2 hp = u_cb.x >= 0 ? ivec2(gl_FragCoord.xy) * 4 + u_cb : ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(hp) + 0.5) / u_full;
    vec3 dir = view_ray(uv * 2.0 - 1.0), fwd = u_fwd;
    float depth_raw = texelFetch(t_depth, min(hp, textureSize(t_depth, 0) - 1), 0).r;
    // A surface in front of the whole layer: nothing to march. The resolve
    // keeps the sky image alive behind it (alpha < 0 marks "no sample").
    if (cloud_blocked(depth_raw, u_far, u_cam, dir, fwd)) { o = vec4(0.0, 0.0, 0.0, -1.0); return; }
    float scene_w = linear_depth(depth_raw, u_far);
    float scene_t = scene_w >= u_far * 0.99 ? 1e9 : scene_w / max(dot(dir, fwd), 1e-3);
    if (abs(dir.y) < 1e-4) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }
    float ta = (CB - u_cam.y) / dir.y, tb = (CT - u_cam.y) / dir.y;
    float t0 = max(min(ta, tb), 0.0), t1 = max(ta, tb);
    if (u_cam.y > CB && u_cam.y < CT) t0 = 0.0;
    t1 = min(t1, min(scene_t, 55000.0));
    vec3 L = u_sun;
    float c = dot(dir, L);
    vec3 amb_top = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L);
    // Horizon light averaged over four azimuths: at sunset the sky toward the
    // sun glows far brighter than the rest, and sampling only that direction
    // turned shaded cloud bases cream.
    vec2 sh = normalize(L.xz + 1e-5);
    vec3 amb_hor = 0.25 * (sky_radiance(t_skyview, normalize(vec3(sh.x, 0.15, sh.y)), L) + sky_radiance(t_skyview, normalize(vec3(-sh.x, 0.15, -sh.y)), L)
                         + sky_radiance(t_skyview, normalize(vec3(-sh.y, 0.15, sh.x)), L) + sky_radiance(t_skyview, normalize(vec3(sh.y, 0.15, -sh.x)), L));
    vec3 amb_bot = amb_hor * 0.42 + vec3(0.025, 0.035, 0.02) * max(L.y, 0.0) * SUN_INTENSITY * 0.08;   // forest bounce
    // Three-octave multiple scattering (Wrenninge): each octave sees less
    // extinction and a flatter phase. The forward lobe gives silver linings.
    float ph[3];
    for (int k = 0; k < 3; k++) {
        float g = 0.7 * pow(0.5, float(k));
        ph[k] = mix(hg_phase(c, g), hg_phase(c, -0.25 * pow(0.5, float(k))), 0.25) * 4.0 * PI;
    }
    vec3 S = vec3(0.0); float T = 1.0; float tw = 0.0, wsum = 0.0;
    if (t1 > t0 && t0 < 52000.0) {
        int STEPS = int(mix(60.0, 120.0, u_quality));
        // Step length tracks distance (constant angular footprint), so near
        // clouds are sampled finely: fixed long steps alias the steep vertical
        // density profile into horizontal bands at grazing angles. Coarse steps
        // through clear air; on a hit, back up and walk the cloud finely.
        float dmin = mix(26.0, 11.0, u_quality), dmax = mix(400.0, 230.0, u_quality), dk = mix(0.017, 0.0075, u_quality);
        float jit = fract(ign(vec2(hp), u_frame) + float(u_frame % 16) * 0.618034);
        float t = t0 + clamp(t0 * dk, dmin, dmax) * 2.0 * jit;   // over a whole coarse step: thin far slabs average out
        bool coarse = true; int empty = 0; float t_fine_end = 0.0;
        float od = 0.0; int lit_n = 0;   // light march result, reused every other step
        int entry = 0; bool entered = false;   // fine sub-steps left after first entering cloud
        for (int i = 0; i < STEPS; i++) {
            if (t > t1) break;
            float dt = clamp(t * dk, dmin, dmax);
            // Entering a cloud: the first samples sit on the lit rim, where
            // the lighting changes fastest, so the jittered estimate varies
            // most there. Back up one step and walk the entry at a third.
            if (entry > 0) { dt *= 0.3333; entry--; }
            vec3 p = u_cam + dir * t;
            g_lod = (1.0 - smoothstep(9000.0, 24000.0, t)) * step(0.25, u_quality);
            g_mip = max(log2(t * 1.15 / (u_full.y * 8.6)), 0.0);   // pixel footprint / shape texel (8.6 m)
            if (coarse) {
                if (envelope(p)) { coarse = false; empty = 0; t = max(t - dt * 1.5, max(t0, t_fine_end)); continue; }
                t += dt * 2.0; continue;
            }
            // Thin out cloud within ~100 m of the camera: flying at or into the
            // base shows soft wisps instead of a smear filling the screen.
            float d = density(p, false) * smoothstep(15.0, 110.0, t);
            if (d > 0.002) {
                if (!entered) { entered = true; entry = 6; t = max(t - dt * 0.8, t0); continue; }
                empty = 0;
                // Light march toward the sun with growing steps.
                // Fewer, longer light taps for distant samples (LOD).
                // Every other contributing step reuses the last result: the
                // jitter and the temporal filter average the difference out.
                if ((lit_n++ & 1) == 0) {
                    int nl = (g_lod > 0.5 && u_quality > 0.5) ? 4 : 3; float grow = g_lod > 0.5 ? 2.1 : 2.8;
                    float ls = 60.0; vec3 q = p + L * ls * 0.5; od = 0.0;
                    for (int j = 0; j < 5; j++) { if (j >= nl) break; od += density(q, true) * ls; q += L * ls; ls *= grow; }
                    // Moonlight is graded down 3 stops; deep self-shadow pockets then read
                    // as holes, so night clouds get a softer light march (flatter, as seen).
                    od *= SIGMA * mix(1.0, 0.55, u_night);
                }
                float h = clamp((p.y - CB) / (CT - CB), 0.0, 1.0);
                vec3 lum = vec3(0.0); float a = 1.0, b = 1.0;
                // Sun light as seen through the hazy low air (a third of the cloud
                // altitude): golden hour reddens the clouds the way the hills are.
                vec3 sc = sun_transmittance(t_trans, p.y * 0.3, L) * SUN_INTENSITY * (1.0 - 0.6 * u_overcast);   // a deck above dims the sun
                for (int k = 0; k < 3; k++) { lum += sc * (a * exp(-od * b) * ph[k]); a *= 0.5; b *= 0.5; }
                // Powder: deep interior faces toward the viewer look darker.
                float powder = 1.0 - exp(-d * SIGMA * 220.0);
                lum *= mix(1.0, powder, 0.5 * (1.0 - max(c, 0.0)) * (1.0 - 0.6 * u_night));
                // Sky light: bright on top, blue-grey at the flat base, and
                // dimmer deep inside the billows.
                float up = smoothstep(0.0, 0.3, h) * 0.75 + 0.25 * d;
                lum += mix(amb_bot, amb_top, up) * (0.65 + 0.55 * exp(-d * 2.0));
                float sigma = d * SIGMA;
                float st = exp(-sigma * dt);
                S += T * lum * (1.0 - st);
                tw += t * T * (1.0 - st); wsum += T * (1.0 - st);
                T *= st;
                if (T < 0.012) break;
            } else if (++empty > 4) { coarse = true; t_fine_end = t + dt; }
            t += dt;
        }
        // Aerial perspective: blend clouds toward the sky by their weighted depth.
        if (wsum > 0.0) {
            float dist = tw / wsum;
            float fade = exp(-dist / 21000.0) * smoothstep(0.0, 0.06, dir.y + 0.02);   // far fields sink into the haze, so the horizon reads as depth and not as a white line
            vec3 sky = sky_radiance(t_skyview, normalize(vec3(dir.x, max(dir.y, 0.01), dir.z)), L);
            S = mix(sky * (1.0 - T), S, fade);
        }
    }
    // Thin layers behind the cumulus (only what is still visible).
    if (T > 0.01 && scene_t > 1e8) {
        float ph_fwd = mix(hg_phase(c, 0.8), hg_phase(c, -0.2), 0.3) * 4.0 * PI;
        thin_layer(ALT_H, dir, L, amb_top, ph_fwd, S, T);
        thin_layer(CIR_H, dir, L, amb_top, ph_fwd, S, T);
    }
    S *= mix(1.0, 0.32, u_night);   // moonlit cumulus: silver-grey against the dark dome, not daylight white
    o = vec4(S, T);
}
