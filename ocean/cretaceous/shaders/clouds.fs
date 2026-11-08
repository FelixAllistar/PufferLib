#version 330
#include "common.glsl"
// Volumetric cumulus field, ray-marched at half resolution and accumulated
// temporally (cloud_resolve.fs).
// Shape: the weather map holds one smooth dome per cloud (R = footprint
// strength, G = top height). Above the shared condensation level CB the dome
// is full, which gives flat bases; higher up only the dome core survives,
// which gives rounded tops. Perlin-Worley billows and Worley detail erode the
// dome surface into cauliflower turrets, more so toward the top.
in vec2 v_uv; out vec4 o;
uniform sampler2D t_depth, t_weather, t_trans, t_skyview;
uniform sampler3D t_shape, t_detail;
uniform mat4 u_inv_viewproj;
uniform vec3 u_cam, u_sun;
uniform vec2 u_screen;
uniform float u_far, u_time, u_cloud_cover;
uniform int u_frame;
uniform float u_quality;           // 0 mobile .. 1 desktop
const float CB = 1600.0, CT = 4000.0;     // condensation level and highest tops (m)
const float SIGMA = 0.08;                 // extinction per unit density (1/m)
vec2 wind() { return vec2(u_time * 6.0, u_time * 2.5); }
float remap(float v, float a, float b, float c, float d) { return c + (v - a) / (b - a) * (d - c); }

