#version 330
// Trample map update (render/trample.h): one texel per ~0.4 m around the
// camera. rg = bend vector (xz; 1 = laid flat), b = trail strength.
// Animals and critters (the contact-shadow ellipses, csm.glsl) push the cover outward
// from their spine; bigger bodies push harder. Blades follow quickly, spring
// part of the way back with a short lag, and a trail stays down a few seconds.
uniform sampler2D t_prev;
uniform vec2 u_shift, u_origin;
uniform float u_texel, u_res, u_dt;
uniform int u_reset;
uniform vec4 u_contact_a[16], u_contact_b[16];   // (feet x, y, z, half length), (cos yaw, sin yaw, half width, height)
uniform int u_contact_n;
out vec4 o;
void main() {
    vec2 px = floor(gl_FragCoord.xy);
    vec2 p = u_origin + (px + 0.5) * u_texel;
    vec4 prev = vec4(0.0);
    vec2 q = px + u_shift;
    if (u_reset == 0 && q.x >= 0.0 && q.y >= 0.0 && q.x < u_res && q.y < u_res) prev = texelFetch(t_prev, ivec2(q), 0);
    vec2 stamp = vec2(0.0); float sm = 0.0;
    for (int i = 0; i < 16; i++) {
        if (i >= u_contact_n) break;
        vec4 a = u_contact_a[i], b = u_contact_b[i];
        vec2 d = p - a.xz, f = b.xy, s = vec2(-b.y, b.x);
        float hl = a.w, hw = b.z, H = b.w;
        // Legs and flanks brush the cover a little beyond the body outline.
        // A small animal (under 0.35 m tall: the critters, view.c) parts the
        // fronds just round itself and pushes them less far over.
        bool small_one = H < 0.35;
        float margin = small_one ? 0.12 + hw : 0.35 + 0.4 * hw;
        float u = dot(d, f), v = dot(d, s);
        float r = length(vec2(u / (hl + margin), v / (hw + margin)));
        if (r >= 1.0) continue;
        // Outward from the spine (a segment along the body), sideways under the belly.
        float uc = clamp(u, -hl * 0.8, hl * 0.8);
        vec2 off = vec2(u - uc, v);
        float ol = length(off);
        vec2 dl = ol > 0.05 ? off / ol : vec2(0.0, v >= 0.0 ? 1.0 : -1.0);
        vec2 dw = f * dl.x + s * dl.y;
        float w = 1.0 - smoothstep(0.5, 1.0, r);
        float big = small_one ? 0.5 : clamp(0.55 + 0.2 * H, 0.7, 1.25);   // raptor ~0.8, stegosaur ~1.1, tyrannosaur 1.25
        float m = w * big;
        if (m > sm) { sm = m; stamp = dw * m; }
    }
    float pm = length(prev.xy);
    float trail = max(prev.b * exp(-u_dt / 4.0), sm * 0.7);
    vec2 target = stamp;
    if (sm < trail) target = (pm > 1e-3 ? prev.xy / pm : vec2(0.0)) * trail;
    float rate = sm > pm ? 10.0 : 4.0;
    vec2 bend = u_reset == 1 ? target : mix(prev.xy, target, 1.0 - exp(-u_dt * rate));
    o = vec4(bend, trail, 0.0);
}
