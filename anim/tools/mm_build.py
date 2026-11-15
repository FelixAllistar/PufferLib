#!/usr/bin/env python3
"""Offline motion-matching database builder.

Reads BVH clips, retargets them onto a target skeleton (a JSON description from
gltf_skeleton.py), adds a mirrored copy of every clip, extracts the simulation
root, foot contacts and matching features, and writes an MMDB file for mm.c.
The README documents every step and the byte layout.

Usage:
    mm_build.py --clips DIR_OR_BVH [...] --skeleton target.json --map MAP --out db.mmdb
                [--manifest tags.json] [--fps 30] [--features features.json]
                [--source-yaw-deg 0] [--source-rest-bvh rest.bvh] [--no-mirror]
                [--body-shape body.json] [--auto-stance 0.82] [--format 2]
                [--drop-joints REGEX]

MAP is a JSON file {"source joint": "target joint", ...} or one of the built-in
names soma77_to_mixamo, soma77_to_officer and identity.
"""
import argparse
import copy
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bvh  # noqa: E402
import mmmath as mm  # noqa: E402

MAGIC = b"MMDB"
VERSION = 2
MAX_TAGS = 32
SMALL_BOX = 16
LARGE_BOX = 64

FLAG_CONTACT_LEFT = 1
FLAG_CONTACT_RIGHT = 2
FLAG_TRAJECTORY_EXTRAPOLATED = 4
FLAG_NOT_SEARCHABLE = 8

BLOCK_TYPES = {"position": 0, "velocity": 1, "trajectory_position": 2, "trajectory_direction": 3}

# Roles are the joints the builder and runtime need by function. Each role is
# named by its SOMA joint and resolved through the bone map, unless a features
# file overrides it with a target joint name.
ROLE_SOURCE_NAMES = {
    "hips": "Hips",
    "left_upleg": "LeftLeg", "left_leg": "LeftShin", "left_foot": "LeftFoot", "left_toe": "LeftToeBase",
    "right_upleg": "RightLeg", "right_leg": "RightShin", "right_foot": "RightFoot", "right_toe": "RightToeBase",
    "left_arm": "LeftArm", "right_arm": "RightArm",
}
ROLE_ORDER = ["left_upleg", "left_leg", "left_foot", "left_toe",
              "right_upleg", "right_leg", "right_foot", "right_toe"]

DEFAULT_FEATURES = {
    "trajectory_frames": [10, 20, 30],
    "blocks": [
        {"name": "left_foot_position", "type": "position", "joint": "@left_foot", "weight": 0.75},
        {"name": "right_foot_position", "type": "position", "joint": "@right_foot", "weight": 0.75},
        {"name": "left_foot_velocity", "type": "velocity", "joint": "@left_foot", "weight": 1.0},
        {"name": "right_foot_velocity", "type": "velocity", "joint": "@right_foot", "weight": 1.0},
        {"name": "hips_velocity", "type": "velocity", "joint": "@hips", "weight": 1.0},
        {"name": "trajectory_position", "type": "trajectory_position", "weight": 1.0},
        {"name": "trajectory_direction", "type": "trajectory_direction", "weight": 1.5},
    ],
    # Root extraction, in frames at the database rate (Gaussian standard deviations).
    "root_position_sigma": 3.0,
    "root_facing_sigma": 6.0,
    # Contacts: ankle height above its rest height and ankle speed, in meters and
    # meters per second (scaled by the skeleton's units_per_meter).
    "contact_height": 0.05,
    "contact_speed": 0.35,
    "contact_min_frames": 3,
    # Frames this close to the end of a clip are never returned by the search.
    "end_margin": 10,
    "default_tags": ["stand"],
    "tag_vocabulary": ["tactical", "civilian", "scared", "suspect", "injured", "stand", "crouch",
                       "walk", "jog", "run", "idle", "start", "stop", "turn", "strafe", "backpedal"],
}


def _soma_to_mixamo(prefix):
    m = {"Hips": "Hips", "Spine1": "Spine", "Spine2": "Spine1", "Chest": "Spine2",
         "Neck1": "Neck", "Head": "Head"}
    for side in ("Left", "Right"):
        m[f"{side}Shoulder"] = f"{side}Shoulder"
        m[f"{side}Arm"] = f"{side}Arm"
        m[f"{side}ForeArm"] = f"{side}ForeArm"
        m[f"{side}Hand"] = f"{side}Hand"
        # SOMA's thumb starts at the carpometacarpal joint like Mixamo's, but its
        # other fingers start with a metacarpal joint near the wrist, so SOMA
        # finger joint k + 1 (the knuckle for k = 1) is Mixamo joint k.
        for k in range(1, 4):
            m[f"{side}HandThumb{k}"] = f"{side}HandThumb{k}"
        for finger in ("Index", "Middle", "Ring", "Pinky"):
            for k in range(1, 4):
                m[f"{side}Hand{finger}{k + 1}"] = f"{side}Hand{finger}{k}"
        m[f"{side}Leg"] = f"{side}UpLeg"
        m[f"{side}Shin"] = f"{side}Leg"
        m[f"{side}Foot"] = f"{side}Foot"
        m[f"{side}ToeBase"] = f"{side}ToeBase"
    return {k: prefix + v for k, v in m.items()}


def builtin_map(name):
    if name == "soma77_to_mixamo":
        return _soma_to_mixamo("mixamorig:")
    if name == "soma77_to_officer":
        # The SWAT officer (ready.glb) is a Mixamo rig; its joint names were read
        # from the file and every mapped name below exists in it.
        return _soma_to_mixamo("mixamorig:")
    if name == "identity":
        return None
    raise ValueError(f"unknown built-in map {name}")


def load_map(arg):
    if os.path.exists(arg):
        with open(arg) as f:
            return json.load(f)
    return builtin_map(arg)


# ---------------------------------------------------------------- skeletons


def skeleton_from_json(path):
    with open(path) as f:
        doc = json.load(f)
    joints = doc["joints"]
    return {
        "names": [j["name"] for j in joints],
        "parents": [j["parent"] for j in joints],
        "rest_pos": np.array([j["translation"] for j in joints], dtype=np.float64),
        "rest_rot": mm.quat_normalize(np.array([j["rotation"] for j in joints], dtype=np.float64)),
        "inverse_bind": np.array([j.get("inverse_bind", list(np.eye(4).reshape(16))) for j in joints]),
        "units_per_meter": float(doc.get("units_per_meter", 1.0)),
    }


def skeleton_from_bvh(clip, units_per_meter=100.0):
    """Use a BVH hierarchy (zero-rotation rest pose) as a target skeleton."""
    J = len(clip["names"])
    rest_pos = clip["offsets"].copy()
    world, _ = mm.forward_kinematics(clip["parents"], rest_pos[None], np.tile([0.0, 0, 0, 1], (1, J, 1)))
    ibm = []
    for j in range(J):
        m = np.eye(4)
        m[:3, 3] = -world[0, j]
        ibm.append(list(m.T.reshape(16)))
    return {"names": list(clip["names"]), "parents": list(clip["parents"]), "rest_pos": rest_pos,
            "rest_rot": np.tile([0.0, 0, 0, 1], (J, 1)), "inverse_bind": np.array(ibm),
            "units_per_meter": units_per_meter}


def rest_world(skel):
    return mm.forward_kinematics(skel["parents"], skel["rest_pos"][None], skel["rest_rot"][None])


def hip_height(names, parents, world_pos, hips):
    """Height of the hips above the lowest foot or toe joint of a rest pose."""
    low = [i for i, n in enumerate(names) if any(k in n.lower() for k in ("foot", "toe"))]
    if not low:
        raise ValueError("cannot find foot joints to measure the hip height")
    return world_pos[hips, 1] - min(world_pos[i, 1] for i in low)


# ---------------------------------------------------------------- retargeting


