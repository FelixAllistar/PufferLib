#!/usr/bin/env python3
"""Tests for the offline tools: skeleton extraction, retargeting, mirroring,
root extraction, contacts, features and the MMDB writer.

Run with: python3 tests/test_build.py (the Makefile does this)."""
import copy
import json
import math
import os
import struct
import sys
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import bvh  # noqa: E402
import gltf_skeleton  # noqa: E402
import mm_build as B  # noqa: E402
import mmmath as mm  # noqa: E402
import synth_bvh  # noqa: E402

OFFICER_GLB = os.path.join(HERE, "..", "..", "ocean", "swat", "assets", "characters", "ready.glb")
TMP = os.path.join(HERE, "..", "build", "pytest")
os.makedirs(TMP, exist_ok=True)
_cache = {}


def officer():
    if "officer" not in _cache:
        path = os.path.join(TMP, "officer.json")
        with open(path, "w") as f:
            json.dump(gltf_skeleton.extract(OFFICER_GLB), f)
        _cache["officer"] = B.skeleton_from_json(path)
    return _cache["officer"]


def clip_path(name):
    path = os.path.join(TMP, name + ".bvh")
    if not os.path.exists(path):
        keys, duration, crouch, _ = synth_bvh.standard_clips()[name]
        names, parents, offsets, pos, rot = synth_bvh.generate(keys, duration, crouch)
        bvh.write(path, names, parents, offsets, pos, rot, 1.0 / synth_bvh.FPS, position_joints=(0, 1))
    return path


def angle_between(a, b):
    a = a / np.linalg.norm(a, axis=-1, keepdims=True)
    b = b / np.linalg.norm(b, axis=-1, keepdims=True)
    return np.degrees(np.arccos(np.clip(np.sum(a * b, axis=-1), -1.0, 1.0)))


def quat_close(a, b):
    """Largest angle in degrees between two quaternion arrays."""
    d = np.abs(np.sum(mm.quat_normalize(a) * mm.quat_normalize(b), axis=-1))
    return float(np.degrees(2 * np.arccos(np.clip(d, -1.0, 1.0))).max())


class SkeletonTests(unittest.TestCase):
    def test_officer_skeleton(self):
        doc = gltf_skeleton.extract(OFFICER_GLB)
        joints = doc["joints"]
        self.assertEqual(len(joints), 70)
        for i, j in enumerate(joints):
            self.assertLess(j["parent"], i)
        skel = officer()
        world_pos, world_rot = B.rest_world(skel)
        # The rest pose must agree with the inverse bind matrices.
        for i in range(len(joints)):
            bind = np.linalg.inv(skel["inverse_bind"][i].reshape(4, 4).T)
            self.assertLess(np.abs(bind[:3, 3] - world_pos[0, i]).max(), 1e-4)
            self.assertLess(np.abs(bind[:3, :3] - mm.quat_to_matrix(world_rot[0, i])).max(), 1e-4)

    def test_builtin_maps_exist_in_officer(self):
        names = set(officer()["names"])
        for target in B.builtin_map("soma77_to_officer").values():
            self.assertIn(target, names)
        src = set(synth_bvh.soma_like_skeleton()[0])
        for source in B.builtin_map("soma77_to_mixamo"):
            self.assertIn(source, src)


class BvhTests(unittest.TestCase):
    def test_roundtrip(self):
        keys, duration, crouch, _ = synth_bvh.standard_clips()["walk_turn90"]
        names, parents, offsets, pos, rot = synth_bvh.generate(keys, duration, crouch)
        clip = bvh.read(clip_path("walk_turn90"))
        self.assertEqual(clip["names"], names)
        self.assertLess(quat_close(clip["rotations"], rot), 1e-3)
        self.assertLess(np.abs(clip["positions"] - pos).max(), 1e-4)

    def test_generator_plants_feet(self):
        clip = bvh.read(clip_path("walk_straight"))
        wp, _ = mm.forward_kinematics(clip["parents"], clip["positions"], clip["rotations"])
        foot = wp[:, clip["names"].index("LeftFoot")]
        low = foot[:, 1] < 7.0 + 1e-3
        speed = np.linalg.norm(np.diff(foot, axis=0), axis=1)
        planted = low[1:] & low[:-1]
        self.assertGreater(planted.sum(), 50)
        self.assertLess(speed[planted].max(), 1e-3)


