#version 330
#include "common.glsl"
in vec2 v_uv; out vec4 o;
uniform sampler2D t_trans;
uniform vec3 u_cam, u_sun;
void main() {
    // Invert the sky-view parameterisation.
    float v = v_uv.y, l = (v - 0.5) * 2.0;
    float elev = sign(l) * l * l * PI * 0.5;
    float az = v_uv.x * PI;
    vec2 s = normalize(u_sun.xz + 1e-6);
    vec2 sd = vec2(s.x * cos(az) - s.y * sin(az), s.x * sin(az) + s.y * cos(az));
    vec3 rd = vec3(sd.x * cos(elev), sin(elev), sd.y * cos(elev));
    float r = R_GROUND + max(u_cam.y, 2.0) / 1000.0;
    vec3 ro = vec3(0.0, r, 0.0);
    float tg = ray_sphere(ro, rd, R_GROUND), tt = ray_sphere(ro, rd, R_TOP);
    float tmax = tg > 0.0 ? tg : tt;
    tmax = min(tmax, 400.0);
    const int N = 32; vec3 L = vec3(0.0), T = vec3(1.0);
    float c = dot(rd, u_sun), pr = rayleigh_phase(c), pm = hg_phase(c, 0.8);
    float t = 0.0;
    for (int i = 0; i < N; i++) {
        float t1 = tmax * pow((float(i) + 1.0) / float(N), 2.0), dt = t1 - t;
        vec3 p = ro + rd * (t + dt * 0.5); t = t1;
        float h = length(p) - R_GROUND; vec3 up = p / length(p);
        float mu_s = dot(up, u_sun);
        vec3 ts = texture(t_trans, trans_uv(length(p), mu_s)).rgb;
        vec3 sr = RAYLEIGH_S * exp(-h / RAYLEIGH_H); float sm = MIE_S * exp(-h / MIE_H) * 1.6;
        vec3 ext = atmo_extinction(h);
        // Single scattering plus a cheap isotropic multiple-scattering term.
        vec3 S = ts * (sr * pr + sm * pm) + (sr + sm) * ts * 0.06 + (sr + sm) * 0.012 * max(u_sun.y + 0.1, 0.0);
        vec3 Tstep = exp(-ext * dt);
        L += T * S * (1.0 - Tstep) / max(ext, vec3(1e-6));
        T *= Tstep;
    }
    // Night: the moonlit sky lights the ground more strongly than the eye sees
    // it (sky.fs shows NIGHT_DOME of it), plus a faint starlight and airglow
    // floor that carries the ambient light while the sun has set and the moon
    // is still below the horizon.
    vec3 glow = vec3(0.018, 0.032, 0.07) * (1.0 + 1.5 * (1.0 - abs(rd.y)));
    o = vec4(L * SUN_INTENSITY * mix(1.0, NIGHT_SKY_LIGHT, u_night) + glow * u_night, 1.0);
}
