// Shared constants and helpers. Units: world metres, atmosphere kilometres.
const float PI = 3.14159265;
const float SUN_INTENSITY = 18.0;

// ---------------------------------------------------------------- depth
// Reverse-Z (depth.glsl): stored depth = CRET_NEAR / w, 0 = sky. Must match
// CRET_NEAR in render/view.c.
const float CRET_NEAR = 0.3;
float linear_depth(float d, float far_plane) { return d * far_plane <= CRET_NEAR ? far_plane : CRET_NEAR / d; }

// Sky light under a closed canopy (t_misc.a: canopy density from the terra
// bake). The crowns overhead hide much of the sky and pass green light, so
// the forest floor is dimmer and greener than open ground; eye adaptation
// (post.fs) brings the level back up.
vec3 canopy_sky(float canopy) { return mix(vec3(1.0), vec3(0.5, 0.66, 0.42), clamp(canopy, 0.0, 1.0) * 0.8); }

// ---------------------------------------------------------------- atmosphere (Hillaire 2020 style)
const float R_GROUND = 6360.0;
const float R_TOP = 6460.0;
const vec3 RAYLEIGH_S = vec3(5.802, 13.558, 33.1) * 1e-3;
const float RAYLEIGH_H = 8.0;
const float MIE_S = 3.996e-3;
const float MIE_A = 4.40e-3;
const float MIE_H = 1.2;
const vec3 OZONE_A = vec3(0.650, 1.881, 0.085) * 1e-3;

vec3 atmo_extinction(float h_km) {
    vec3 r = RAYLEIGH_S * exp(-h_km / RAYLEIGH_H);
    float m = (MIE_S + MIE_A) * exp(-h_km / MIE_H) * 1.6;     // humid Cretaceous air: a little hazier
    vec3 o = OZONE_A * max(0.0, 1.0 - abs(h_km - 25.0) / 15.0);
    return r + vec3(m) + o;
}
float ray_sphere(vec3 ro, vec3 rd, float r) {          // nearest positive hit, -1 if none
    float b = dot(ro, rd), c = dot(ro, ro) - r * r, d = b * b - c;
    if (d < 0.0) return -1.0;
    d = sqrt(d); float t0 = -b - d, t1 = -b + d;
    return t0 > 0.0 ? t0 : (t1 > 0.0 ? t1 : -1.0);
}
// Transmittance LUT parameterisation: u = view zenith cosine, v = altitude.
vec2 trans_uv(float r, float mu) {
    return vec2(clamp(mu * 0.5 + 0.5, 0.0, 1.0), clamp(sqrt(max(r - R_GROUND, 0.0) / (R_TOP - R_GROUND)), 0.0, 1.0));
}
vec3 sun_transmittance(sampler2D t_trans, float alt_m, vec3 sun) {
    return texture(t_trans, trans_uv(R_GROUND + max(alt_m, 0.0) / 1000.0, sun.y)).rgb;
}
float rayleigh_phase(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }
float hg_phase(float c, float g) { float g2 = g * g; return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * c, 1.5)); }

// Sky-view LUT: azimuth relative to the sun (u), non-linear elevation (v).
vec2 skyview_uv(vec3 dir, vec3 sun) {
    float elev = asin(clamp(dir.y, -1.0, 1.0));
    float v = 0.5 + 0.5 * sign(elev) * sqrt(abs(elev) / (PI * 0.5));
    vec2 a = normalize(dir.xz + 1e-6), b = normalize(sun.xz + 1e-6);
    float az = acos(clamp(dot(a, b), -1.0, 1.0)) / PI;
    return vec2(az, v);
}
// Weather (render/view.c, from the cloud cover): 0 clear .. 1 overcast;
// surface wetness 0..1. Unset (0) in programs that do not take part.
uniform float u_overcast, u_wet;
// Night (view.c): 0 day .. 1 once the moon is the key light. The moonlit
// frame is lit like day and graded in post.fs. The moon is a cool key light,
// and the sky lights the ground more than the visible dome shows, so shadows
// and forest stay readable under a dark, starry sky.
uniform float u_night;
const vec3 MOON_TINT = vec3(0.76, 0.9, 1.15);
const float NIGHT_SKY_LIGHT = 2.4;   // sky-view LUT gain at night: ambient light and haze
const float NIGHT_DOME = 0.3;        // fraction of that gain left where the sky itself is seen
const float NIGHT_HAZE = 0.5;        // and in the haze: distance fades toward a dark horizon, not fog
// u_night as the sky-view LUT carries it (alpha, skyview.fs), for programs
// that do not set u_night (plants, props, dinosaurs).
float lut_night(sampler2D t_skyview) { return texture(t_skyview, vec2(0.5)).a; }
// Under an overcast sky the dome is an even, cool grey and the sun is gone.
vec3 sky_radiance(sampler2D t_skyview, vec3 dir, vec3 sun) {
    vec3 c = texture(t_skyview, skyview_uv(dir, sun)).rgb;
    if (u_overcast <= 0.0) return c;
    float l = dot(c, vec3(0.3, 0.59, 0.11));
    return mix(c, vec3(l) * vec3(0.93, 0.97, 1.0) * 0.8, u_overcast * 0.85);
}
// Wet surfaces: porous albedo darkens, the surface film lowers roughness.
void wet_surface(inout vec3 albedo, inout float rough, float porous, float up) {
    float w = u_wet * mix(0.55, 1.0, clamp(up, 0.0, 1.0));
    albedo *= mix(1.0, mix(0.85, 0.55, porous), w); rough = mix(rough, min(rough, 0.3), w * 0.8);
}

