#version 330
// Shadow-map pass for distant trees: one alpha-tested impostor card per tree,
// facing the sun. The card spans the tree's silhouette as seen from the sun
// (height H cos(elev) plus the crown's depth), using the baked side view
// nearest the sun's azimuth.
layout(location = 0) in vec2 a_corner;   // x -1..1, y 0..1
layout(location = 4) in vec4 i_ps;
layout(location = 5) in vec4 i_rot;
layout(location = 6) in vec4 i_extra;    // fade, first layer, radius, height
uniform mat4 u_viewproj;
uniform vec3 u_sun;
uniform int u_views;
out vec2 v_uv;
flat out float v_layer;
void main() {
    vec3 base = i_ps.xyz; float sc = i_ps.w, R = i_extra.z * sc * 0.8, H = i_extra.w * sc;
    vec2 th = normalize(u_sun.xz + 1e-4);
    vec3 right = vec3(th.y, 0.0, -th.x);
    vec3 F = -u_sun, U = normalize(vec3(0.0, 1.0, 0.0) - F * F.y);
    float c = i_rot.x, s = i_rot.y;
    vec2 ob = vec2(c * th.x + s * th.y, -s * th.x + c * th.y);
    float az = atan(ob.y, ob.x); if (az < 0.0) az += 6.2831853;
    float k = floor(az / 6.2831853 * float(u_views) + 0.5);
    v_layer = i_extra.y + mod(k, float(u_views));
    float se = max(u_sun.y, 0.0), ce = sqrt(1.0 - se * se);
    float lo = -R * se * 0.7, hi = H * ce + R * se * 0.7;
    vec3 centre = base + vec3(0.0, H * 0.5, 0.0);
    // Card plane through the crown centre, perpendicular to the sun.
    vec3 foot = centre - U * dot(U, centre - base);
    vec3 wp = foot + right * (a_corner.x * R) + U * mix(lo, hi, a_corner.y);
    v_uv = vec2(a_corner.x * 0.5 + 0.5, a_corner.y);
    gl_Position = u_viewproj * vec4(wp, 1.0);
}
