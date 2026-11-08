// Cascaded sun shadow lookup (render/csm.c), shared by terrain, vegetation,
// impostors and props. Needs common.glsl (ign). Multiply the result into the
// horizon and cloud shadow; it is 1 past the last cascade, where the baked
// terrain horizon shadow carries on alone.
uniform sampler2DArrayShadow t_csm;
uniform mat4 u_csm_vp[4];
uniform vec4 u_csm_texel;   // world size of one texel per cascade
uniform vec4 u_csm_split;   // far view distance per cascade
uniform float u_csm_res;
uniform int u_csm_count;    // 0: shadows off
uniform int u_csm_debug;    // CRET_SHADOW_VIS=1: receivers show the shadow term tinted by cascade

const vec2 CSM_DISK[8] = vec2[8](
    vec2(-0.7071, 0.7071), vec2(-0.0000, -0.8750), vec2(0.5303, 0.5303), vec2(-0.6250, -0.0000),
    vec2(0.3536, -0.3536), vec2(-0.0000, 0.3750), vec2(-0.1768, -0.1768), vec2(0.1250, 0.0000));

// Rotated disk (8 taps, or 4 for leaves and far cascades) on top of the
// hardware 2x2 comparison filter.
float csm_pcf(int k, vec3 p, float radius_texels, vec2 rot, int taps) {
    vec4 c = u_csm_vp[k] * vec4(p, 1.0);
    vec3 q = c.xyz * 0.5 + 0.5;
    // A receiver past the cascade's far plane (the box is fit to casters,
    // e.g. open sea below a high camera) compares as lit against empty map
    // texels instead of always shadowed.
    q.z = min(q.z, 1.0);
    float px = radius_texels / u_csm_res, s = 0.0;
    int step = 8 / taps;
    for (int i = 0; i < 8; i += step) {
        vec2 o = CSM_DISK[i]; o = vec2(o.x * rot.x - o.y * rot.y, o.x * rot.y + o.y * rot.x);
        s += texture(t_csm, vec4(q.xy + o * px, float(k), q.z));
    }
    return s / float(taps);
}

// Biased position for cascade k: normal offset grows with slope to the sun;
// foliage (two-sided cards, bent normals) is pushed toward the sun instead.
vec3 csm_offset(int k, vec3 p, vec3 n, vec3 L, float foliage) {
    float t = u_csm_texel[k], ndl = clamp(dot(n, L), 0.0, 1.0);
    vec3 solid = p + n * t * (0.8 + 1.6 * (1.0 - ndl)) + L * t * 0.6;
    // Leaves: skip the nearest ~half metre of foliage toward the sun, so a
    // frond is not darkened by its own neighbours; deeper crown and other
    // plants still shade it.
    vec3 leaf = p + L * max(t * 4.0, 0.5) + n * t * 0.5;
    return mix(solid, leaf, foliage);
}

vec3 csm_debug_tint(vec3 p) {
    const vec3 T[5] = vec3[5](vec3(1.0, 0.45, 0.45), vec3(0.45, 1.0, 0.45), vec3(0.45, 0.6, 1.0), vec3(1.0, 1.0, 0.4), vec3(1.0));
    for (int i = 0; i < 4; i++) {
        if (i >= u_csm_count) break;
        vec2 a = abs((u_csm_vp[i] * vec4(p, 1.0)).xy);
        if (max(a.x, a.y) < 0.96) return T[i];
    }
    return T[4];
}
// p world position, n surface normal, dist eye distance, foliage 0..1.
float csm_shadow(vec3 p, vec3 n, vec3 L, float dist, float foliage) {
    if (u_csm_count == 0) return 1.0;
    float last = u_csm_split[u_csm_count - 1];
    if (dist > last) return 1.0;
    // First (finest) cascade whose box holds the point, with a margin for the filter.
    int k = -1; float edge = 1.0;
    for (int i = 0; i < 4; i++) {
        if (i >= u_csm_count) break;
        vec2 a = abs((u_csm_vp[i] * vec4(p, 1.0)).xy);
        float m = max(a.x, a.y);
        if (m < 0.96) { k = i; edge = m; break; }
    }
    if (k < 0) return 1.0;
    float ang = ign(gl_FragCoord.xy, 0) * 6.2831853;
    vec2 rot = vec2(cos(ang), sin(ang));
    // Penumbra of at least ~8 cm in the near cascade, 1.5 texels further out.
    float soft = mix(1.0, 1.6, foliage);
    float rad = max(1.5, 0.08 / u_csm_texel[k]) * soft;
    int taps = (foliage > 0.5 || k >= 2) ? 4 : 8;
    float s = csm_pcf(k, csm_offset(k, p, n, L, foliage), rad, rot, taps);
    // Blend into the next cascade near the box edge: no visible seam.
    if (edge > 0.8 && k + 1 < u_csm_count) {
        float rad1 = max(1.5, 0.08 / u_csm_texel[k + 1]) * soft;
        float s1 = csm_pcf(k + 1, csm_offset(k + 1, p, n, L, foliage), rad1, rot, 4);
        s = mix(s, s1, smoothstep(0.8, 0.96, edge));
    }
    return mix(s, 1.0, smoothstep(last * 0.8, last, dist));
}

// Contact shadows (render/csm.c cret_csm_set_contacts): an animal hides much
// of the sky from the ground and plants right under and around it, which the
// sun shadow maps (direct light only) do not show, and it should still sit
// in the grass under an overcast sky or at night. Each body is an ellipse in
// its own frame: u_contact_a = (feet x, y, z, half length), u_contact_b =
// (cos yaw, sin yaw, half width, height). Returns the sky-light factor at p:
// darkest under the belly, gone at twice the footprint and above the back.
uniform vec4 u_contact_a[16], u_contact_b[16];
uniform int u_contact_n;
float contact_occlusion(vec3 p) {
    float occ = 1.0;
    for (int i = 0; i < 16; i++) {
        if (i >= u_contact_n) break;
        vec4 a = u_contact_a[i], b = u_contact_b[i];
        vec2 d = p.xz - a.xz;
        vec2 q = vec2(dot(d, b.xy) / a.w, dot(d, vec2(-b.y, b.x)) / b.z);
        float r2 = dot(q, q) * 0.25;            // 1 at twice the footprint
        if (r2 >= 1.0) continue;
        float f = (1.0 - r2) * (1.0 - r2);
        float h = clamp((p.y - a.y) / b.w, 0.0, 1.0);
        occ *= 1.0 - 0.7 * f * (1.0 - smoothstep(0.25, 0.9, h));
    }
    return occ;
}
