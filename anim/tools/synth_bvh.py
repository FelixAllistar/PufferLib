#!/usr/bin/env python3
"""Procedural biped BVH generator used for tests and benchmarks.

The skeleton imitates the SOMA layout that Kimodo exports (joint names such as
Root, Hips, Spine1, Spine2, Chest, Neck1, Neck2, Head, LeftShoulder, LeftArm,
LeftForeArm, LeftHand, LeftHandIndex1..4, LeftLeg, LeftShin, LeftFoot,
LeftToeBase, LeftToeEnd and the Right mirror), in centimeters, Y up, facing +Z,
with the left side on +X and a T-pose rest pose built from identity rotations.

The motion is driven by a list of keys (time, speed, movement heading, facing).
The root follows the integrated velocity. Footholds are planned as the gait
phase advances: a stance foot stays exactly where it landed, a swinging foot
travels from its lift-off point to a predicted landing point, and the legs are
solved analytically with two-bone IK, so contacts in the output are exact.

Usage:
    synth_bvh.py OUT_DIR             write the standard test clip set and a manifest
    synth_bvh.py OUT_DIR --random N  write N random locomotion clips (benchmarks)
"""
import json
import math
import multiprocessing
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import bvh  # noqa: E402
import mmmath as mm  # noqa: E402

FPS = 30


def soma_like_skeleton():
    """Return (names, parents, offsets) of the synthetic SOMA-like skeleton."""
    names, parents, offsets = [], [], []

    def add(name, parent, offset):
        names.append(name)
        parents.append(names.index(parent) if parent else -1)
        offsets.append(offset)

    add("Root", None, [0, 0, 0])
    add("Hips", "Root", [0, 95, 0])
    add("Spine1", "Hips", [0, 10, 0])
    add("Spine2", "Spine1", [0, 12, 0])
    add("Chest", "Spine2", [0, 14, 0])
    add("Neck1", "Chest", [0, 14, 0])
    add("Neck2", "Neck1", [0, 5, 0])
    add("Head", "Neck2", [0, 6, 0])
    add("HeadEnd", "Head", [0, 18, 0])
    add("Jaw", "Head", [0, -2, 6])
    add("LeftEye", "Head", [3, 6, 8])
    add("RightEye", "Head", [-3, 6, 8])
    for side, s in (("Left", 1.0), ("Right", -1.0)):
        add(f"{side}Shoulder", "Chest", [4 * s, 10, 0])
        add(f"{side}Arm", f"{side}Shoulder", [12 * s, 0, 0])
        add(f"{side}ForeArm", f"{side}Arm", [26 * s, 0, 0])
        add(f"{side}Hand", f"{side}ForeArm", [25 * s, 0, 0])
        fingers = {"Thumb": [3, -1, 3], "Index": [9, 0, 2], "Middle": [9.5, 0, 0.5],
                   "Ring": [9, 0, -1.5], "Pinky": [8, 0, -3]}
        for finger, base in fingers.items():
            parent = f"{side}Hand"
            for k in range(1, 5):
                off = [base[0] * s, base[1], base[2]] if k == 1 else [3.0 * s, 0, 0]
                add(f"{side}Hand{finger}{k}", parent, off)
                parent = f"{side}Hand{finger}{k}"
    for side, s in (("Left", 1.0), ("Right", -1.0)):
        add(f"{side}Leg", "Hips", [9 * s, -5, 0])
        add(f"{side}Shin", f"{side}Leg", [0, -42, 0])
        add(f"{side}Foot", f"{side}Shin", [0, -41, 0])
        add(f"{side}ToeBase", f"{side}Foot", [0, -6, 13])
        add(f"{side}ToeEnd", f"{side}ToeBase", [0, 0, 6])
    # Twist helpers, as the SOMA skeleton carries them (76 joints in all).
    for side, s in (("Left", 1.0), ("Right", -1.0)):
        add(f"{side}ArmTwist", f"{side}Arm", [13 * s, 0, 0])
        add(f"{side}ForeArmTwist", f"{side}ForeArm", [12 * s, 0, 0])
        add(f"{side}LegTwist", f"{side}Leg", [0, -21, 0])
    # Reorder depth first so the BVH file order equals the joint order.
    order = []

    def visit(j):
        order.append(j)
        for c in range(len(names)):
            if parents[c] == j:
                visit(c)

    visit(0)
    new_index = {old: new for new, old in enumerate(order)}
    names = [names[o] for o in order]
    parents = [new_index[parents[o]] if parents[o] >= 0 else -1 for o in order]
    offsets = [offsets[o] for o in order]
    return names, parents, np.array(offsets, dtype=np.float64)


