#version 330
// The scent view (render/scentfx.c), in display colours over the finished
// frame. A wisp is a soft, slowly churning blob; a pulse is a ring that
// spreads and fades every 1.4 s, with a faint core. Both test the scene
// depth themselves (stored as near / w, render/view.c): behind something a
// wisp shows at a third of its strength and a pulse at two thirds, so the
// pulse finds prey through plants without outlining it.
in vec2 v_uv;
in vec4 v_col, v_par;
in float v_w;
uniform sampler2D t_depth;
uniform vec2 u_screen;
uniform float u_near;
out vec4 o;
float hash2(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);
}
void main() {
    float kind = v_par.y;
    if (kind > 2.5) {   // an animal sensed: a steady halo round it, a faint fill, a slow breath
        vec2 q = v_uv * 2.0 - 1.0; float r = length(q);
        float a = exp(-pow((r - 0.78) * 9.0, 2.0)) * (0.8 + 0.2 * cos(6.2831853 * v_par.w)) + 0.14 * smoothstep(0.8, 0.0, r);
        float d = texture(t_depth, gl_FragCoord.xy / u_screen).r, scene_w = d > 0.0 ? u_near / d : 1e9;
        if (v_w > scene_w * 1.03 + 0.05) a *= 0.75;   // through the plants: nearly as clear
        o = vec4(v_col.rgb, v_col.a * a);
        return;
    }
    if (kind > 1.5) {   // edges: a soft vignette
        vec2 q = v_uv * 2.0 - 1.0;
        o = vec4(0.0, 0.0, 0.0, v_col.a * smoothstep(0.55, 1.45, length(q * vec2(1.0, 0.8))));
        return;
    }
    vec2 q = v_uv * 2.0 - 1.0; float r = length(q), a;
    if (kind < 0.5) {
        float n = vnoise(q * 2.2 + v_par.z * 31.0 + vec2(v_par.w, -0.6 * v_par.w)) * 0.65 + vnoise(q * 5.0 - v_par.w * 1.7) * 0.35;
        a = smoothstep(1.0, 0.15, r) * smoothstep(0.25, 0.75, n);
    } else {
        float ph = v_par.w, ring = exp(-pow((r - 0.25 - 0.7 * ph) * 7.0, 2.0));
        a = ring * (1.0 - ph) + 0.35 * smoothstep(0.45, 0.0, r) * (0.6 + 0.4 * cos(6.2831853 * ph));
    }
    float d = texture(t_depth, gl_FragCoord.xy / u_screen).r;
    float scene_w = d > 0.0 ? u_near / d : 1e9;
    if (v_w > scene_w * 1.03 + 0.05) a *= kind < 0.5 ? 0.33 : 0.66;
    o = vec4(v_col.rgb, v_col.a * a);
}