class RetargetTests(unittest.TestCase):
    def test_identity(self):
        clip = bvh.read(clip_path("walk_turn90"))
        target = B.skeleton_from_bvh(clip)
        local_rot, hips_world, _ = B.retarget(clip, target, None)
        self.assertLess(quat_close(local_rot, clip["rotations"]), 1e-4)
        wp, _ = mm.forward_kinematics(clip["parents"], clip["positions"], clip["rotations"])
        self.assertLess(np.abs(hips_world - wp[:, 1]).max(), 1e-6)

    def test_officer_limb_directions(self):
        clip = bvh.read(clip_path("walk_turn90"))
        skel = officer()
        local_rot, hips_world, _ = B.retarget(clip, skel, B.builtin_map("soma77_to_officer"))
        F = local_rot.shape[0]
        local_pos = np.repeat(skel["rest_pos"][None], F, axis=0)
        local_pos[:, skel["names"].index("mixamorig:Hips")] = hips_world
        tp, _ = mm.forward_kinematics(skel["parents"], local_pos, local_rot)
        sp, _ = mm.forward_kinematics(clip["parents"], clip["positions"], clip["rotations"])
        rest_t, _ = B.rest_world(skel)
        rest_s, _ = mm.forward_kinematics(clip["parents"], clip["offsets"][None],
                                          np.tile([0.0, 0, 0, 1], (1, len(clip["names"]), 1)))
        limbs = [("LeftLeg", "LeftShin", "LeftUpLeg", "LeftLeg"), ("LeftShin", "LeftFoot", "LeftLeg", "LeftFoot"),
                 ("RightLeg", "RightShin", "RightUpLeg", "RightLeg"), ("RightShin", "RightFoot", "RightLeg", "RightFoot"),
                 ("LeftArm", "LeftForeArm", "LeftArm", "LeftForeArm"), ("LeftForeArm", "LeftHand", "LeftForeArm", "LeftHand"),
                 ("RightArm", "RightForeArm", "RightArm", "RightForeArm"), ("Spine1", "Chest", "Spine", "Spine2")]
        si = {n: i for i, n in enumerate(clip["names"])}
        ti = {n: i for i, n in enumerate(skel["names"])}
        for sa, sb, ta, tb in limbs:
            ta, tb = "mixamorig:" + ta, "mixamorig:" + tb
            src = sp[:, si[sb]] - sp[:, si[sa]]
            tgt = tp[:, ti[tb]] - tp[:, ti[ta]]
            rest_angle = angle_between(rest_s[0, si[sb]] - rest_s[0, si[sa]], rest_t[0, ti[tb]] - rest_t[0, ti[ta]])
            angles = angle_between(src, tgt)
            self.assertLess(angles.max(), 12.0, f"{sa}->{sb}")
            self.assertLess(np.abs(angles - rest_angle).max(), 0.5, f"{sa}->{sb}")
        # The hips follow the source scaled by the hip height ratio (cm to m here).
        travel_s = sp[-1, si["Hips"]] - sp[0, si["Hips"]]
        travel_t = hips_world[-1] - hips_world[0]
        ratio = np.linalg.norm(travel_t) / np.linalg.norm(travel_s)
        self.assertAlmostEqual(ratio, 0.945 / 94.0, delta=0.0005)


class MirrorTests(unittest.TestCase):
    def test_mirror_twice_is_identity(self):
        skel = officer()
        clip = bvh.read(clip_path("walk_turn90"))
        local_rot, hips_world, animated = B.retarget(clip, skel, B.builtin_map("soma77_to_officer"))
        hips = skel["names"].index("mixamorig:Hips")
        m1, h1 = B.mirror(skel, local_rot, hips_world, hips, animated)
        self.assertGreater(quat_close(m1, local_rot), 5.0)
        rest = np.broadcast_to(skel["rest_rot"], local_rot.shape)
        self.assertLess(quat_close(m1[:, ~animated], rest[:, ~animated]), 1e-4)
        m2, h2 = B.mirror(skel, m1, h1, hips, animated)
        self.assertLess(quat_close(m2, local_rot), 1e-3)
        self.assertLess(np.abs(h2 - hips_world).max(), 1e-9)

    def test_mirror_swaps_feet(self):
        skel = officer()
        cfg = copy.deepcopy(B.DEFAULT_FEATURES)
        db = B.build([(clip_path("strafe_left"), None)], skel, B.builtin_map("soma77_to_officer"), cfg, 30.0)
        a, b = db["clips"][0]["features"], db["clips"][1]["features"]
        flip = np.array([-1.0, 1.0, 1.0])
        # Left foot position of the mirror is the reflected right foot position.
        self.assertLess(np.abs(b[:, 0:3] - a[:, 3:6] * flip).max(), 0.02)
        self.assertLess(np.abs(b[:, 15:21:2] + a[:, 15:21:2]).max(), 0.02)
        self.assertEqual(db["clips"][1]["tags"][-1], "mirrored")
        fa, fb = db["clips"][0]["flags"], db["clips"][1]["flags"]
        self.assertTrue(np.array_equal(fa & 1, (fb & 2) >> 1))


