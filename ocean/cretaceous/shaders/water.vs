#version 330
#include "depth.glsl"
in vec2 a_pos;
uniform mat4 u_viewproj;
layout(location = 4) in vec4 i_node;    // per patch (view.c node_instances): x, z, size, level
layout(location = 5) in vec3 i_morph;   // morph start and end distance, quads per side of the patch mesh
uniform float u_time, u_world_size;
uniform vec3 u_cam;
uniform int u_mode;            // 0 map water, 1 open ocean tiles
uniform sampler2D t_water, t_height;
#include "terrain_height.glsl"
const float PI = 3.14159265;
out vec3 v_world;
out float v_w, v_depth;
// Gerstner swell, attenuated in shallow water.
vec3 gerstner(vec2 xz, float t, float amp) {
    vec3 d = vec3(0.0);
    const vec4 W[4] = vec4[4](vec4(0.83, 0.55, 38.0, 0.22), vec4(0.31, 0.95, 21.0, 0.14), vec4(-0.45, 0.89, 13.0, 0.08), vec4(0.97, -0.24, 7.0, 0.05));
    for (int i = 0; i < 4; i++) {
        vec2 dir = W[i].xy; float L = W[i].z, A = W[i].w * amp, k = 2.0 * PI / L, c = sqrt(9.8 / k);
        float f = k * (dot(dir, xz) - c * t);
        d.xz += dir * (A * 0.6 * cos(f)); d.y += A * sin(f);
    }
    return d;
}
void main() {
    float spacing = i_node.z / i_morph.z;
    vec2 wp = i_node.xy + a_pos * spacing;
    float level, depth;
    if (u_mode == 1) { level = 0.0; depth = 60.0; }
    else {
        float h0 = textureLod(t_water, map_uv(wp), 0.0).r;
        float d = distance(u_cam, vec3(wp.x, h0, wp.y));
        float k = clamp((d - i_morph.x) / (i_morph.y - i_morph.x), 0.0, 1.0);
        wp -= fract(a_pos * 0.5) * 2.0 * k * spacing;
        wp = clamp(wp, vec2(0.0), vec2(u_world_size));
        // Outside terra's wet mask the extended level can stand above dry
        // ground (a river surface beside a lower bank). Cutting it there left
        // straight, cell-shaped water edges; instead the sheet sinks under
        // the ground as the mask fades, so the terrain depth test cuts a
        // natural shoreline (water.fs applies the same sink).
        vec2 wm = textureLod(t_water, map_uv(wp), 0.0).rg; float g = sample_cubic(t_height, wp);
        level = mix(min(wm.r, g - 0.2), wm.r, smoothstep(0.2, 0.5, wm.g));
        depth = level - g;
    }
    vec3 p = vec3(wp.x, level, wp.y);
    float amp = smoothstep(1.5, 18.0, depth) * smoothstep(-3.0, 2.0, -abs(level)) ;   // swell only near sea level
    p += gerstner(wp, u_time, amp);
    v_world = p; v_depth = depth;
    vec4 clip = u_viewproj * vec4(p, 1.0);
    v_w = clip.w; gl_Position = scene_clip(clip);
}