def _lerp_angle(a, b, t):
    d = (b - a + math.pi) % (2 * math.pi) - math.pi
    return a + d * t


def sample_keys(keys, t):
    """Linearly interpolate (speed, heading, facing) keys; angles in degrees."""
    if t <= keys[0][0]:
        k = keys[0]
        return k[1], math.radians(k[2]), math.radians(k[3])
    for a, b in zip(keys[:-1], keys[1:]):
        if t <= b[0]:
            u = (t - a[0]) / max(b[0] - a[0], 1e-9)
            u = u * u * (3 - 2 * u)
            return (a[1] + (b[1] - a[1]) * u,
                    _lerp_angle(math.radians(a[2]), math.radians(b[2]), u),
                    _lerp_angle(math.radians(a[3]), math.radians(b[3]), u))
    k = keys[-1]
    return k[1], math.radians(k[2]), math.radians(k[3])


def _rot_from_axes(down_dir, forward):
    """Rotation mapping rest -Y to down_dir and rest +Z as close as possible to forward."""
    y = -down_dir / np.linalg.norm(down_dir)
    z = forward - np.dot(forward, y) * y
    z /= np.linalg.norm(z)
    x = np.cross(y, z)
    return mm.quat_from_matrix(np.stack([x, y, z], axis=1))