def resample(clip, fps):
    """Resample a parsed BVH to fps with linear positions and normalized lerp rotations."""
    src_fps = 1.0 / clip["frame_time"]
    if abs(src_fps - fps) < 1e-3:
        return clip
    F = clip["rotations"].shape[0]
    duration = (F - 1) / src_fps
    n = int(np.floor(duration * fps + 1e-6)) + 1
    t = np.arange(n) / fps * src_fps
    i0 = np.clip(np.floor(t).astype(int), 0, F - 1)
    i1 = np.clip(i0 + 1, 0, F - 1)
    a = (t - i0)[:, None, None]
    out = dict(clip)
    out["positions"] = clip["positions"][i0] * (1 - a) + clip["positions"][i1] * a
    q0 = clip["rotations"][i0]
    q1 = clip["rotations"][i1]
    q1 = np.where(np.sum(q0 * q1, axis=-1, keepdims=True) < 0, -q1, q1)
    out["rotations"] = mm.quat_normalize(q0 * (1 - a) + q1 * a)
    out["frame_time"] = 1.0 / fps
    return out


def retarget(clip, target, bone_map, source_rest_rot=None, source_yaw=0.0, hips_source="Hips"):
    """Retarget a parsed BVH clip onto target by world rotation change from rest.

    For each mapped target joint, W_target(t) = (W_src(t) * W_src_rest^-1) * W_target_rest,
    converted to a local rotation under the animated parent. Unmapped target joints
    keep their rest local rotation. The hips take the source hips' world translation
    change from rest scaled by the ratio of hip heights.

    Returns (local_rot (F, Jt, 4), hips_world_pos (F, 3), mapped (Jt,) bool) in
    target space, where mapped marks the joints driven by a source joint.
    """
    src_names = clip["names"]
    parents_s = clip["parents"]
    F = clip["rotations"].shape[0]
    Js = len(src_names)
    yaw_q = mm.quat_from_yaw(source_yaw)
    src_pos, src_rot = mm.forward_kinematics(parents_s, clip["positions"], clip["rotations"])
    src_pos = mm.quat_rotate(yaw_q, src_pos)
    src_rot = mm.quat_mul(yaw_q, src_rot)
    rest_local_rot = np.tile([0.0, 0, 0, 1], (1, Js, 1)) if source_rest_rot is None else source_rest_rot[None]
    rest_pos_s, rest_rot_s = mm.forward_kinematics(parents_s, clip["offsets"][None], rest_local_rot)
    rest_pos_s = mm.quat_rotate(yaw_q, rest_pos_s[0])
    rest_rot_s = mm.quat_mul(yaw_q, rest_rot_s[0])

    t_names = target["names"]
    t_parents = target["parents"]
    Jt = len(t_names)
    t_rest_pos, t_rest_rot = rest_world(target)
    t_rest_pos, t_rest_rot = t_rest_pos[0], t_rest_rot[0]

    if bone_map is None:
        bone_map = {n: n for n in src_names if n in t_names}
    src_index = {n: i for i, n in enumerate(src_names)}
    t_index = {n: i for i, n in enumerate(t_names)}
    mapped = {}
    for s, t in bone_map.items():
        if s in src_index and t in t_index:
            mapped[t_index[t]] = src_index[s]
    hips_t = t_index[bone_map.get(hips_source, hips_source)]
    hips_s = src_index[hips_source]

    world_rot = np.zeros((F, Jt, 4))
    local_rot = np.zeros((F, Jt, 4))
    for j in range(Jt):
        p = t_parents[j]
        parent_rot = np.tile([0.0, 0, 0, 1], (F, 1)) if p < 0 else world_rot[:, p]
        if j in mapped:
            s = mapped[j]
            delta = mm.quat_mul(src_rot[:, s], mm.quat_inv(rest_rot_s[s]))
            world_rot[:, j] = mm.quat_mul(delta, t_rest_rot[j])
            local_rot[:, j] = mm.quat_mul(mm.quat_inv(parent_rot), world_rot[:, j])
        else:
            local_rot[:, j] = target["rest_rot"][j]
            world_rot[:, j] = mm.quat_mul(parent_rot, local_rot[:, j])

    # The source rest pose (zero rotations and zero root translation) is assumed
    # to stand on the motion's floor, so its hips are the reference for the
    # translation change.
    hs = hip_height(src_names, parents_s, rest_pos_s, hips_s)
    ht = hip_height(t_names, t_parents, t_rest_pos, hips_t)
    hips_world = t_rest_pos[hips_t] + (ht / hs) * (src_pos[:, hips_s] - rest_pos_s[hips_s])
    animated = np.zeros(Jt, dtype=bool)
    animated[list(mapped.keys())] = True
    return mm.quat_unroll(mm.quat_normalize(local_rot)), hips_world, animated


# ---------------------------------------------------------------- arm-body penetration


# Test points along each arm and their radii in meters (scaled by units_per_meter):
# the elbow, three forearm points, the wrist and the palm.
ARM_POINTS = (("elbow", 0.0, 0.040), ("forearm", 0.25, 0.040), ("forearm", 0.5, 0.038),
              ("forearm", 0.75, 0.036), ("wrist", 1.0, 0.034), ("palm", 1.0, 0.030))


def load_body_shape(path, target):
    """Read gltf_body_shape.py volumes as [(joint index, slices (S, 6))] for the target."""
    with open(path) as f:
        shape = json.load(f)
    vols = []
    for v in shape["volumes"]:
        if v["joint"] not in target["names"]:
            continue
        s = np.array([[x["y0"], x["y1"], x["cx"], x["cz"], x["ax"], x["az"]] for x in v["slices"]])
        vols.append((target["names"].index(v["joint"]), s))
    if not vols:
        raise ValueError(f"no volume in {path} names a joint of the target skeleton")
    return vols


def arm_chains(names):
    """(upper arm, forearm, hand, palm) joint indices per side, found by name suffix."""
    def find(suffix):
        hits = [i for i, n in enumerate(names) if n.split(":")[-1] == suffix]
        return hits[0] if hits else None
    chains = []
    for side in ("Left", "Right"):
        js = [find(side + s) for s in ("Arm", "ForeArm", "Hand", "HandMiddle1")]
        if None not in js[:3]:
            if js[3] is None:
                js[3] = js[2]
            chains.append(tuple(js))
    return chains


def push_out(p, r, wp, wr, vols):
    """Displacement (F, 3) that moves sphere centres p (F, 3) of radius r out of every volume.

    Inside a slice the point moves radially from the slice axis to the
    (radius-inflated) elliptical surface; volumes are applied in turn.
    """
    q0 = p.copy()
    hit = np.zeros(p.shape[0], dtype=bool)
    for j, slices in vols:
        inv = mm.quat_inv(wr[:, j])
        q = mm.quat_rotate(inv, p - wp[:, j])
        for y0, y1, cx, cz, ax, az in slices:
            dx = (q[:, 0] - cx) / (ax + r)
            dz = (q[:, 2] - cz) / (az + r)
            rho = np.sqrt(dx * dx + dz * dz)
            m = (q[:, 1] >= y0) & (q[:, 1] < y1) & (rho < 1.0)
            if not m.any():
                continue
            rho = np.maximum(rho, 1e-3)
            nq = q.copy()
            nq[:, 0] = cx + (q[:, 0] - cx) / rho
            nq[:, 2] = cz + (q[:, 2] - cz) / rho
            q = np.where(m[:, None], nq, q)
            hit |= m
        p = mm.quat_rotate(wr[:, j], q) + wp[:, j]
    # Points no slice touched return exactly zero (no rotation round-off).
    return np.where(hit[:, None], p - q0, 0.0)


def rotation_between(u, v):
    """Shortest-arc quaternions (F, 4) rotating directions u onto v."""
    u = u / np.maximum(np.linalg.norm(u, axis=-1, keepdims=True), 1e-9)
    v = v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-9)
    q = np.concatenate([np.cross(u, v), 1.0 + np.sum(u * v, axis=-1, keepdims=True)], axis=-1)
    return mm.quat_normalize(q + np.array([1e-9, 0, 0, 0]))