// Dome strength (0..1) of the cloud above xz and its top height fraction.
vec2 footprint(vec2 xz) {
    vec4 w = textureLod(t_weather, (xz + wind()) / 30000.0, 0.0);
    float c = clamp(remap(w.r, 1.0 - u_cloud_cover - 0.05, 1.0, 0.0, 1.0), 0.0, 1.0);
    // Overcast: the cumulus merge into a continuous deck ~700 m thick.
    return vec2(max(c, 0.62 * u_overcast), max(w.g, 0.32 * u_overcast));
}
// Smooth cloud envelope (footprint and dome, no noise): used to find clouds
// with coarse steps without skipping small ones.
bool envelope(vec3 p) {
    float h = (p.y - CB) / (CT - CB);
    if (h < 0.0 || h > 1.0) return false;
    vec2 fp = footprint(p.xz);
    return fp.x > 0.0 && h < fp.y * mix(0.35, 1.0, fp.x);
}
float g_lod = 1.0;   // detail weight for the current ray sample (fades with distance)
// Explicit mip level for the noise lookups. Implicit derivatives are useless
// in a jittered ray march (neighbouring pixels sample far-apart points), and
// picked blurry mips per 2x2 quad, which showed as horizontal dashes.
float g_mip = 0.0;
float density(vec3 p, bool cheap) {
    float h = (p.y - CB) / (CT - CB);
    if (h < 0.0 || h > 1.0) return 0.0;
    vec2 fp = footprint(p.xz);
    if (fp.x <= 0.0) return 0.0;
    // Dome: tops reach fp.y at the core and fall off toward the cell edge.
    float top = fp.y * mix(0.35, 1.0, fp.x);
    float hr = h / max(top, 0.03);
    if (hr >= 1.0) return 0.0;
    float profile = smoothstep(0.0, 0.04, h) * (1.0 - hr * hr);
    vec3 sp = (p + vec3(wind().x, 0.0, wind().y) * 1.2) / 1100.0;
    vec4 s = textureLod(t_shape, sp, g_mip);
    float wfbm = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
    float billow = clamp(remap(s.r, wfbm - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
    // Noise-first: the footprint strength sets how much of the billow noise
    // survives, so silhouettes follow the turrets, not the smooth dome. The
    // base keeps more of the field, so it stays flat.
    float flat_base = 1.0 - smoothstep(0.0, 0.18, hr);
    billow = smoothstep(0.3, 0.95, billow);
    float n = mix(mix(billow, 1.0, 0.3), 1.0, flat_base * 0.8) * profile;
    n = mix(n, profile * mix(0.84, 1.0, billow), u_overcast * 0.8);   // overcast: an even deck, not cells (rain: smoother still)
    float core = sqrt(fp.x);
    float d = clamp(remap(n, 1.0 - core, 1.0, 0.0, 1.0), 0.0, 1.0) * core;
    if (cheap || d <= 0.0 || g_lod <= 0.0) return d;
    vec3 dd = textureLod(t_detail, p / 300.0 + vec3(u_time * 0.003, -u_time * 0.002, 0.0), max(g_mip - 1.0, 0.0)).rgb;
    float dn = dd.r * 0.625 + dd.g * 0.25 + dd.b * 0.125;
    // Wispy (inverted) detail at the base, cauliflower at the top.
    dn = mix((1.0 - dn) * 0.5, dn, smoothstep(0.05, 0.4, hr));
    return clamp(remap(d, dn * 0.38 * g_lod, 1.0, 0.0, 1.0), 0.0, 1.0);
}
// Checkerboard: with u_cb.x >= 0 this pass runs at a quarter of the
// half-res cloud buffer and each texel marches the half-res pixel at offset
// u_cb inside its 2x2 block (a different one every frame); cloud_resolve.fs
// fills the other three from reprojected history.
uniform ivec2 u_cb;
uniform vec2 u_half;   // half-res cloud buffer size
void main() {
    ivec2 hp = u_cb.x >= 0 ? ivec2(gl_FragCoord.xy) * 2 + u_cb : ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(hp) + 0.5) / u_half;
    vec4 pf = u_inv_viewproj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(pf.xyz / pf.w - u_cam);
    // Depth of the top-left full-res pixel of this 2x2 block (the composite's
    // depth-aware upsample compares against the same texel).
    float scene_w = linear_depth(texelFetch(t_depth, min(hp * 2, textureSize(t_depth, 0) - 1), 0).r, u_far);
    vec3 fwd = normalize((u_inv_viewproj * vec4(0.0, 0.0, 1.0, 1.0)).xyz / (u_inv_viewproj * vec4(0.0, 0.0, 1.0, 1.0)).w - u_cam);
    float scene_t = scene_w >= u_far * 0.99 ? 1e9 : scene_w / max(dot(dir, fwd), 1e-3);
    if (abs(dir.y) < 1e-4) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }
    float ta = (CB - u_cam.y) / dir.y, tb = (CT - u_cam.y) / dir.y;
    float t0 = max(min(ta, tb), 0.0), t1 = max(ta, tb);
    if (u_cam.y > CB && u_cam.y < CT) t0 = 0.0;
    t1 = min(t1, min(scene_t, 55000.0));
    if (t1 <= t0 || t0 > 52000.0) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }
    int STEPS = int(mix(72.0, 160.0, u_quality));
    // Step length tracks distance (constant angular footprint), so near
    // clouds are sampled finely: fixed long steps alias the steep vertical
    // density profile into horizontal bands at grazing angles. Coarse steps
    // through clear air; on a hit, back up and walk the cloud finely.
    float dmin = mix(24.0, 14.0, u_quality), dmax = mix(380.0, 240.0, u_quality), dk = mix(0.016, 0.009, u_quality);
    float jit = fract(ign(vec2(hp), u_frame) + float(u_frame % 16) * 0.618034);
    float t = t0 + clamp(t0 * dk, dmin, dmax) * jit;
    vec3 L = u_sun;
    vec3 amb_top = sky_radiance(t_skyview, vec3(0.0, 1.0, 0.0), L);
    // Horizon light averaged over four azimuths: at sunset the sky toward the
    // sun glows far brighter than the rest, and sampling only that direction
    // turned shaded cloud bases cream.
    vec2 sh = normalize(L.xz + 1e-5);
    vec3 amb_hor = 0.25 * (sky_radiance(t_skyview, normalize(vec3(sh.x, 0.15, sh.y)), L) + sky_radiance(t_skyview, normalize(vec3(-sh.x, 0.15, -sh.y)), L)
                         + sky_radiance(t_skyview, normalize(vec3(-sh.y, 0.15, sh.x)), L) + sky_radiance(t_skyview, normalize(vec3(sh.y, 0.15, -sh.x)), L));
    vec3 amb_bot = amb_hor * 0.3 + vec3(0.025, 0.035, 0.02) * max(L.y, 0.0) * SUN_INTENSITY * 0.08;   // forest bounce
    float c = dot(dir, L);
    // Three-octave multiple scattering (Wrenninge): each octave sees less
    // extinction and a flatter phase. The forward lobe gives silver linings.
    float ph[3];
    for (int k = 0; k < 3; k++) {
        float g = 0.75 * pow(0.5, float(k));
        ph[k] = mix(hg_phase(c, g), hg_phase(c, -0.25 * pow(0.5, float(k))), 0.25) * 4.0 * PI;
    }
    vec3 S = vec3(0.0); float T = 1.0; float tw = 0.0, wsum = 0.0;
    bool coarse = true; int empty = 0; float t_fine_end = 0.0;
    for (int i = 0; i < STEPS; i++) {
        if (t > t1) break;
        float dt = clamp(t * dk, dmin, dmax);
        vec3 p = u_cam + dir * t;
        g_lod = (1.0 - smoothstep(9000.0, 24000.0, t)) * step(0.25, u_quality);
        g_mip = max(log2(t * 1.6 / (u_half.y * 8.6)), 0.0);   // pixel footprint / shape texel (8.6 m)
        if (coarse) {
            if (envelope(p)) { coarse = false; empty = 0; t = max(t - dt * 1.5, max(t0, t_fine_end)); continue; }
            t += dt * 2.0; continue;
        }
        // Thin out cloud within ~100 m of the camera: flying at or into the
        // base shows soft wisps instead of a smear filling the screen.
        float d = density(p, false) * smoothstep(15.0, 110.0, t);
        if (d > 0.002) {
            empty = 0;
            // Light march toward the sun with growing steps.
            // Fewer, longer light taps for distant samples (LOD).
            int nl = (g_lod > 0.5 && u_quality > 0.5) ? 5 : 3; float grow = g_lod > 0.5 ? 1.9 : 2.8;
            float od = 0.0, ls = 70.0; vec3 q = p + L * ls * 0.5;
            for (int j = 0; j < 5; j++) { if (j >= nl) break; od += density(q, true) * ls; q += L * ls; ls *= grow; }
            // Moonlight is graded down 3 stops; deep self-shadow pockets then read
            // as holes, so night clouds get a softer light march (flatter, as seen).
            od *= SIGMA * mix(1.0, 0.55, u_night);
            float h = clamp((p.y - CB) / (CT - CB), 0.0, 1.0);
            vec3 lum = vec3(0.0); float a = 1.0, b = 1.0;
            // Sun light as seen through the hazy low air (a third of the cloud
            // altitude): golden hour reddens the clouds the way the hills are.
            vec3 sc = sun_transmittance(t_trans, p.y * 0.3, L) * SUN_INTENSITY * (1.0 - 0.6 * u_overcast);   // a deck above dims the sun
            for (int k = 0; k < 3; k++) { lum += sc * (a * exp(-od * b) * ph[k]); a *= 0.5; b *= 0.5; }
            // Powder: deep interior faces toward the viewer look darker.
            float powder = 1.0 - exp(-d * SIGMA * 220.0);
            lum *= mix(1.0, powder, 0.5 * (1.0 - max(c, 0.0)) * (1.0 - 0.6 * u_night));
            // Sky light: bright on top, dark grey-blue at the flat base.
            float up = smoothstep(0.0, 0.35, h) * 0.75 + 0.25 * d;
            lum += mix(amb_bot, amb_top, up) * (0.7 + 0.5 * exp(-d * 2.0));
            float sigma = d * SIGMA;
            float st = exp(-sigma * dt);
            S += T * lum * (1.0 - st);
            tw += t * T * (1.0 - st); wsum += T * (1.0 - st);
            T *= st;
            if (T < 0.015) break;
        } else if (++empty > 4) { coarse = true; t_fine_end = t + dt; }
        t += dt;
    }
    // Aerial perspective: blend clouds toward the sky by their weighted depth.
    if (wsum > 0.0) {
        float dist = tw / wsum;
        float fade = exp(-dist / 28000.0) * smoothstep(0.0, 0.06, dir.y + 0.02);
        vec3 sky = sky_radiance(t_skyview, normalize(vec3(dir.x, max(dir.y, 0.01), dir.z)), L);
        S = mix(sky * (1.0 - T), S, fade);
    }
    S *= mix(1.0, 0.45, u_night);   // moonlit cumulus: silver-grey against the dark dome, not daylight white
    o = vec4(S, T);
}