class FeatureTests(unittest.TestCase):
    def test_straight_walk_geometry(self):
        skel = officer()
        cfg = copy.deepcopy(B.DEFAULT_FEATURES)
        db = B.build([(clip_path("walk_straight"), None)], skel, B.builtin_map("soma77_to_officer"), cfg, 30.0,
                     do_mirror=False)
        c = db["clips"][0]
        f = c["features"]
        speed = 1.40 * 0.945 / 0.94
        mid = slice(30, 140)
        self.assertLess(np.abs(c["root_yaw"][mid]).max(), 0.01)
        for k, off in enumerate(cfg["trajectory_frames"]):
            np.testing.assert_allclose(f[mid, 15 + 2 * k], 0.0, atol=0.01)
            np.testing.assert_allclose(f[mid, 16 + 2 * k], speed * off / 30.0, atol=0.01)
            np.testing.assert_allclose(f[mid, 21 + 2 * k], 0.0, atol=0.01)
            np.testing.assert_allclose(f[mid, 22 + 2 * k], 1.0, atol=1e-3)
        self.assertAlmostEqual(f[mid, 14].mean(), speed, delta=0.02)
        self.assertLess(np.abs(f[mid, 12]).max(), 0.05)
        # Feet are on their own sides, and planted feet do not move.
        self.assertGreater(f[mid, 0].min(), 0.05)
        self.assertLess(f[mid, 3].max(), -0.05)
        left_contact = (c["flags"][mid] & 1) > 0
        self.assertGreater(left_contact.sum(), 40)
        world_speed = np.linalg.norm(B.velocity(c["world_pos"][:, skel["names"].index("mixamorig:LeftFoot")], 30.0), axis=1)
        self.assertLess(world_speed[mid][left_contact].max(), 0.35)
        # Within the clip interior the extrapolation flags are off.
        self.assertEqual(int((c["flags"][:150] & 4).max()), 0)
        self.assertTrue(np.all(c["flags"][-30:] & 4))
        self.assertTrue(np.all(c["flags"][-10:] & 8))
        self.assertEqual(int((c["flags"][:-10] & 8).max()), 0)

    def test_turn_root_facing(self):
        skel = officer()
        cfg = copy.deepcopy(B.DEFAULT_FEATURES)
        db = B.build([(clip_path("walk_turn90"), None)], skel, B.builtin_map("soma77_to_officer"), cfg, 30.0,
                     do_mirror=False)
        yaw = db["clips"][0]["root_yaw"]
        self.assertAlmostEqual(float(yaw[20]), 0.0, delta=0.02)
        self.assertAlmostEqual(float(yaw[-20]), math.pi / 2, delta=0.03)

    def test_gaussian_keeps_lines(self):
        x = np.arange(50.0)[:, None] * np.array([[1.0, 0.0, 2.0]])
        np.testing.assert_allclose(B.gaussian_smooth(x, 4.0), x, atol=1e-9)


class FileTests(unittest.TestCase):
    def test_write_layout(self):
        skel = officer()
        cfg = copy.deepcopy(B.DEFAULT_FEATURES)
        db = B.build([(clip_path("walk_straight"), None), (clip_path("idle"), None)], skel,
                     B.builtin_map("soma77_to_officer"), cfg, 30.0)
        path = os.path.join(TMP, "t.mmdb")
        size = B.write_mmdb(db, path)
        with open(path, "rb") as f:
            data = f.read()
        self.assertEqual(len(data), size)
        self.assertEqual(data[:4], b"MMDB")
        self.assertEqual(data[-4:], b"MMDB")
        version, fps = struct.unpack_from("<If", data, 4)
        J, N, C, T, F = struct.unpack_from("<5I", data, 12)
        self.assertEqual((version, fps), (1, 30.0))
        self.assertEqual((J, C, F), (70, 4, 27))
        self.assertEqual(N, sum(c["local_rot"].shape[0] for c in db["clips"]))
        self.assertEqual(T, len(db["tags"]))


if __name__ == "__main__":
    unittest.main(verbosity=1)
