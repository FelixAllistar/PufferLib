#!/usr/bin/env python3
"""Fit collision volumes for a character's torso, head and thighs from its skinned mesh.

Retargeted motion keeps the source's joint directions, so on a target that is
wider, deeper or shorter-armed than the source the hands and elbows pass
through the body. mm_build.py --body-shape uses these volumes to push the arms
back out. Each volume belongs to one joint and is a stack of elliptical slices
in that joint's rest frame (the frame of its inverse bind matrix), so it bends
with the skeleton:

    {"source": "...", "volumes": [{"joint": name, "slices": [
        {"y0": .., "y1": .., "cx": .., "cz": .., "ax": .., "az": ..}, ...]}]}

A slice holds the vertices whose strongest skin weight is on that joint and
whose joint-space y lies in [y0, y1); cx, cz is the slice centre and ax, az
are the given percentile of |x - cx| and |z - cz| (percentiles keep stray
straps and pouches from inflating the shape).

Usage: gltf_body_shape.py input.glb output.json [--mesh NAME ...] [--joints NAME ...]
       [--slices N] [--percentile P]
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from gltf_skeleton import read_glb, read_accessor_mat4  # noqa: E402

DEFAULT_JOINTS = ["mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2",
                  "mixamorig:Neck", "mixamorig:Head", "mixamorig:LeftUpLeg", "mixamorig:RightUpLeg"]

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
DTYPES = {5120: "<i1", 5121: "<u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}


def read_accessor(doc, binary, index):
    """Read any non-sparse accessor as an (count, components) float64 array."""
    a = doc["accessors"][index]
    view = doc["bufferViews"][a["bufferView"]]
    n = COMPONENTS[a["type"]]
    dt = np.dtype(DTYPES[a["componentType"]])
    start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = view.get("byteStride", n * dt.itemsize)
    count = a["count"]
    raw = np.frombuffer(binary, dtype=np.uint8, count=stride * (count - 1) + n * dt.itemsize, offset=start)
    rows = np.lib.stride_tricks.as_strided(raw, shape=(count, n * dt.itemsize), strides=(stride, 1))
    out = np.ascontiguousarray(rows).view(dt).reshape(count, n).astype(np.float64)
    if a.get("normalized"):
        out /= float(np.iinfo(dt).max)
    return out


def skinned_vertices(doc, binary, mesh_names):
    """Yield (positions, joints, weights, skin) for skinned primitives of the chosen meshes."""
    for node in doc["nodes"]:
        if "mesh" not in node or "skin" not in node:
            continue
        mesh = doc["meshes"][node["mesh"]]
        name = mesh.get("name", node.get("name", ""))
        if mesh_names and name not in mesh_names and node.get("name", "") not in mesh_names:
            continue
        for prim in mesh["primitives"]:
            attr = prim["attributes"]
            if "JOINTS_0" not in attr or "WEIGHTS_0" not in attr:
                continue
            yield (read_accessor(doc, binary, attr["POSITION"]),
                   read_accessor(doc, binary, attr["JOINTS_0"]).astype(int),
                   read_accessor(doc, binary, attr["WEIGHTS_0"]), node["skin"], name)


def largest_skinned_mesh(doc):
    best, count = None, -1
    for node in doc["nodes"]:
        if "mesh" in node and "skin" in node:
            mesh = doc["meshes"][node["mesh"]]
            n = sum(doc["accessors"][p["attributes"]["POSITION"]]["count"] for p in mesh["primitives"])
            if n > count:
                best, count = mesh.get("name", node.get("name")), n
    return best


def fit(path, mesh_names=None, joint_names=None, slices=4, percentile=95.0, min_weight=0.5):
    doc, binary = read_glb(path)
    if not mesh_names:
        mesh_names = [largest_skinned_mesh(doc)]
    joint_names = joint_names or DEFAULT_JOINTS
    buckets = {j: [] for j in joint_names}
    used = []
    for pos, jnt, wts, skin_index, name in skinned_vertices(doc, binary, mesh_names):
        used.append(name)
        skin = doc["skins"][skin_index]
        ibm = read_accessor_mat4(doc, binary, skin["inverseBindMatrices"])
        names = [doc["nodes"][n].get("name", f"node{n}") for n in skin["joints"]]
        best = np.argmax(wts, axis=1)
        dom = jnt[np.arange(len(jnt)), best]
        domw = wts[np.arange(len(wts)), best]
        homo = np.concatenate([pos, np.ones((len(pos), 1))], axis=1)
        for k, jn in enumerate(names):
            if jn not in buckets:
                continue
            sel = (dom == k) & (domw >= min_weight)
            if not sel.any():
                continue
            m = ibm[k].reshape(4, 4).T  # glTF stores column major
            buckets[jn].append((homo[sel] @ m.T)[:, :3])
    if not used:
        raise ValueError(f"no skinned mesh named {mesh_names} in {path}")
    volumes = []
    for jn in joint_names:
        if not buckets[jn]:
            continue
        v = np.concatenate(buckets[jn])
        lo, hi = np.percentile(v[:, 1], 1.0), np.percentile(v[:, 1], 99.0)
        edges = np.linspace(lo, hi, slices + 1)
        out = []
        for a, b in zip(edges[:-1], edges[1:]):
            s = v[(v[:, 1] >= a) & (v[:, 1] <= b)]
            if len(s) < 8:
                continue
            cx = 0.5 * (np.percentile(s[:, 0], 100 - percentile) + np.percentile(s[:, 0], percentile))
            cz = 0.5 * (np.percentile(s[:, 2], 100 - percentile) + np.percentile(s[:, 2], percentile))
            out.append({"y0": float(a), "y1": float(b), "cx": float(cx), "cz": float(cz),
                        "ax": float(np.percentile(np.abs(s[:, 0] - cx), percentile)),
                        "az": float(np.percentile(np.abs(s[:, 2] - cz), percentile))})
        if out:
            volumes.append({"joint": jn, "vertices": int(len(v)), "slices": out})
    return {"source": os.path.basename(path), "meshes": used, "percentile": percentile, "volumes": volumes}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("glb")
    ap.add_argument("out")
    ap.add_argument("--mesh", nargs="+", help="mesh names (default: the skinned mesh with most vertices)")
    ap.add_argument("--joints", nargs="+", help="joints that get a volume")
    ap.add_argument("--slices", type=int, default=4)
    ap.add_argument("--percentile", type=float, default=95.0)
    args = ap.parse_args(argv)
    shape = fit(args.glb, args.mesh, args.joints, args.slices, args.percentile)
    with open(args.out, "w") as f:
        json.dump(shape, f, indent=1)
    for v in shape["volumes"]:
        widths = " ".join(f"{2 * s['ax']:.2f}x{2 * s['az']:.2f}" for s in v["slices"])
        print(f"{v['joint']}: {v['vertices']} vertices, slices (width x depth m) {widths}")
    print(f"wrote {args.out} from {shape['meshes']}")


if __name__ == "__main__":
    main()
