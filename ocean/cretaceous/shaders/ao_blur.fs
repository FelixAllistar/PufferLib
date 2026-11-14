#version 330
in vec2 v_uv; out vec4 o;
uniform sampler2D t_ao;   // ao.fs: R occlusion, G linear depth
// Averages the occlusion over the 4x4 block round the pixel, which holds each of ao.fs's 16 sample
// rotations once, so the pattern cancels. Texels at another depth (a fern against the ground behind it)
// are left out, so the shade stays on its own surface instead of bleeding a halo past the silhouette.
void main() {
    ivec2 ts = textureSize(t_ao, 0), c = ivec2(gl_FragCoord.xy), mx = ts - 1;
    vec2 me = texelFetch(t_ao, clamp(c, ivec2(0), mx), 0).rg;
    float sum = 0.0, wsum = 0.0;
    for (int y = -2; y < 2; y++) for (int x = -2; x < 2; x++) {
        vec2 s = texelFetch(t_ao, clamp(c + ivec2(x, y), ivec2(0), mx), 0).rg;
        float w = 1.0 - smoothstep(0.02, 0.06, abs(s.g - me.g) / max(me.g, 1e-3));   // 2-6% of the depth apart
        sum += s.r * w; wsum += w;
    }
    o = vec4(sum / wsum, 0.0, 0.0, 1.0);   // the pixel itself always counts
}