// Aerial perspective for opaque surfaces: haze toward the horizon sky colour,
// plus low valley mist. dist in metres.
vec3 aerial(vec3 col, sampler2D t_skyview, vec3 cam, vec3 p, vec3 sun) {
#ifdef CRET_NO_FOG
    return col;   // debug console: fog off
#endif
    vec3 d = p - cam; float dist = length(d); vec3 dir = d / max(dist, 1e-3);
    float hmid = max(0.0, (cam.y + p.y) * 0.5) / 1000.0;
    vec3 ext = atmo_extinction(hmid) * mix(2.2, 6.0, u_overcast);   // game haze: depth cues within 8 km; rain haze
    vec3 T = exp(-ext * dist / 1000.0);
    vec3 skydir = normalize(vec3(dir.x, max(dir.y, 0.02), dir.z));
    vec3 ins = sky_radiance(t_skyview, skydir, sun) * mix(1.0, NIGHT_HAZE, lut_night(t_skyview));
    // Ground mist hugging low ground and water.
    float fog_h = mix(45.0, 160.0, u_overcast), dens = mix(0.00045, 0.0016, u_overcast);
    float a = cam.y / fog_h, b = p.y / fog_h;
    float od = dens * dist * (abs(a - b) > 1e-3 ? (exp(-min(a, 30.0)) - exp(-min(b, 30.0))) / (b - a) : exp(-a));
    float mist = 1.0 - exp(-max(od, 0.0));
    vec3 mist_col = ins * 0.9 + vec3(0.02);
    col = col * T + ins * (1.0 - T);
    return mix(col, mist_col, mist);
}

// Smooth 2D value noise from a hash (no texture): plant stand variation.
float hash12(vec2 p) { vec3 q = fract(vec3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return fract((q.x + q.y) * q.z); }
float vnoise2(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), f.x), mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), f.x), f.y);
}
// Foliage tint per stand and per grove: some stands darker and bluer, some
// lighter and yellower, so a forest seen from afar is a patchwork.
vec3 stand_tint(vec2 xz) {
    float a = vnoise2(xz / 160.0), b = vnoise2(xz / 45.0 + 17.0);
    vec3 t = mix(vec3(0.82, 0.9, 1.1), vec3(1.18, 1.08, 0.78), smoothstep(0.2, 0.8, a));
    return t * mix(0.8, 1.15, b);
}

// ---------------------------------------------------------------- clouds
// Low cloud layer (clouds.fs, shadow.glsl): condensation level and the
// highest towering tops, metres above sea level.
const float CLOUD_BASE = 1500.0, CLOUD_TOP = 4600.0;
// The cloud buffer is a sky image: a scene pixel shows no cloud when its
// surface lies in front of the whole layer (post.fs, bright.fs, lum.fs). Only
// a camera above the condensation level sees terrain through cloud.
bool cloud_blocked(float depth_raw, float far_plane, vec3 cam, vec3 dir, vec3 fwd) {
    if (depth_raw <= 0.0 || cam.y >= CLOUD_BASE) return false;
    float scene_t = linear_depth(depth_raw, far_plane) / max(dot(dir, fwd), 1e-3);
    return dir.y <= 0.0 || scene_t < (CLOUD_BASE - cam.y) / dir.y;
}

// View basis for the cloud passes (view.c basis_uniforms): the ray of a
// pixel from its clip-space xy, and that ray in last frame's basis.
uniform vec3 u_right, u_up, u_fwd, u_prev_right, u_prev_up, u_prev_fwd;
uniform vec2 u_tan;   // tan of the half field of view, x and y
vec3 view_ray(vec2 ndc) { return normalize(u_fwd + u_right * (ndc.x * u_tan.x) + u_up * (ndc.y * u_tan.y)); }
// Previous-frame uv of a direction; false when it was behind the camera.
bool prev_uv(vec3 dir, out vec2 puv) {
    vec3 d = vec3(dot(dir, u_prev_right), dot(dir, u_prev_up), dot(dir, u_prev_fwd));
    puv = 0.5 + 0.5 * vec2(d.x / max(d.z, 1e-6) / u_tan.x, d.y / max(d.z, 1e-6) / u_tan.y);
    return d.z > 1e-4;
}

// Interleaved gradient noise for dithering and ray jitter.
float ign(vec2 p, int frame) { p += 5.588238 * float(frame % 64); return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

// sRGB albedo already linear via SRGB8 texture; helpers for lighting.
float ggx_spec(vec3 n, vec3 v, vec3 l, float rough) {
    vec3 h = normalize(v + l); float a = rough * rough, a2 = a * a;
    float nh = max(dot(n, h), 0.0), nv = max(dot(n, v), 1e-3), nl = max(dot(n, l), 0.0);
    float d = a2 / (PI * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
    float k = a * 0.5, g = nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
    return d * g / max(4.0 * nv * nl, 1e-3);
}
