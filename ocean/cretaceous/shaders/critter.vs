#version 330
#include "depth.glsl"
// Small animals (render/critters.c, meshes from render/critter_mesh.h). One
// instanced draw per kind and LOD; every pose is made here from a handful of
// numbers per animal, without bones:
//   - the spine bends sideways (a lizard's standing wave, a fish's travelling
//     tail-beat, a tail's sway) and up and down (a bounding mouse's arched
//     back); every vertex follows the spine at its spine coordinate, so legs
//     and fins ride on their girdle;
//   - legs swing about the hip (mammals and birds fore and aft, lizards
//     sideways from their sprawl) with the foot lifted on the way forward;
//   - the head pitches and turns about the neck (sniffing, pecking, looking
//     round); a bird's wings fold against its flanks or open and beat;
//   - the whole body bobs, rocks, crouches and flinches, and curls up limp in
//     death.
// Body length 1 in the mesh; i_ps.w scales it, i_q turns it.
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_seg;    // spine x, part, limb parameter (0 joint .. 1 tip), panel span (0 root .. 1 tip)
layout(location = 3) in vec3 a_piv;    // joint the part turns about
layout(location = 4) in vec4 i_ps;     // position (feet; a fish's centre), body length (m)
layout(location = 5) in vec4 i_q;      // rotation quaternion
layout(location = 6) in vec4 i_anim;   // gait phase (rad), gait 0 still .. 1 full speed, flinch 0..1, crouch 0..1
layout(location = 7) in vec4 i_ex;     // wings open 0..1 (bird) or threat (large mammal), death 0..1, seed 0..1, clock (s)
uniform mat4 u_viewproj;
uniform int u_kind;                    // 0 mouse, 1 large mammal, 2 lizard, 3 bird, 4 fish
uniform vec4 u_rig0;                   // neck x, neck y, tail root x, tail root y
uniform vec4 u_rig1;                   // body centre height, hip height
out vec3 v_world, v_n, v_rest, v_rn;
out vec4 v_seg;
out float v_w, v_seed;