def arm_points(wp, wr, chain):
    a, b, c, palm = chain
    out = []
    for kind, s, r in ARM_POINTS:
        if kind == "palm":
            out.append((wp[:, palm] if palm != c else wp[:, c], r))
        else:
            out.append((wp[:, b] + s * (wp[:, c] - wp[:, b]), r))
    return out


def penetration(wp, wr, vols, chains, upm):
    """Per-frame deepest penetration (no radius) of any arm test point, in meters."""
    worst = np.zeros(wp.shape[0])
    for chain in chains:
        for p, _ in arm_points(wp, wr, chain):
            worst = np.maximum(worst, np.linalg.norm(push_out(p, 0.0, wp, wr, vols), axis=-1) / upm)
    return worst


def resolve_arm_penetration(target, local_rot, hips_world, hips, vols, iterations=12, sigma=1.5,
                            tolerance=0.0005):
    """Push elbows, forearms and hands out of the body volumes with two-bone IK.

    Per side and iteration: the elbow's push moves the IK pole, the largest push
    of the forearm, wrist and palm points moves the hand target, both pushes are
    smoothed over time, and the upper arm and forearm are re-solved so the hand
    keeps its world rotation. Frames that need no push are left unchanged. It
    stops when no push exceeds tolerance meters; poses the arm cannot reach
    out of (a hand forced to the far side of the chest) stay partly inside.
    Returns (local_rot, penetration before, penetration after) with penetration
    per frame in meters.
    """
    parents = target["parents"]
    upm = target["units_per_meter"]
    F = local_rot.shape[0]
    local_pos = np.repeat(target["rest_pos"][None], F, axis=0)
    local_pos[:, hips] = hips_world
    chains = arm_chains(target["names"])
    local_rot = local_rot.copy()
    wp, wr = mm.forward_kinematics(parents, local_pos, local_rot)
    before = penetration(wp, wr, vols, chains, upm)
    for _ in range(iterations):
        moved = False
        for chain in chains:
            a, b, c, _palm = chain
            pts = arm_points(wp, wr, chain)
            pe = push_out(pts[0][0], pts[0][1] * upm, wp, wr, vols)
            ph = np.zeros_like(pe)
            for p, r in pts[1:]:
                d = push_out(p, r * upm, wp, wr, vols)
                bigger = np.linalg.norm(d, axis=-1) > np.linalg.norm(ph, axis=-1)
                ph = np.where(bigger[:, None], d, ph)
            worst = max(np.linalg.norm(pe, axis=-1).max(), np.linalg.norm(ph, axis=-1).max())
            if worst <= tolerance * upm:
                continue
            pe = gaussian_smooth(pe, sigma)
            ph = gaussian_smooth(ph, sigma)
            mask = (np.linalg.norm(pe, axis=-1) + np.linalg.norm(ph, axis=-1)) > 1e-5 * upm
            if not mask.any():
                continue
            moved = True
            A, B, C = wp[:, a], wp[:, b], wp[:, c]
            T, pole = C + ph, B + pe
            l1 = np.linalg.norm(B - A, axis=-1)
            l2 = np.linalg.norm(C - B, axis=-1)
            d = T - A
            dl = np.linalg.norm(d, axis=-1)
            dn = d / np.maximum(dl, 1e-9)[:, None]
            dl = np.clip(dl, np.abs(l1 - l2) + 1e-4 * upm, l1 + l2 - 1e-4 * upm)
            pv = pole - A
            pv = pv - dn * np.sum(pv * dn, axis=-1, keepdims=True)
            pn = pv / np.maximum(np.linalg.norm(pv, axis=-1, keepdims=True), 1e-9)
            cos_a = np.clip((l1 * l1 + dl * dl - l2 * l2) / (2 * l1 * dl), -1.0, 1.0)
            E = A + l1[:, None] * (cos_a[:, None] * dn + np.sqrt(1 - cos_a ** 2)[:, None] * pn)
            Tc = A + dn * dl[:, None]
            r1 = rotation_between(B - A, E - A)
            fore_dir = mm.quat_rotate(r1, C - B)
            r2 = rotation_between(fore_dir, Tc - E)
            up_w = mm.quat_mul(r1, wr[:, a])
            fo_w = mm.quat_mul(r2, mm.quat_mul(r1, wr[:, b]))
            pa = parents[a]
            parent_w = wr[:, pa] if pa >= 0 else np.tile([0.0, 0, 0, 1], (F, 1))
            new_a = mm.quat_mul(mm.quat_inv(parent_w), up_w)
            new_b = mm.quat_mul(mm.quat_inv(up_w), fo_w)
            new_c = mm.quat_mul(mm.quat_inv(fo_w), wr[:, c])
            for j, q in ((a, new_a), (b, new_b), (c, new_c)):
                local_rot[:, j] = np.where(mask[:, None], q, local_rot[:, j])
            wp, wr = mm.forward_kinematics(parents, local_pos, local_rot)
        if not moved:
            break
    after = penetration(wp, wr, vols, chains, upm)
    return mm.quat_unroll(mm.quat_normalize(local_rot)), before, after


# ---------------------------------------------------------------- mirroring


def mirror_partner(names):
    """Index of the Left/Right partner of every joint (itself when unpaired)."""
    index = {n: i for i, n in enumerate(names)}
    out = []
    for n in names:
        if "Left" in n:
            out.append(index.get(n.replace("Left", "Right"), index[n]))
        elif "Right" in n:
            out.append(index.get(n.replace("Right", "Left"), index[n]))
        else:
            out.append(index[n])
    return out


def mirror(target, local_rot, hips_world, hips, animated=None):
    """Mirror a retargeted clip across the character's sagittal plane at frame 0.

    Every joint takes its partner's world rotation change from rest, reflected:
    W'_j(t) = (M D_partner(t) M) W_rest_j with D = W(t) W_rest^-1 and M the
    reflection, which for quaternions maps (x, y, z, w) to (x, -y, -z, w) when the
    plane is x = 0. The motion is rotated into the frame of the starting facing
    first so the plane is the character's sagittal plane, and rotated back after.
    Joints not marked in animated (unmapped helpers and props) keep their rest
    local rotation, as they do in unmirrored clips.
    """
    parents = target["parents"]
    F, J = local_rot.shape[:2]
    partner = mirror_partner(target["names"])
    root_rot = np.tile([0.0, 0, 0, 1], (F, 1))
    _, wr = mm.forward_kinematics(parents, np.zeros((F, J, 3)), local_rot)
    _, rest_rot = rest_world(target)
    rest_rot = rest_rot[0]
    yaw0 = initial_facing(target, wr, hips_world, hips)
    q0 = mm.quat_from_yaw(yaw0)
    refl = np.array([1.0, -1.0, -1.0, 1.0])
    new_world = np.zeros_like(wr)
    for j in range(J):
        p = partner[j]
        delta = mm.quat_mul(wr[:, p], mm.quat_inv(rest_rot[p]))
        delta = mm.quat_mul(mm.quat_inv(q0), mm.quat_mul(delta, q0))
        delta = delta * refl
        delta = mm.quat_mul(q0, mm.quat_mul(delta, mm.quat_inv(q0)))
        new_world[:, j] = mm.quat_mul(delta, rest_rot[j])
    new_local = np.zeros_like(local_rot)
    for j in range(J):
        p = parents[j]
        parent_rot = root_rot if p < 0 else new_world[:, p]
        if animated is not None and not animated[j]:
            new_local[:, j] = target["rest_rot"][j]
            new_world[:, j] = mm.quat_mul(parent_rot, new_local[:, j])
        else:
            new_local[:, j] = mm.quat_mul(mm.quat_inv(parent_rot), new_world[:, j])
    # Reflect the hips path in the same plane, through the frame 0 hips position.
    origin = hips_world[0] * np.array([1.0, 0.0, 1.0])
    h = mm.quat_rotate(mm.quat_inv(q0), hips_world - origin)
    h[:, 0] = -h[:, 0]
    new_hips = mm.quat_rotate(q0, h) + origin
    return mm.quat_unroll(mm.quat_normalize(new_local)), new_hips