def generate(keys, duration, crouch=False, start_yaw=0.0, start_pos=(0.0, 0.0)):
    """Generate one clip. Returns (names, parents, offsets, positions, rotations)."""
    names, parents, offsets = soma_like_skeleton()
    J = len(names)
    idx = {n: i for i, n in enumerate(names)}
    F = int(round(duration * FPS))
    dt = 1.0 / FPS
    rot = np.zeros((F, J, 4))
    rot[..., 3] = 1.0
    pos = np.repeat(offsets[None], F, axis=0)

    l1, l2 = 42.0, 41.0
    ankle_h = 95 - 5 - 42 - 41 + 0.0
    width = 10.0
    root = np.array([start_pos[0], 0.0, start_pos[1]])
    phase = 0.0
    feet = {}
    for side, s in (("Left", 1.0), ("Right", -1.0)):
        y0 = math.radians(start_yaw)
        p = root + np.array([math.cos(y0) * s * width, ankle_h, -math.sin(y0) * s * width])
        feet[side] = {"stance": True, "pos": p.copy(), "yaw": y0, "lift": p.copy(),
                      "lift_yaw": y0, "duty": 0.6, "psi": 0.0}
    prev_facing = None
    for f in range(F):
        t = f * dt
        speed, heading, facing = sample_keys(keys, t)
        facing += math.radians(start_yaw)
        heading += math.radians(start_yaw)
        vel = np.array([math.sin(heading), 0.0, math.cos(heading)]) * speed
        if f > 0:
            root = root + vel * dt
        yaw_rate = 0.0 if prev_facing is None else abs(_lerp_angle(prev_facing, facing, 1.0) - prev_facing) / dt
        prev_facing = facing
        sm = speed / 100.0
        cadence = 1.0 + 0.25 * sm
        duty = min(max(0.62 - 0.06 * sm, 0.35), 0.62)
        if crouch:
            cadence *= 0.85
        if speed < 10.0 and yaw_rate < 0.4:
            # Finish the current step, then freeze in double stance.
            in_double = (phase % 0.5) >= 0.03 and (phase % 0.5) <= 0.07
            both = feet["Left"]["stance"] and feet["Right"]["stance"]
            rate = 0.0 if (in_double and both) else 1.2
        else:
            rate = max(cadence, 1.2)
        if f > 0:
            phase += rate * dt
        stance_time = duty / max(rate, 1e-6) if rate > 0 else 0.0
        swing_time = (1 - duty) / max(rate, 1e-6) if rate > 0 else 0.0
        fyaw = mm.quat_from_yaw(facing)
        for side, offset in (("Left", 0.0), ("Right", 0.5)):
            st = feet[side]
            psi = (phase + offset) % 1.0
            wrapped = psi < st["psi"] - 0.5
            st["psi"] = psi
            s = 1.0 if side == "Left" else -1.0
            lateral = mm.quat_rotate(fyaw, np.array([s * width, 0.0, 0.0]))
            if st["stance"] and psi >= st["duty"] and rate > 0:
                st["stance"] = False
                st["lift"] = st["pos"].copy()
                st["lift_yaw"] = st["yaw"]
                st["swing_start"] = st["duty"]
            if not st["stance"]:
                u = 1.0 if wrapped else (psi - st["swing_start"]) / max(1 - st["swing_start"], 1e-6)
                u = min(max(u, 0.0), 1.0)
                to_land = (1 - u) * swing_time
                target = root + vel * (to_land + 0.5 * stance_time) + lateral
                target[1] = ankle_h
                w = u * u * (3 - 2 * u)
                p = st["lift"] + (target - st["lift"]) * w
                p[1] = ankle_h + (8.0 + 3.0 * sm) * math.sin(math.pi * u)
                st["pos"] = p
                st["yaw"] = _lerp_angle(st["lift_yaw"], facing, w)
                if wrapped:
                    st["stance"] = True
                    st["duty"] = duty
                    st["pos"] = target
                    st["yaw"] = facing
        # Root and pelvis.
        drop = 6.0 + 2.5 * sm + (28.0 if crouch else 0.0)
        bob = -1.5 * min(sm, 1.5) * math.cos(4 * math.pi * phase)
        twist = math.radians(6.0) * min(sm, 1.5) * math.sin(2 * math.pi * phase)
        pos[f, idx["Root"]] = root
        rot[f, idx["Root"]] = fyaw
        pos[f, idx["Hips"]] = np.array([0.0, 95.0 - drop + bob, 0.0])
        lean = math.radians(4.0 * min(sm, 2.0) + (20.0 if crouch else 0.0))
        rot[f, idx["Hips"]] = mm.quat_mul(mm.quat_from_yaw(twist), mm.quat_from_axis_angle([1, 0, 0], lean))
        rot[f, idx["Spine1"]] = mm.quat_from_yaw(-0.5 * twist)
        rot[f, idx["Chest"]] = mm.quat_from_yaw(-1.0 * twist)
        rot[f, idx["Neck1"]] = mm.quat_from_axis_angle([1, 0, 0], -lean * 0.6)
        rot[f, idx["Head"]] = mm.quat_mul(mm.quat_from_yaw(0.5 * twist), mm.quat_from_axis_angle([1, 0, 0], -lean * 0.4))
        # Arms hang and swing against the legs.
        swing = math.radians(18.0) * min(sm, 1.8) * math.sin(2 * math.pi * phase)
        for side, s, sign in (("Left", 1.0, 1.0), ("Right", -1.0, -1.0)):
            down = mm.quat_from_axis_angle([0, 0, 1], -s * math.radians(75.0))
            rot[f, idx[f"{side}Arm"]] = mm.quat_mul(mm.quat_from_axis_angle([1, 0, 0], sign * swing), down)
            rot[f, idx[f"{side}ForeArm"]] = mm.quat_from_axis_angle([0, 1, 0], -s * math.radians(20.0 + 10.0 * min(sm, 2.0)))
        # Legs by analytic two-bone IK.
        wp, wr = mm.forward_kinematics(parents, pos[f:f + 1], rot[f:f + 1])
        for side in ("Left", "Right"):
            st = feet[side]
            leg, shin, foot = idx[f"{side}Leg"], idx[f"{side}Shin"], idx[f"{side}Foot"]
            H = wp[0, leg]
            T = st["pos"]
            d = T - H
            dist = np.linalg.norm(d)
            reach = min(max(dist, 1e-3), (l1 + l2) * 0.999)
            u = d / dist
            fwd = np.array([math.sin(st["yaw"]), 0.0, math.cos(st["yaw"])])
            n = fwd - np.dot(fwd, u) * u
            n /= np.linalg.norm(n)
            along = (l1 * l1 - l2 * l2 + reach * reach) / (2 * reach)
            h = math.sqrt(max(l1 * l1 - along * along, 0.0))
            K = H + along * u + h * n
            Tc = H + u * reach
            thigh_w = _rot_from_axes(K - H, fwd)
            shin_w = _rot_from_axes(Tc - K, fwd)
            foot_w = mm.quat_from_yaw(st["yaw"])
            parent_w = wr[0, parents[leg]]
            rot[f, leg] = mm.quat_mul(mm.quat_inv(parent_w), thigh_w)
            rot[f, shin] = mm.quat_mul(mm.quat_inv(thigh_w), shin_w)
            rot[f, foot] = mm.quat_mul(mm.quat_inv(shin_w), foot_w)
    rot = mm.quat_unroll(mm.quat_normalize(rot))
    return names, parents, offsets, pos, rot


