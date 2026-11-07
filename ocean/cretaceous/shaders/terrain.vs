#version 330
#include "depth.glsl"
in vec2 a_pos;
uniform mat4 u_viewproj;
uniform vec4 u_node;           // x, z, size, level
uniform vec2 u_morph;          // morph start/end distance
uniform float u_patch;         // quads per side of the patch mesh
uniform vec3 u_cam;
uniform float u_world_size;
uniform sampler2D t_height;
#include "terrain_height.glsl"
out vec3 v_world;
out float v_w;
void main() {
    float spacing = u_node.z / u_patch;
    vec2 wp = u_node.xy + a_pos * spacing;
    float h = textureLod(t_height, map_uv(wp), 0.0).r;
    float d = distance(u_cam, vec3(wp.x, h, wp.y));
    float k = clamp((d - u_morph.x) / (u_morph.y - u_morph.x), 0.0, 1.0);
    wp -= fract(a_pos * 0.5) * 2.0 * k * spacing;      // CDLOD morph to the coarser grid
    wp = clamp(wp, vec2(0.0), vec2(u_world_size));
    float y = sample_cubic(t_height, wp);
    v_world = vec3(wp.x, y, wp.y);
    vec4 clip = u_viewproj * vec4(v_world, 1.0);
    v_w = clip.w;
    gl_Position = scene_clip(clip);
}