def initial_facing(target, world_rot, hips_world, hips):
    """Facing yaw of frame 0 from the rest-relative hips rotation."""
    _, rest_rot = rest_world(target)
    d = mm.quat_mul(world_rot[0, hips], mm.quat_inv(rest_rot[0, hips]))
    fwd = mm.quat_rotate(d, np.array([0.0, 0.0, 1.0]))
    return float(np.arctan2(fwd[0], fwd[2]))


# ---------------------------------------------------------------- root, features, contacts


def gaussian_smooth(x, sigma):
    """Gaussian filter along axis 0 with odd-reflection padding.

    Odd reflection (x[-k] = 2 x[0] - x[k]) keeps linear trends at the clip
    edges, so a constant-velocity path is reproduced exactly.
    """
    if sigma <= 0:
        return x.copy()
    radius = int(np.ceil(3 * sigma))
    k = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    k /= k.sum()
    n = x.shape[0]
    idx = np.arange(-radius, n + radius)
    out = np.zeros_like(x)
    padded = np.empty((n + 2 * radius,) + x.shape[1:])
    for i, t in enumerate(idx):
        if t < 0:
            padded[i] = 2 * x[0] - x[min(-t, n - 1)]
        elif t >= n:
            padded[i] = 2 * x[n - 1] - x[max(2 * (n - 1) - t, 0)]
        else:
            padded[i] = x[t]
    for i, w in enumerate(k):
        out += w * padded[i: i + n]
    return out


def compute_root(world_pos, roles, cfg):
    """Simulation root: hips projected to the ground and Gaussian smoothed; facing
    is the smoothed horizontal forward vector of the hip and shoulder lines."""
    hips = world_pos[:, roles["hips"]] * np.array([1.0, 0.0, 1.0])
    pos = gaussian_smooth(hips, cfg["root_position_sigma"])
    across = (world_pos[:, roles["left_upleg"]] - world_pos[:, roles["right_upleg"]]
              + world_pos[:, roles["left_arm"]] - world_pos[:, roles["right_arm"]])
    fwd = np.cross(across, np.array([0.0, 1.0, 0.0]))
    fwd[:, 1] = 0.0
    fwd /= np.linalg.norm(fwd, axis=1, keepdims=True)
    fwd = gaussian_smooth(fwd, cfg["root_facing_sigma"])
    yaw = np.arctan2(fwd[:, 0], fwd[:, 2])
    return pos, yaw


def velocity(x, fps):
    """Central differences in time (one-sided at the ends), per second."""
    v = np.zeros_like(x)
    if x.shape[0] > 1:
        v[1:-1] = (x[2:] - x[:-2]) * (0.5 * fps)
        v[0] = (x[1] - x[0]) * fps
        v[-1] = (x[-1] - x[-2]) * fps
    return v


def to_root(v, yaw):
    """Rotate world vectors into root space (yaw only)."""
    return mm.quat_rotate(mm.quat_from_yaw(-yaw), v)


def debounce(c, min_frames):
    """Remove off-gaps and then on-runs shorter than min_frames."""
    c = c.copy()
    for value in (False, True):
        n = len(c)
        i = 0
        while i < n:
            if c[i] == value:
                j = i
                while j < n and c[j] == value:
                    j += 1
                if j - i < min_frames and i > 0 and j < n:
                    c[i:j] = not value
                i = j
            else:
                i += 1
    return c


def contacts(world_pos, rest_pos, roles, fps, cfg, upm):
    out = []
    for side in ("left", "right"):
        j = roles[f"{side}_foot"]
        h = world_pos[:, j, 1] - rest_pos[j, 1]
        s = np.linalg.norm(velocity(world_pos[:, j], fps), axis=1)
        c = (h < cfg["contact_height"] * upm) & (s < cfg["contact_speed"] * upm)
        out.append(debounce(c, cfg["contact_min_frames"]))
    return out


def trajectory(root_pos, root_yaw, frames, fps):
    """Future root positions and facings for every frame, with extrapolation past
    the clip end: position continues with the final root velocity (mean of the
    last 5 frames) and the facing is held at the final facing."""
    F = root_pos.shape[0]
    tail = min(5, F - 1)
    end_vel = (root_pos[-1] - root_pos[-1 - tail]) / max(tail, 1) if tail > 0 else np.zeros(3)
    K = len(frames)
    fpos = np.zeros((F, K, 3))
    fyaw = np.zeros((F, K))
    for k, off in enumerate(frames):
        t = np.arange(F) + off
        inside = np.minimum(t, F - 1)
        fpos[:, k] = root_pos[inside] + end_vel[None] * np.maximum(t - (F - 1), 0)[:, None]
        fyaw[:, k] = root_yaw[inside]
    return fpos, fyaw


def clip_features(world_pos, root_pos, root_yaw, blocks, joints, frames, fps):
    F = world_pos.shape[0]
    cols = []
    fpos, fyaw = trajectory(root_pos, root_yaw, frames, fps)
    for b in blocks:
        if b["type"] == "position":
            cols.append(to_root(world_pos[:, joints[b["name"]]] - root_pos, root_yaw))
        elif b["type"] == "velocity":
            cols.append(to_root(velocity(world_pos[:, joints[b["name"]]], fps), root_yaw))
        elif b["type"] == "trajectory_position":
            rel = to_root(fpos - root_pos[:, None], root_yaw[:, None])
            cols.append(rel[..., [0, 2]].reshape(F, -1))
        elif b["type"] == "trajectory_direction":
            d = fyaw - root_yaw[:, None]
            cols.append(np.stack([np.sin(d), np.cos(d)], axis=-1).reshape(F, -1))
    return np.concatenate(cols, axis=1)


# ---------------------------------------------------------------- clip processing


def resolve_roles(target, bone_map, cfg):
    names = target["names"]
    roles = {}
    for role, src in ROLE_SOURCE_NAMES.items():
        override = cfg.get("roles", {}).get(role)
        name = override if override else (bone_map.get(src, src) if bone_map else src)
        if name not in names:
            raise ValueError(f"role {role}: target joint {name} not found")
        roles[role] = names.index(name)
    if target["parents"][roles["hips"]] >= 0:
        raise ValueError("the hips must be a root joint (parent -1) of the target skeleton")
    return roles


def process_variant(target, local_rot, hips_world, roles, cfg, fps, blocks, joints):
    """Compute root, root-relative locals, contacts and raw features of one clip."""
    parents = target["parents"]
    F, J = local_rot.shape[:2]
    hips = roles["hips"]
    local_pos = np.repeat(target["rest_pos"][None], F, axis=0)
    local_pos[:, hips] = hips_world
    wp, _ = mm.forward_kinematics(parents, local_pos, local_rot)
    root_pos, root_yaw = compute_root(wp, roles, cfg)
    rest_pos, _ = rest_world(target)
    upm = target["units_per_meter"]
    c_left, c_right = contacts(wp, rest_pos[0], roles, fps, cfg, upm)
    feats = clip_features(wp, root_pos, root_yaw, blocks, joints, cfg["trajectory_frames"], fps)
    # Express the hips relative to the simulation root.
    inv_root = mm.quat_from_yaw(-root_yaw)
    rel_rot = local_rot.copy()
    rel_pos = local_pos.copy()
    rel_rot[:, hips] = mm.quat_mul(inv_root, local_rot[:, hips])
    rel_pos[:, hips] = mm.quat_rotate(inv_root, local_pos[:, hips] - root_pos)
    # Other root joints (props, helpers) keep their rest transform relative to
    # the root; world_pos is recomputed so that it matches what the runtime does.
    wp, _ = mm.forward_kinematics(parents, rel_pos, rel_rot, root_pos, mm.quat_from_yaw(root_yaw))
    flags = np.zeros(F, dtype=np.uint8)
    flags[c_left] |= FLAG_CONTACT_LEFT
    flags[c_right] |= FLAG_CONTACT_RIGHT
    horizon = max(cfg["trajectory_frames"])
    flags[max(F - horizon, 0):] |= FLAG_TRAJECTORY_EXTRAPOLATED
    flags[max(F - cfg["end_margin"], 0):] |= FLAG_NOT_SEARCHABLE
    return {
        "local_rot": mm.quat_unroll(mm.quat_normalize(rel_rot)),
        "hips_pos": rel_pos[:, hips],
        "root_pos": root_pos,
        "root_yaw": root_yaw,
        "flags": flags,
        "features": feats,
        "world_pos": wp,
    }


