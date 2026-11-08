#version 330
#include "common.glsl"
in vec2 v_uv; out vec4 o;
void main() {
    float mu = v_uv.x * 2.0 - 1.0, r = R_GROUND + v_uv.y * v_uv.y * (R_TOP - R_GROUND);
    vec3 ro = vec3(0.0, r, 0.0), rd = vec3(sqrt(max(0.0, 1.0 - mu * mu)), mu, 0.0);
    if (ray_sphere(ro, rd, R_GROUND) > 0.0 && mu < 0.0) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }
    float tmax = ray_sphere(ro, rd, R_TOP); vec3 od = vec3(0.0);
    const int N = 40; float dt = tmax / float(N);
    for (int i = 0; i < N; i++) { vec3 p = ro + rd * (float(i) + 0.5) * dt; od += atmo_extinction(length(p) - R_GROUND) * dt; }
    // At night the key light is the moon: same path, cooler light (common.glsl).
    o = vec4(exp(-od) * mix(vec3(1.0), MOON_TINT, u_night), 1.0);
}
