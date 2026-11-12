// Terrain horizon shadows and cloud shadows, shared by terrain and vegetation.
// Needs: t_horizon0/1, t_weather, u_time, u_cloud_cover, common.glsl (PI).
float horizon_angle(vec2 uv, vec3 sun) {
    vec4 a = texture(t_horizon0, uv), b = texture(t_horizon1, uv);
    float hz[8] = float[8](a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w);
    float az = atan(sun.z, sun.x); if (az < 0.0) az += 2.0 * PI;
    float f = az / (2.0 * PI) * 8.0; int k0 = int(floor(f)) & 7, k1 = (k0 + 1) & 7;
    return mix(hz[k0], hz[k1], fract(f)) * PI * 0.5;
}
float cloud_shadow(vec3 p, vec3 sun) {
    // Matches clouds.fs: the weather map's coverage field with the same
    // cover remap and wind, projected from the lower body of the layer
    // (300 m above the mean cloud base). Cloud forms where the shape noise beats
    // 1 - coverage, so the shade grows with the coverage and is nearly
    // solid under the dense middle of a field.
    vec2 q = p.xz + sun.xz / max(sun.y, 0.08) * max(CLOUD_BASE + 0.5 * CLOUD_LIFT + 300.0 - p.y, 0.0);
    vec2 wind = cloud_wind(u_time);
    float w = texture(t_weather, (q + wind) / 30000.0).r;
    float lo = 1.0 - u_cloud_cover * 0.85;
    float c = clamp((w - lo) / 0.45, 0.0, 1.0);
    // Overcast (u_overcast, common.glsl): the sun is mostly gone everywhere.
    return (1.0 - 0.85 * smoothstep(0.04, 0.6, c)) * (1.0 - 0.88 * u_overcast);
}
// Sun visibility at a point given its horizon-map uv (soft terrain shadow).
float terrain_sun(vec2 uv, vec3 p, vec3 sun) {
    if (sun.y <= -0.05) return 0.0;
    float ha = horizon_angle(uv, sun), e = asin(clamp(sun.y, -1.0, 1.0));
    return smoothstep(ha - 0.035, ha + 0.035, e) * cloud_shadow(p, sun);
}