def clip_tags(path, root, manifest, cfg):
    name = os.path.splitext(os.path.basename(path))[0]
    if manifest is not None and name in manifest:
        entry = manifest[name]
        if isinstance(entry, dict):
            return name, [str(v) for v in entry.values() if v]
        return name, list(entry)
    rel = os.path.relpath(path, root) if root else os.path.basename(path)
    tokens = []
    for part in rel.replace("\\", "/").split("/"):
        for tok in os.path.splitext(part)[0].replace("-", "_").split("_"):
            tokens.append(tok.lower())
    tags = [t for t in cfg["tag_vocabulary"] if t in tokens]
    if not any(t in tags for t in ("stand", "crouch")):
        tags += cfg["default_tags"]
    return name, tags


def collect_clips(inputs):
    out = []
    for item in inputs:
        if os.path.isdir(item):
            for dirpath, _, files in sorted(os.walk(item)):
                for f in sorted(files):
                    if f.lower().endswith(".bvh"):
                        out.append((os.path.join(dirpath, f), item))
        else:
            out.append((item, None))
    return out


def build(clip_paths, target, bone_map, cfg, fps, manifest=None, do_mirror=True,
          source_yaw=0.0, source_rest_rot=None, body_volumes=None, auto_stance=None):
    """Build the in-memory database. Returns a dict that write_mmdb serializes."""
    roles = resolve_roles(target, bone_map, cfg)
    blocks = []
    joints = {}
    offset = 0
    K = len(cfg["trajectory_frames"])
    for b in cfg["blocks"]:
        size = 3 if b["type"] in ("position", "velocity") else 2 * K
        joint = -1
        if b["type"] in ("position", "velocity"):
            jn = b["joint"]
            joint = roles[jn[1:]] if jn.startswith("@") else target["names"].index(jn)
            joints[b["name"]] = joint
        blocks.append({"name": b["name"], "type": b["type"], "joint": joint, "offset": offset,
                       "size": size, "weight": float(b["weight"])})
        offset += size
    clips, tag_names = [], []
    body_stats = []
    source_hips = "Hips"
    if bone_map is not None:
        inverse = {v: k for k, v in bone_map.items()}
        source_hips = inverse.get(target["names"][roles["hips"]], "Hips")
    for path, root in clip_paths:
        name, tags = clip_tags(path, root, manifest, cfg)
        clip = resample(bvh.read(path), fps)
        local_rot, hips_world, animated = retarget(clip, target, bone_map, source_rest_rot, source_yaw,
                                                   source_hips)
        if body_volumes is not None:
            local_rot, pen0, pen1 = resolve_arm_penetration(target, local_rot, hips_world, roles["hips"],
                                                            body_volumes)
            body_stats.append((pen0, pen1))
        if auto_stance:
            # Stance becomes a per-frame tag (frame_stance, applied in to_stored).
            tags = [t for t in tags if t not in ("stand", "crouch")]
        variants = [(name, tags, local_rot, hips_world)]
        if do_mirror:
            mr, mh = mirror(target, local_rot, hips_world, roles["hips"], animated)
            variants.append((name + "_mirrored", tags + ["mirrored"], mr, mh))
        for vname, vtags, lr, hw in variants:
            v = process_variant(target, lr, hw, roles, cfg, fps, blocks, joints)
            v["name"] = vname
            v["tags"] = vtags
            for t in vtags:
                if t not in tag_names:
                    tag_names.append(t)
            clips.append(v)
    if body_stats:
        b0 = np.concatenate([x[0] for x in body_stats])
        b1 = np.concatenate([x[1] for x in body_stats])
        print(f"arm-body penetration: {100 * (b0 > 0.005).mean():.1f}% of frames deeper than 5 mm before, "
              f"{100 * (b1 > 0.005).mean():.1f}% after; mean depth {100 * b0.mean():.2f} -> "
              f"{100 * b1.mean():.2f} cm, max {100 * b0.max():.1f} -> {100 * b1.max():.1f} cm")
    if auto_stance:
        for t in ("stand", "crouch"):
            if t not in tag_names:
                tag_names.append(t)
    if len(tag_names) > MAX_TAGS:
        raise ValueError(f"too many tags ({len(tag_names)} > {MAX_TAGS})")
    raw = np.concatenate([c["features"] for c in clips], axis=0)
    means = raw.mean(axis=0)
    scales = np.ones(raw.shape[1])
    for b in blocks:
        sl = slice(b["offset"], b["offset"] + b["size"])
        std = raw[:, sl].std(axis=0).mean()
        scales[sl] = max(std, 1e-8) / b["weight"]
    return {"target": target, "roles": roles, "blocks": blocks, "clips": clips, "tags": tag_names,
            "fps": fps, "means": means, "scales": scales, "raw_features": raw,
            "trajectory_frames": list(cfg["trajectory_frames"]), "auto_stance": auto_stance}


# ---------------------------------------------------------------- per-frame stance


STANCE_LEAVE_MARGIN = 0.06   # Leave crouch above ratio + this.
STANCE_MIN_RUN = 6           # Frames; shorter stance runs join their neighbour.
STANCE_SIGMA = 3.0           # Gaussian smoothing of the hips height, frames.


def frame_stance(hips_y, standing, floor, ratio, leave_margin=STANCE_LEAVE_MARGIN, min_run=STANCE_MIN_RUN,
                 sigma=STANCE_SIGMA):
    """Per-frame crouch mask of one clip from its hips height.

    The hips height above the rest floor, relative to the standing hips
    height, is Gaussian smoothed; a frame enters crouch below ratio and leaves
    it above ratio + leave_margin (hysteresis; the first frame is crouched when
    it is below ratio). Runs shorter than min_run frames then take the stance
    of the run before them (the first run that of the run after it), unless the
    clip has a single run.
    """
    h = (np.asarray(hips_y, dtype=np.float64) - floor) / standing
    h = gaussian_smooth(h[:, None], sigma)[:, 0]
    crouch = np.zeros(len(h), dtype=bool)
    state = bool(h[0] < ratio)
    for i, x in enumerate(h):
        if state and x > ratio + leave_margin:
            state = False
        elif not state and x < ratio:
            state = True
        crouch[i] = state
    changed = True
    while changed:
        changed = False
        runs = []
        i = 0
        while i < len(crouch):
            j = i
            while j < len(crouch) and crouch[j] == crouch[i]:
                j += 1
            runs.append((i, j))
            i = j
        if len(runs) < 2:
            break
        for k, (i, j) in enumerate(runs):
            if j - i < min_run:
                crouch[i:j] = crouch[i - 1] if k > 0 else crouch[j]
                changed = True
                break
    return crouch


def standing_floor(names, parents, rest_pos, rest_rot, hips):
    """(standing hips height, floor height) of a target's rest pose."""
    wp, _ = mm.forward_kinematics(parents, np.asarray(rest_pos, dtype=np.float64)[None],
                                  np.asarray(rest_rot, dtype=np.float64)[None])
    standing = hip_height(names, parents, wp[0], hips)
    return standing, wp[0][hips, 1] - standing


