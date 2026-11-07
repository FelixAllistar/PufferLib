// Height sampling shared by terrain and water vertex shaders.
uniform vec2 u_map;            // samples per side, metres per sample
vec2 map_uv(vec2 xz) { return (xz / u_map.y + 0.5) / u_map.x; }
// Smooth cubic B-spline filtering from four bilinear taps (Sigg & Hadwiger).
float sample_cubic(sampler2D t, vec2 xz) {
    vec2 tc = xz / u_map.y + 0.5;               // texel space (centres at .5)
    vec2 c = tc - 0.5, f = fract(c); c = floor(c) + 0.5;
    vec2 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0;
    vec2 w1 = (4.0 + 3.0 * f * f * f - 6.0 * f * f) / 6.0;
    vec2 w2 = (1.0 + 3.0 * f + 3.0 * f * f - 3.0 * f * f * f) / 6.0;
    vec2 w3 = f * f * f / 6.0;
    vec2 g0 = w0 + w1, g1 = w2 + w3;
    vec2 h0 = c - 1.0 + w1 / g0, h1 = c + 1.0 + w3 / g1;
    float inv = 1.0 / u_map.x;
    float a = textureLod(t, vec2(h0.x, h0.y) * inv, 0.0).r, b = textureLod(t, vec2(h1.x, h0.y) * inv, 0.0).r;
    float d = textureLod(t, vec2(h0.x, h1.y) * inv, 0.0).r, e = textureLod(t, vec2(h1.x, h1.y) * inv, 0.0).r;
    return mix(mix(e, d, g0.x), mix(b, a, g0.x), g0.y);
}
