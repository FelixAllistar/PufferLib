#!/usr/bin/env python3
"""Build the synthetic test database and the reference data the C tests use.

Usage: make_fixtures.py BUILD_DIR

Writes BUILD_DIR/officer.json (from ready.glb), BUILD_DIR/synth/*.bvh,
BUILD_DIR/test.mmdb and BUILD_DIR/fk_reference.txt. The reference file holds,
for every 7th frame, the builder's world joint positions (computed before the
pose was made root relative), so the runtime's forward kinematics placed at the
stored root must reproduce them. Its first line is
"joints frames features walk_frame walk_speed": a frame from the middle of the
straight walk and that clip's root speed, for the trajectory feature test.
"""
import copy
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import bvh  # noqa: E402
import gltf_skeleton  # noqa: E402
import mm_build as B  # noqa: E402
import synth_bvh  # noqa: E402

GLB = os.path.join(HERE, "..", "..", "ocean", "swat", "assets", "characters", "ready.glb")


def main():
    out = sys.argv[1]
    synth = os.path.join(out, "synth")
    os.makedirs(synth, exist_ok=True)
    manifest = {}
    for name, (keys, duration, crouch, tags) in synth_bvh.standard_clips().items():
        path = os.path.join(synth, name + ".bvh")
        if not os.path.exists(path):
            names, parents, offsets, pos, rot = synth_bvh.generate(keys, duration, crouch)
            bvh.write(path, names, parents, offsets, pos, rot, 1.0 / synth_bvh.FPS, position_joints=(0, 1))
        manifest[name] = tags
    with open(os.path.join(synth, "manifest.json"), "w") as f:
        json.dump({"clips": manifest}, f, indent=1)
    skel_path = os.path.join(out, "officer.json")
    with open(skel_path, "w") as f:
        json.dump(gltf_skeleton.extract(GLB), f)
    target = B.skeleton_from_json(skel_path)
    cfg = copy.deepcopy(B.DEFAULT_FEATURES)
    paths = B.collect_clips([synth])
    db = B.build(paths, target, B.builtin_map("soma77_to_officer"), cfg, 30.0, manifest)
    B.write_mmdb(db, os.path.join(out, "test.mmdb"))
    N = sum(c["local_rot"].shape[0] for c in db["clips"])
    start = 0
    walk_frame, walk_speed = -1, 0.0
    lines = []
    for c in db["clips"]:
        n = c["local_rot"].shape[0]
        if c["name"] == "walk_straight":
            walk_frame = start + n // 2
            walk_speed = float(np.linalg.norm(c["root_pos"][n // 2 + 1] - c["root_pos"][n // 2 - 1]) * 15.0)
        for f in range(0, n, 7):
            row = [str(start + f)] + [f"{v:.7f}" for v in c["world_pos"][f].reshape(-1)]
            lines.append(" ".join(row))
        start += n
    with open(os.path.join(out, "fk_reference.txt"), "w") as f:
        f.write(f"{len(target['names'])} {N} {db['means'].shape[0]} {walk_frame} {walk_speed:.7f}\n")
        f.write("\n".join(lines) + "\n")
    print(f"test database: {len(db['clips'])} clips, {N} frames")


if __name__ == "__main__":
    main()