def apply_auto_stance(st, ratio, verbose=True):
    """Label every frame of a stored database crouch or stand (frame tags) from
    the hips height, and remove stand and crouch from the clip tags."""
    tags = st["tags"]
    for t in ("stand", "crouch"):
        if t not in tags:
            tags.append(t)
    if len(tags) > MAX_TAGS:
        raise ValueError(f"too many tags ({len(tags)} > {MAX_TAGS})")
    bit_stand, bit_crouch = 1 << tags.index("stand"), 1 << tags.index("crouch")
    standing, floor = standing_floor(st["names"], st["parents"], st["rest_pos"], st["rest_rot"], st["hips"])
    ft = st["frame_tags"] & ~np.uint32(bit_stand | bit_crouch)
    changes = 0
    for c in st["clips"]:
        c["tags"] &= ~(bit_stand | bit_crouch)
        sl = slice(c["start"], c["start"] + c["length"])
        crouch = frame_stance(st["hips_pos"][sl, 1], standing, floor, ratio)
        ft[sl] |= np.where(crouch, np.uint32(bit_crouch), np.uint32(bit_stand))
        if crouch.any() and not crouch.all():
            changes += 1
    st["frame_tags"] = ft.astype(np.uint32)
    if verbose:
        n = len(ft)
        print(f"auto-stance {ratio}: {100.0 * ((ft & bit_crouch) != 0).sum() / max(n, 1):.1f}% of frames crouch, "
              f"{changes} of {len(st['clips'])} clips change stance")
    return st


def measured_stance(target, hips_world, hips, ratio):
    """'crouch' when a clip's median hips height above the rest floor is below
    ratio times the target's standing hips height, else 'stand' (clip-level
    stance, used by version 1 files through the majority of frame tags)."""
    standing, floor = standing_floor(target["names"], target["parents"], target["rest_pos"], target["rest_rot"], hips)
    return "crouch" if np.median(hips_world[:, 1] - floor) < ratio * standing else "stand"


# ---------------------------------------------------------------- quantization

SQRT1_2 = 0.7071067811865476
QUAT_BITS = 15
QUAT_MAX = (1 << QUAT_BITS) - 1
OTHERS = np.array([[1, 2, 3], [0, 2, 3], [0, 1, 3], [0, 1, 2]])
CONSTANT_JOINT_DEG = 0.01     # A joint whose rotation never leaves this cone is stored once.


def quat_encode(q):
    """Smallest-three encoding of quaternions (..., 4) into uint16 (..., 3).

    The largest-magnitude component is dropped (made positive by negating the
    quaternion) and its index i goes into the top bits of the first two words
    (word 0 bit 15 = i & 1, word 1 bit 15 = i >> 1); the other three components,
    in index order, lie in [-1/sqrt 2, 1/sqrt 2] and are stored as 15-bit
    unsigned values round((c * sqrt 2 + 1) / 2 * 32767).
    """
    q = np.asarray(q, dtype=np.float64)
    q = q / np.linalg.norm(q, axis=-1, keepdims=True)
    i = np.argmax(np.abs(q), axis=-1)
    big = np.take_along_axis(q, i[..., None], axis=-1)
    q = np.where(big < 0, -q, q)
    c = np.take_along_axis(q, OTHERS[i], axis=-1)
    v = np.rint((np.clip(c / SQRT1_2, -1.0, 1.0) + 1.0) * 0.5 * QUAT_MAX).astype(np.uint16)
    v[..., 0] |= ((i & 1) << 15).astype(np.uint16)
    v[..., 1] |= ((i >> 1) << 15).astype(np.uint16)
    return v


def quat_decode(v):
    """Inverse of quat_encode (the runtime's mm.c decoder does the same in float32)."""
    v = np.asarray(v, dtype=np.uint16)
    i = ((v[..., 0] >> 15) | ((v[..., 1] >> 15) << 1)).astype(np.int64)
    c = ((v & 0x7FFF).astype(np.float64) * (2.0 / QUAT_MAX) - 1.0) * SQRT1_2
    big = np.sqrt(np.maximum(1.0 - np.sum(c * c, axis=-1), 0.0))
    q = np.zeros(v.shape[:-1] + (4,))
    np.put_along_axis(q, OTHERS[i], c, axis=-1)
    np.put_along_axis(q, i[..., None], big[..., None], axis=-1)
    return q


def quat_angle_deg(a, b):
    d = np.abs(np.sum(mm.quat_normalize(np.asarray(a, np.float64)) * mm.quat_normalize(np.asarray(b, np.float64)),
                      axis=-1))
    return np.degrees(2.0 * np.arccos(np.clip(d, -1.0, 1.0)))


def feature_steps(features):
    """Per-feature quantization step: the smallest power of two with which
    every stored value fits in int16. Dequantized values k * step are exact
    float32 numbers."""
    m = np.abs(features).max(axis=0).astype(np.float64)
    m = np.maximum(m, 1e-30)
    return np.exp2(np.ceil(np.log2(m / 32767.0))).astype(np.float32)


# ---------------------------------------------------------------- serialization


def _string(s):
    b = s.encode("utf-8")
    pad = (-len(b)) % 4
    return struct.pack("<I", len(b)) + b + b"\0" * pad


def _pad4(b):
    return b + b"\0" * ((-len(b)) % 4)


def compute_boxes(features, size):
    """Per-box minimums and maximums over consecutive runs of size frames (the
    last box is shorter); keeps the input dtype."""
    N, F = features.shape
    n = (N + size - 1) // size
    padded = np.concatenate([features, np.repeat(features[-1:], n * size - N, axis=0)], axis=0)
    seg = padded.reshape(n, size, F)
    return seg.min(axis=1), seg.max(axis=1)


def to_stored(db):
    """The arrays a database file holds (float32, root relative, normalized
    features), from build(). write_v1 and write_v2 serialize this form and
    read_mmdb returns it, so a v1 file converts to v2 exactly as a build does."""
    target = db["target"]
    clips = db["clips"]
    tag_index = {t: i for i, t in enumerate(db["tags"])}
    rec, start = [], 0
    for c in clips:
        n = c["local_rot"].shape[0]
        bits = 0
        for t in c["tags"]:
            bits |= 1 << tag_index[t]
        rec.append({"name": c["name"], "start": start, "length": n, "tags": bits})
        start += n
    st = {
        "fps": float(db["fps"]),
        "names": list(target["names"]), "parents": list(target["parents"]),
        "rest_pos": np.asarray(target["rest_pos"], np.float32), "rest_rot": np.asarray(target["rest_rot"], np.float32),
        "inverse_bind": np.asarray(target["inverse_bind"], np.float32).reshape(-1, 16),
        "hips": int(db["roles"]["hips"]), "roles": [int(db["roles"][r]) for r in ROLE_ORDER],
        "tags": list(db["tags"]), "clips": rec,
        "frame_tags": np.zeros(start, dtype=np.uint32),
        "trajectory_frames": list(db["trajectory_frames"]),
        "blocks": [{"name": b["name"], "type": BLOCK_TYPES[b["type"]], "joint": int(b["joint"]),
                    "offset": int(b["offset"]), "size": int(b["size"]), "weight": float(b["weight"])}
                   for b in db["blocks"]],
        "means": np.asarray(db["means"], np.float32), "scales": np.asarray(db["scales"], np.float32),
        "rotations": np.concatenate([c["local_rot"] for c in clips]).astype(np.float32),
        "hips_pos": np.concatenate([c["hips_pos"] for c in clips]).astype(np.float32),
        "root_pos": np.concatenate([c["root_pos"] for c in clips]).astype(np.float32),
        "root_yaw": np.concatenate([c["root_yaw"] for c in clips]).astype(np.float32),
        "flags": np.concatenate([c["flags"] for c in clips]).astype(np.uint8),
        "features": ((db["raw_features"] - db["means"]) / db["scales"]).astype(np.float32),
    }
    if db.get("auto_stance"):
        apply_auto_stance(st, db["auto_stance"])
    return st