const float PI = 3.14159265;
vec3 rot_x(vec3 v, float a) { float c = cos(a), s = sin(a); return vec3(v.x, c * v.y - s * v.z, s * v.y + c * v.z); }
vec3 rot_y(vec3 v, float a) { float c = cos(a), s = sin(a); return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z); }
vec3 rot_z(vec3 v, float a) { float c = cos(a), s = sin(a); return vec3(c * v.x - s * v.y, s * v.x + c * v.y, v.z); }
vec3 qrot(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
float hash1(float n) { return fract(sin(n * 12.9898) * 43758.5453); }

float ph, gait, moving, flinch, crouch, wings, death, seed, clock, bound;

// Sideways offset of the spine at x (+z: the right side).
float spine_lat(float x) {
    float d = max(u_rig0.z - x, 0.0), lat = 0.0;   // d: distance down the tail
    if (u_kind == 2) {
        // Lizard: the trunk swings in a standing wave with the legs (girdles to
        // one side, the middle to the other), the tail in a travelling wave.
        float xc = clamp(x, -0.55, 0.35);
        lat = moving * mix(0.03, 0.06, gait) * sin(ph) * cos(PI * (xc + 0.12) / 0.75);
        lat += d * (moving * 0.07 * sin(ph - 3.0 * d) + 0.04 * sin(clock * 0.5 + seed * 6.0) * d);
    } else if (u_kind == 4) {
        // Fish: a travelling wave from head to tail, growing toward the tail;
        // a C-start bend when it bolts.
        float e = 0.45 - x;
        lat = mix(0.35, 1.0, gait) * (0.01 + 0.11 * e * e) * sin(ph - 6.0 * e);
        lat += flinch * 0.3 * x * x;
    } else if (u_kind <= 1) {
        // Mammals: the tail sways at rest and swings a little in the run.
        lat = d * (0.06 * sin(clock * 1.1 + seed * 9.0) * (1.0 - gait) + 0.05 * gait * sin(ph));
    }
    // Death: still, with a slight twist to one side (the curl itself is ventral, spine_vert).
    return lat * (1.0 - death) + death * 0.04 * x * x;
}
// Upward offset of the spine at x.
float spine_vert(float x) {
    float d = max(u_rig0.z - x, 0.0), v = 0.0;
    if (u_kind <= 1 && death < 1.0) {
        // Bounding (a fleeing mouse): the back arches and stretches with each leap.
        float xc = clamp(x, -0.6, 0.35), a = (xc + 0.12) / 0.45;
        v = bound * 0.05 * sin(ph) * (1.0 - a * a);
        // The tail lifts off the ground in the run and tucks in when hiding.
        v += d * (0.1 * gait - 0.02 - 0.08 * crouch);
        v *= 1.0 - death;
    }
    // Death: limp, head and tail curled toward the belly (hanging, when held
    // up by the middle; curled on the ground, when lying on its side).
    if (u_kind != 4) {
        float xc = clamp(x, u_rig0.z, 0.5);
        v -= death * (0.25 * xc * xc + 0.25 * d * d / (1.0 + d));
    }
    return v;
}

void main() {
    ph = i_anim.x; gait = i_anim.y; flinch = i_anim.z; crouch = i_anim.w;
    wings = i_ex.x; death = i_ex.y; seed = i_ex.z; clock = i_ex.w;
    moving = smoothstep(0.02, 0.12, gait);
    bound = u_kind == 0 ? smoothstep(0.55, 0.85, gait) : u_kind == 1 ? 0.5 * smoothstep(0.7, 0.95, gait) : 0.0;
    float alive = 1.0 - death, idle = (1.0 - smoothstep(0.05, 0.3, gait)) * alive * (1.0 - crouch);
    int part = int(a_seg.y + 0.5);
    vec3 p = a_pos, n = a_normal, piv = a_piv;
    float sx = a_seg.x, t = a_seg.z;
    float neck_x = u_rig0.x, neck_y = u_rig0.y, centre_y = u_rig1.x, hip_y = u_rig1.y;
    float threat = u_kind == 1 ? wings : 0.0;
    float peck = 0.0;

    // ---- legs: swing about the hip, foot lifted on the way forward
    if (part >= 1 && part <= 4) {
        bool front = part <= 2, left = part == 1 || part == 3;
        float side = left ? -1.0 : 1.0;
        // Trot: diagonal pairs. Bound: the forelegs together, then the hind legs.
        float off_walk = (part == 1 || part == 4) ? 0.0 : PI;
        float off_bound = (front ? 0.0 : PI) + (left ? 0.0 : 0.3);
        float off = mix(off_walk, off_bound, bound), c = ph + off;
        vec3 r = p - piv;
        if (u_kind <= 1) {
            float a = moving * (0.25 + 0.45 * gait) * sin(c);
            r = rot_z(r, a); n = rot_z(n, a);
            r.y += moving * 0.3 * hip_y * max(cos(c), 0.0) * t;
            // Dead: forelegs folded back, hind legs forward, all drawn in.
            float fold = death * (front ? -0.7 : 0.7);
            r = rot_z(r, fold) * (1.0 - 0.2 * death); n = rot_z(n, fold);
        } else if (u_kind == 2) {
            float a = moving * mix(0.4, 0.7, gait) * sin(c) * side;
            r = rot_y(r, a); n = rot_y(n, a);
            r.y += moving * 0.5 * hip_y * max(cos(c), 0.0) * t;
            float fold = death * side * (front ? 0.5 : -0.5);
            r = rot_y(r, fold); n = rot_y(n, fold);
        } else if (u_kind == 3) {
            // Bird: both legs together; tucked while airborne, trailed in flight.
            float air = moving * max(sin(ph), 0.0);
            float a = -0.5 * air - 1.3 * wings + 0.6 * death;
            r = rot_z(r, a); n = rot_z(n, a);
        }
        r.y *= 1.0 - 0.5 * crouch; r.x *= 1.0 - 0.3 * crouch;
        p = piv + r;
    }

    // ---- wings: folded against the flank, or open and beating
    if (part == 5 && u_kind == 3) {
        float side = piv.z < 0.0 ? -1.0 : 1.0, fold = 1.0 - wings;
        vec3 r = p - piv; r.z *= side; n.z *= side;   // work on the right wing
        r.z *= mix(0.62, 1.0, wings); r.x *= mix(0.6, 1.0, wings);   // the folded wing's feathers overlap: shorter and narrower
        // Fold: the span swings back along the body and the wing stands up
        // against the flank, tip down toward the tail and in toward the rump.
        r = rot_y(r, -0.5 * PI * fold); n = rot_y(n, -0.5 * PI * fold);
        r = rot_x(r, -0.5 * PI * fold); n = rot_x(n, -0.5 * PI * fold);
        r = rot_z(r, 0.2 * fold); n = rot_z(n, 0.2 * fold);
        r = rot_y(r, -0.12 * fold); n = rot_y(n, -0.12 * fold);
        r += vec3(-0.01, 0.01, 0.06) * fold;
        // Beat about the shoulder (11 Hz); dead wings droop.
        float beat = wings * alive * (0.15 + 0.95 * sin(clock * 6.2831853 * 11.0 + seed * 6.0)) - 0.5 * death;
        r = rot_x(r, -beat); n = rot_x(n, -beat);
        r.z *= side; n.z *= side;
        p = piv + r;
    }

    // ---- fins: the paired fins flutter about their roots
    if (part == 9 && abs(piv.z) > 0.001) {
        float side = piv.z < 0.0 ? -1.0 : 1.0, a = alive * side * 0.35 * sin(clock * 5.0 + side + seed * 6.0);
        vec3 r = p - piv; r = rot_x(r, a); n = rot_x(n, a); p = piv + r;
    }

    // ---- a bird's tail: flicked up now and then, lowered in flight
    if (part == 10) {
        float fl = idle * smoothstep(0.0, 0.05, fract(clock * 0.3 + seed * 3.0)) * (1.0 - smoothstep(0.1, 0.2, fract(clock * 0.3 + seed * 3.0)));
        float a = -0.35 * fl + 0.12 * wings + 0.25 * death;
        vec3 r = p - piv; r = rot_z(r, a); n = rot_z(n, a); p = piv + r;
    }

    // ---- head: about the neck
    float pitch = 0.0, yaw = 0.0;
    if (u_kind == 0) {
        float sniff = step(0.5, fract(clock * 0.23 + seed));
        pitch = idle * (0.12 * sin(clock * 9.0) * sniff + 0.1 * sin(clock * 0.7 + seed * 5.0)) - 0.12 * gait * alive;
        yaw = idle * 0.45 * sin(clock * 0.5 + seed * 7.0);
    } else if (u_kind == 1) {
        pitch = idle * 0.08 * sin(clock * 0.9) + 0.3 * threat * (0.6 + 0.4 * sin(clock * 9.0));
        yaw = idle * 0.35 * sin(clock * 0.4 + seed * 4.0);
    } else if (u_kind == 2) {
        pitch = idle * 0.14;
        yaw = idle * 0.6 * (hash1(floor(clock * 0.5 + seed * 3.0)) - 0.5);
    } else if (u_kind == 3) {
        float pp = fract(clock * 0.35 + seed);
        peck = idle * smoothstep(0.0, 0.06, pp) * (1.0 - smoothstep(0.12, 0.2, pp));
        pitch = -1.1 * peck + 0.15 * flinch;
        yaw = idle * (1.0 - peck) * 1.0 * (hash1(floor(clock * 1.3 + seed * 7.0)) - 0.5);
    }
    pitch -= crouch * 0.15;
    if (u_kind != 4) { pitch += death * -0.5; yaw += death * 0.4; }
    float wh = smoothstep(neck_x - 0.03, neck_x + 0.03, sx);
    if (wh > 0.0) {
        vec3 r = p - vec3(neck_x, neck_y, 0.0);
        r = rot_y(rot_z(r, pitch * wh), -yaw * wh); n = rot_y(rot_z(n, pitch * wh), -yaw * wh);
        p = vec3(neck_x, neck_y, 0.0) + r;
    }

    // ---- spine: bend sideways and arch, each vertex at its spine coordinate
    {
        float e = 0.01;
        float lat = spine_lat(sx), sl = (spine_lat(sx + e) - spine_lat(sx - e)) / (2.0 * e);
        float ang = atan(sl);
        vec3 r = p - vec3(sx, 0.0, 0.0);
        r = rot_y(r, -ang); n = rot_y(n, -ang);
        p = vec3(sx + r.x, r.y, lat + r.z);
        float vt = spine_vert(sx), sv = (spine_vert(sx + e) - spine_vert(sx - e)) / (2.0 * e);
        float va = atan(sv);
        r = p - vec3(sx, centre_y, 0.0);
        r = rot_z(r, va); n = rot_z(n, va);
        p = vec3(sx, centre_y + vt, 0.0) + r;
    }

    // ---- whole body: crouch, bob and rock with the gait, flinch, threat lunges
    float body_pitch = 0.0, lift = 0.0, fwd = 0.0;
    if (u_kind <= 1) {
        lift = moving * (1.0 - bound) * 0.025 * (0.5 + 0.5 * cos(2.0 * ph)) + bound * 0.12 * max(sin(ph + 0.6), 0.0);
        body_pitch = bound * 0.18 * sin(ph) + 0.25 * flinch - 0.08 * threat;
        lift += 0.12 * flinch * centre_y / 0.18;
        fwd = threat * 0.06 * max(sin(clock * 9.0), 0.0);
    } else if (u_kind == 2) {
        lift = 0.012 * gait + 0.03 * flinch;
        body_pitch = 0.1 * flinch + 0.05 * idle;
    } else if (u_kind == 3) {
        lift = moving * (1.0 - wings) * 0.12 * max(sin(ph), 0.0) + 0.05 * flinch;
        body_pitch = -0.32 * wings - 0.3 * peck + 0.15 * flinch;
        lift += wings * alive * 0.03 * sin(clock * 6.2831853 * 11.0 + seed * 6.0 - 1.2);
    }
    lift -= crouch * 0.45 * hip_y;
    if (u_kind == 4) lift = 0.0;
    {
        vec3 r = p - vec3(0.0, centre_y, 0.0);
        r = rot_z(r, body_pitch); n = rot_z(n, body_pitch);
        p = vec3(fwd, centre_y + lift, 0.0) + r;
    }

    vec3 wp = qrot(i_q, p * i_ps.w) + i_ps.xyz;
    v_world = wp; v_n = qrot(i_q, n); v_rest = a_pos; v_rn = a_normal; v_seg = a_seg; v_seed = seed;
    vec4 clip = u_viewproj * vec4(wp, 1.0);
    v_w = clip.w; gl_Position = scene_clip(clip);
}