def standard_clips():
    """The clip set used by the tests: name -> (keys, duration, crouch, tags)."""
    w, r = 140.0, 350.0
    return {
        "walk_straight": ([(0, w, 0, 0)], 6.0, False, ["tactical", "stand"]),
        "walk_turn90": ([(0, w, 0, 0), (2.5, w, 0, 0), (3.5, w, 90, 90), (6, w, 90, 90)], 6.0, False, ["tactical", "stand"]),
        "walk_turn90_right": ([(0, w, 0, 0), (2.5, w, 0, 0), (3.5, w, -90, -90), (6, w, -90, -90)], 6.0, False, ["civilian", "stand"]),
        "strafe_left": ([(0, 0, 90, 0), (0.6, 110, 90, 0), (6, 110, 90, 0)], 6.0, False, ["tactical", "stand"]),
        "walk_stop": ([(0, w, 0, 0), (2.5, w, 0, 0), (3.2, 0, 0, 0), (6, 0, 0, 0)], 6.0, False, ["tactical", "stand"]),
        "idle_start_walk": ([(0, 0, 0, 0), (2.0, 0, 0, 0), (2.7, w, 0, 0), (6, w, 0, 0)], 6.0, False, ["tactical", "stand"]),
        "idle": ([(0, 0, 0, 0)], 4.0, False, ["tactical", "stand"]),
        "run_straight": ([(0, r, 0, 0)], 5.0, False, ["tactical", "stand"]),
        "walk_run_walk": ([(0, w, 0, 0), (1.5, w, 0, 0), (2.5, r, 0, 0), (4, r, 0, 0), (5, w, 0, 0)], 6.0, False, ["civilian", "stand"]),
        "backpedal": ([(0, 0, 180, 0), (0.6, 100, 180, 0), (5, 100, 180, 0)], 5.0, False, ["tactical", "stand"]),
        "crouch_walk": ([(0, 70, 0, 0)], 5.0, True, ["tactical", "crouch"]),
        "crouch_idle": ([(0, 0, 0, 0)], 3.0, True, ["tactical", "crouch"]),
        "turn_in_place": ([(0, 0, 0, 0), (1.0, 0, 0, 0), (2.5, 0, 0, 90), (4.0, 0, 0, 90)], 4.0, False, ["tactical", "stand"]),
        "walk_circle": ([(0, w, 0, 0)] + [(1 + i * 0.5, w, 20 * (i + 1), 20 * (i + 1)) for i in range(10)], 7.0, False, ["scared", "stand"]),
    }


def random_clip(rng, duration=10.0):
    """A random locomotion clip: piecewise speed, heading and facing changes."""
    keys = []
    t, heading = 0.0, 0.0
    while t < duration:
        speed = float(rng.choice([0.0, 80.0, 140.0, 220.0, 350.0]))
        heading += float(rng.uniform(-120, 120))
        mode = rng.integers(0, 3)
        facing = heading if mode == 0 or speed == 0 else heading + float(rng.choice([-90.0, 90.0, 180.0]))
        keys.append((t, speed, heading, facing))
        t += float(rng.uniform(1.0, 3.0))
    return keys


STYLES = ["tactical", "civilian", "scared", "suspect", "injured"]


def _write_random_clip(job):
    """Write random clip i; each clip has its own seed so workers are independent."""
    out, i = job
    rng = np.random.default_rng([1234, i])
    keys = random_clip(rng)
    crouch = bool(rng.random() < 0.15)
    names, parents, offsets, pos, rot = generate(keys, 10.0, crouch)
    name = f"random_{i:04d}"
    bvh.write(os.path.join(out, name + ".bvh"), names, parents, offsets, pos, rot, 1.0 / FPS, position_joints=(0, 1))
    return name, [STYLES[i % len(STYLES)], "crouch" if crouch else "stand"]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    manifest = {}
    if len(sys.argv) >= 4 and sys.argv[2] == "--random":
        count = int(sys.argv[3])
        with multiprocessing.Pool() as pool:
            for name, tags in pool.imap(_write_random_clip, [(out, i) for i in range(count)]):
                manifest[name] = tags
    else:
        for name, (keys, duration, crouch, tags) in standard_clips().items():
            names, parents, offsets, pos, rot = generate(keys, duration, crouch)
            bvh.write(os.path.join(out, name + ".bvh"), names, parents, offsets, pos, rot, 1.0 / FPS,
                      position_joints=(0, 1))
            manifest[name] = tags
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump({"clips": manifest}, f, indent=1)
    print(f"wrote {len(manifest)} clips to {out}")


if __name__ == "__main__":
    main()