def _header(st, version):
    J, N, F, K = len(st["names"]), st["rotations"].shape[0], st["means"].shape[0], len(st["trajectory_frames"])
    out = bytearray()
    out += MAGIC
    out += struct.pack("<I", version)
    out += struct.pack("<f", st["fps"])
    out += struct.pack("<IIIIIIIIII", J, N, len(st["clips"]), len(st["tags"]), F, len(st["blocks"]), K,
                       SMALL_BOX, LARGE_BOX, 0)
    out += struct.pack("<i", st["hips"])
    out += struct.pack("<8i", *st["roles"])
    for j in range(J):
        out += _string(st["names"][j])
        out += struct.pack("<i", st["parents"][j])
        out += struct.pack("<3f", *st["rest_pos"][j])
        out += struct.pack("<4f", *st["rest_rot"][j])
        out += struct.pack("<16f", *st["inverse_bind"][j])
    for t in st["tags"]:
        out += _string(t)
    return out


def _clips_and_blocks(st, fold_frame_tags):
    out = bytearray()
    for c in st["clips"]:
        bits = c["tags"]
        if fold_frame_tags:
            # Version 1 has no frame tags: a clip gets every frame tag that most
            # of its frames have.
            ft = st["frame_tags"][c["start"]:c["start"] + c["length"]]
            for t in range(len(st["tags"])):
                if 2 * int(((ft >> np.uint32(t)) & np.uint32(1)).sum()) > c["length"]:
                    bits |= 1 << t
        out += _string(c["name"])
        out += struct.pack("<III", c["start"], c["length"], bits)
    out += struct.pack(f"<{len(st['trajectory_frames'])}i", *st["trajectory_frames"])
    for b in st["blocks"]:
        out += _string(b["name"])
        out += struct.pack("<IiIIf", b["type"], b["joint"], b["offset"], b["size"], b["weight"])
    out += st["means"].astype("<f4").tobytes()
    out += st["scales"].astype("<f4").tobytes()
    return out


def write_v1(st, path):
    N = st["rotations"].shape[0]
    out = _header(st, 1)
    out += _clips_and_blocks(st, True)
    out += st["rotations"].astype("<f4").tobytes()
    out += st["hips_pos"].astype("<f4").tobytes()
    out += st["root_pos"].astype("<f4").tobytes()
    out += st["root_yaw"].astype("<f4").tobytes()
    out += st["flags"].astype(np.uint8).tobytes() + b"\0" * ((-N) % 4)
    norm = st["features"].astype("<f4")
    out += norm.tobytes()
    for size in (SMALL_BOX, LARGE_BOX):
        lo, hi = compute_boxes(norm, size)
        out += lo.astype("<f4").tobytes()
        out += hi.astype("<f4").tobytes()
    out += MAGIC
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


def joint_tracks(st, drop_joints=None):
    """(track index per joint or -1, dropped mask, constant rotation per joint).

    Joints matching the drop_joints regular expression (full match) are not
    stored and play their rest rotation. Joints whose rotation stays within
    CONSTANT_JOINT_DEG of their first frame's (props, sockets, end joints,
    unanimated fingers) are stored once. Every other joint is a track."""
    import re
    names = st["names"]
    J = len(names)
    rot = st["rotations"]
    dropped = np.zeros(J, dtype=bool)
    if drop_joints:
        rx = re.compile(drop_joints)
        dropped = np.array([rx.fullmatch(n) is not None for n in names])
        needed = {st["hips"]} | set(st["roles"]) | {b["joint"] for b in st["blocks"] if b["joint"] >= 0}
        bad = [names[j] for j in needed if dropped[j]]
        if bad:
            raise ValueError(f"--drop-joints matches joints the runtime needs: {', '.join(bad)}")
    const = np.asarray(st["rest_rot"], np.float32).copy()
    track = np.full(J, -1, dtype=np.int32)
    A = 0
    for j in range(J):
        if dropped[j]:
            continue
        if j != st["hips"] and quat_angle_deg(rot[:, j], rot[:1, j]).max() <= CONSTANT_JOINT_DEG:
            const[j] = rot[0, j]
            continue
        track[j] = A
        A += 1
    return track, dropped, const


def write_v2(st, path, drop_joints=None, report=None):
    """Write a version 2 (compact) file. report, a dict, receives sizes and errors."""
    N, J = st["rotations"].shape[:2]
    F = st["means"].shape[0]
    track, dropped, const = joint_tracks(st, drop_joints)
    animated = np.where(track >= 0)[0]
    out = _header(st, 2)
    out += _clips_and_blocks(st, False)
    steps = feature_steps(st["features"])
    out += steps.astype("<f4").tobytes()
    out += struct.pack("<I", len(animated))
    for j in range(J):
        out += struct.pack("<iI4f", int(track[j]), int(dropped[j]), *const[j])
    sizes = {"header and metadata": len(out)}
    rot_err = 0.0
    chunk = max(1, 4_000_000 // max(len(animated), 1))
    rbytes = bytearray()
    for a in range(0, N, chunk):
        q = st["rotations"][a:a + chunk][:, animated]
        v = quat_encode(q)
        if q.size:
            rot_err = max(rot_err, float(quat_angle_deg(quat_decode(v), q).max()))
        rbytes += v.astype("<u2").tobytes()
    const_err = 0.0
    for j in range(J):
        if track[j] < 0 and not dropped[j]:
            const_err = max(const_err, float(quat_angle_deg(st["rotations"][:, j], const[j][None]).max()))
    out += _pad4(bytes(rbytes))
    sizes["rotations"] = len(_pad4(bytes(rbytes)))
    n0 = len(out)
    out += st["hips_pos"].astype("<f4").tobytes()
    out += st["root_pos"].astype("<f4").tobytes()
    out += st["root_yaw"].astype("<f4").tobytes()
    out += _pad4(st["flags"].astype(np.uint8).tobytes())
    out += st["frame_tags"].astype("<u4").tobytes()
    sizes["hips, root, flags, frame tags"] = len(out) - n0
    k = np.rint(st["features"].astype(np.float64) / steps.astype(np.float64)).astype(np.int16)
    n0 = len(out)
    out += _pad4(k.astype("<i2").tobytes())
    sizes["features"] = len(out) - n0
    n0 = len(out)
    for size in (SMALL_BOX, LARGE_BOX):
        lo, hi = compute_boxes(k, size)
        out += _pad4(lo.astype("<i2").tobytes())
        out += _pad4(hi.astype("<i2").tobytes())
    sizes["boxes"] = len(out) - n0
    out += MAGIC
    with open(path, "wb") as f:
        f.write(out)
    if report is not None:
        deq = k.astype(np.float32) * steps
        report.update({"bytes": len(out), "sizes": sizes, "tracks": int(len(animated)),
                       "constant": int(((track < 0) & ~dropped).sum()), "dropped": int(dropped.sum()),
                       "rotation_error_deg": rot_err, "constant_error_deg": const_err,
                       "feature_error": float(np.abs(deq - st["features"]).max()) if N else 0.0})
    return len(out)


def write_mmdb(db, path, version=2, drop_joints=None, report=None):
    """Serialize a build() result. Version 2 is the compact format; version 1
    stores float32 rotations and features."""
    st = to_stored(db)
    if version == 1:
        if drop_joints:
            raise ValueError("--drop-joints needs version 2")
        return write_v1(st, path)
    return write_v2(st, path, drop_joints, report)


class _Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, n):
        b = self.d[self.p:self.p + n]
        if len(b) != n:
            raise ValueError("truncated MMDB")
        self.p += n
        return b

    def unpack(self, fmt):
        return struct.unpack(fmt, self.take(struct.calcsize(fmt)))

    def string(self):
        (n,) = self.unpack("<I")
        b = self.take((n + 3) & ~3)
        return b[:n].decode("utf-8")

    def array(self, dtype, count, pad=True):
        dt = np.dtype(dtype)
        nbytes = dt.itemsize * count
        a = np.frombuffer(self.take(nbytes), dtype=dt).copy()
        if pad:
            self.take((-nbytes) % 4)
        return a


def read_mmdb(path):
    """Read a version 1 or 2 file into the to_stored() form (rotations and
    features dequantized for version 2, which also gives "dropped" and "version")."""
    with open(path, "rb") as f:
        r = _Reader(f.read())
    if r.take(4) != MAGIC:
        raise ValueError("not an MMDB file")
    (version, fps) = r.unpack("<If")
    J, N, C, T, F, B_, K, sbox, lbox, _ = r.unpack("<10I")
    if version not in (1, 2) or (sbox, lbox) != (SMALL_BOX, LARGE_BOX):
        raise ValueError(f"unsupported MMDB version {version} or box sizes")
    (hips,) = r.unpack("<i")
    roles = list(r.unpack("<8i"))
    st = {"version": version, "fps": fps, "hips": hips, "roles": roles, "names": [], "parents": []}
    rp, rr, ib = [], [], []
    for _ in range(J):
        st["names"].append(r.string())
        st["parents"].append(r.unpack("<i")[0])
        rp.append(r.unpack("<3f"))
        rr.append(r.unpack("<4f"))
        ib.append(r.unpack("<16f"))
    st["rest_pos"], st["rest_rot"] = np.array(rp, np.float32), np.array(rr, np.float32)
    st["inverse_bind"] = np.array(ib, np.float32)
    st["tags"] = [r.string() for _ in range(T)]
    st["clips"] = []
    for _ in range(C):
        name = r.string()
        start, length, bits = r.unpack("<III")
        st["clips"].append({"name": name, "start": start, "length": length, "tags": bits})
    st["trajectory_frames"] = list(r.unpack(f"<{K}i"))
    st["blocks"] = []
    for _ in range(B_):
        name = r.string()
        t, j, o, s, w = r.unpack("<IiIIf")
        st["blocks"].append({"name": name, "type": t, "joint": j, "offset": o, "size": s, "weight": w})
    st["means"] = r.array("<f4", F)
    st["scales"] = r.array("<f4", F)
    if version == 1:
        st["rotations"] = r.array("<f4", N * J * 4).reshape(N, J, 4)
        st["dropped"] = np.zeros(J, dtype=bool)
    else:
        steps = r.array("<f4", F)
        (A,) = r.unpack("<I")
        track, dropped, const = [], [], []
        for _ in range(J):
            t, fl, *q = r.unpack("<iI4f")
            track.append(t)
            dropped.append(bool(fl & 1))
            const.append(q)
        v = r.array("<u2", N * A * 3).reshape(N, A, 3)
        rot = np.repeat(np.array(const, np.float32)[None], N, axis=0)
        for j, t in enumerate(track):
            if t >= 0:
                rot[:, j] = quat_decode(v[:, t]).astype(np.float32)
        st["rotations"] = rot
        st["dropped"] = np.array(dropped)
    st["hips_pos"] = r.array("<f4", N * 3).reshape(N, 3)
    st["root_pos"] = r.array("<f4", N * 3).reshape(N, 3)
    st["root_yaw"] = r.array("<f4", N)
    st["flags"] = r.array("u1", N)
    if version == 1:
        st["frame_tags"] = np.zeros(N, dtype=np.uint32)
        st["features"] = r.array("<f4", N * F).reshape(N, F)
        for n in (N + SMALL_BOX - 1) // SMALL_BOX, (N + LARGE_BOX - 1) // LARGE_BOX:
            r.take(2 * 4 * n * F)
    else:
        st["frame_tags"] = r.array("<u4", N)
        st["features"] = r.array("<i2", N * F).reshape(N, F).astype(np.float32) * steps
        for n in (N + SMALL_BOX - 1) // SMALL_BOX, (N + LARGE_BOX - 1) // LARGE_BOX:
            r.array("<i2", n * F)
            r.array("<i2", n * F)
    if r.take(4) != MAGIC or r.p != len(r.d):
        raise ValueError("bad MMDB tail")
    return st


def print_report(path, rep):
    s = ", ".join(f"{k} {v / 1e6:.2f} MB" for k, v in rep["sizes"].items())
    print(f"{path}: version 2, {rep['bytes'] / 1e6:.2f} MB ({s}); {rep['tracks']} rotation tracks, "
          f"{rep['constant']} constant joints, {rep['dropped']} dropped; max rotation error "
          f"{rep['rotation_error_deg']:.4f} deg (constant joints {rep['constant_error_deg']:.4f} deg), "
          f"max feature error {rep['feature_error']:.2e}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--clips", nargs="+", required=True)
    ap.add_argument("--skeleton", help="target skeleton JSON from gltf_skeleton.py")
    ap.add_argument("--skeleton-from-bvh", help="use this BVH's hierarchy as the target skeleton")
    ap.add_argument("--map", default="soma77_to_mixamo")
    ap.add_argument("--manifest")
    ap.add_argument("--features", help="JSON overriding entries of the default feature configuration")
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--source-yaw-deg", type=float, default=0.0,
                    help="yaw applied to the source so it faces +Z like the target")
    ap.add_argument("--source-rest-bvh", help="take the source rest pose from frame 0 of this BVH")
    ap.add_argument("--no-mirror", action="store_true")
    ap.add_argument("--auto-stance", type=float, metavar="RATIO",
                    help="tag every frame crouch or stand by its smoothed hips height (enter crouch below "
                         "RATIO of the standing height, e.g. 0.82, leave above RATIO + 0.06), overriding "
                         "names and manifest")
    ap.add_argument("--format", type=int, choices=(1, 2), default=2,
                    help="file version: 2 (compact, default) or 1 (float32, no frame tags)")
    ap.add_argument("--drop-joints", metavar="REGEX",
                    help="joints whose names fully match REGEX are not stored and play their rest rotation, "
                         "e.g. '.*Hand(Thumb|Index|Middle|Ring|Pinky).*'")
    ap.add_argument("--body-shape", help="body volumes from gltf_body_shape.py; arms are pushed out of them")
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    cfg = copy.deepcopy(DEFAULT_FEATURES)
    if args.features:
        with open(args.features) as f:
            cfg.update(json.load(f))
    if args.skeleton:
        target = skeleton_from_json(args.skeleton)
    elif args.skeleton_from_bvh:
        target = skeleton_from_bvh(bvh.read(args.skeleton_from_bvh))
    else:
        ap.error("--skeleton or --skeleton-from-bvh is required")
    bone_map = load_map(args.map)
    manifest = None
    if args.manifest:
        with open(args.manifest) as f:
            manifest = json.load(f)["clips"]
    rest_rot = None
    if args.source_rest_bvh:
        rest_rot = bvh.read(args.source_rest_bvh)["rotations"][0]
    paths = collect_clips(args.clips)
    if not paths:
        ap.error("no BVH clips found")
    db = build(paths, target, bone_map, cfg, args.fps, manifest, not args.no_mirror,
               np.radians(args.source_yaw_deg), rest_rot,
               load_body_shape(args.body_shape, target) if args.body_shape else None,
               args.auto_stance)
    report = {}
    size = write_mmdb(db, args.out, args.format, args.drop_joints, report)
    if report:
        print_report(args.out, report)
    frames = sum(c["local_rot"].shape[0] for c in db["clips"])
    print(f"wrote {args.out}: {len(db['clips'])} clips, {frames} frames, "
          f"{db['means'].shape[0]} features, {len(db['tags'])} tags, {size} bytes")


if __name__ == "__main__":
    main()
