#!/usr/bin/env python3
"""Extract a skeleton description from the first skin of a binary glTF (.glb) file.

The output JSON has one entry per skin joint, ordered so that every parent comes
before its children:

    {"source": "...", "units_per_meter": 1.0,
     "joints": [{"name": ..., "parent": index or -1,
                 "translation": [x, y, z], "rotation": [x, y, z, w],
                 "scale": [x, y, z], "inverse_bind": [16 floats, column major],
                 "gltf_node": node index}, ...]}

Rest local transforms are the node TRS values. A joint whose glTF parent is not
a skin joint gets parent -1, and the world transform of its glTF parent chain is
baked into its rest translation and rotation, so every joint is expressed in the
space of the scene root. The inverse bind matrices are copied unchanged.

Usage: gltf_skeleton.py input.glb output.json
"""
import json
import struct
import sys

import numpy as np

sys.path.insert(0, __import__("os").path.dirname(__file__))
import mmmath as mm  # noqa: E402


def read_glb(path):
    """Return (json_dict, binary_chunk) of a .glb file."""
    with open(path, "rb") as f:
        data = f.read()
    magic, _version, _length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:
        raise ValueError(f"{path} is not a binary glTF file")
    offset = 12
    doc, binary = None, b""
    while offset < len(data):
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        chunk = data[offset + 8: offset + 8 + chunk_length]
        if chunk_type == 0x4E4F534A:
            doc = json.loads(chunk.decode("utf-8"))
        elif chunk_type == 0x004E4942:
            binary = chunk
        offset += 8 + chunk_length
    return doc, binary


def read_accessor_mat4(doc, binary, index):
    """Read a MAT4 float accessor as an (n, 16) array in glTF column-major order."""
    accessor = doc["accessors"][index]
    view = doc["bufferViews"][accessor["bufferView"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    count = accessor["count"]
    stride = view.get("byteStride", 64)
    out = np.zeros((count, 16), dtype=np.float64)
    for i in range(count):
        out[i] = np.frombuffer(binary, dtype="<f4", count=16, offset=start + i * stride)
    return out


def node_trs(node):
    """Return the translation, rotation (xyzw) and scale of a glTF node."""
    if "matrix" in node:
        m = np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
        t = m[:3, 3].copy()
        s = np.linalg.norm(m[:3, :3], axis=0)
        q = mm.quat_from_matrix(m[:3, :3] / s)
        return t, q, s
    t = np.array(node.get("translation", [0, 0, 0]), dtype=np.float64)
    q = np.array(node.get("rotation", [0, 0, 0, 1]), dtype=np.float64)
    s = np.array(node.get("scale", [1, 1, 1]), dtype=np.float64)
    return t, q, s


def extract(path):
    doc, binary = read_glb(path)
    if not doc.get("skins"):
        raise ValueError(f"{path} has no skin")
    skin = doc["skins"][0]
    nodes = doc["nodes"]
    parent_of = {}
    for i, node in enumerate(nodes):
        for child in node.get("children", []):
            parent_of[child] = i
    joint_nodes = list(skin["joints"])
    joint_set = set(joint_nodes)
    if "inverseBindMatrices" in skin:
        ibm = read_accessor_mat4(doc, binary, skin["inverseBindMatrices"])
    else:
        ibm = np.tile(np.eye(4).T.reshape(16), (len(joint_nodes), 1))
    ibm_of = {n: ibm[i] for i, n in enumerate(joint_nodes)}

    def depth(n):
        d = 0
        while n in parent_of:
            n = parent_of[n]
            d += 1
        return d

    # Stable topological order: by depth first would break subtrees apart, so walk
    # the skin joints depth-first starting from joints whose parent is not a joint.
    roots = [n for n in joint_nodes if parent_of.get(n) not in joint_set]
    order = []

    def visit(n):
        order.append(n)
        for c in nodes[n].get("children", []):
            if c in joint_set:
                visit(c)

    for r in sorted(roots, key=lambda n: (depth(n), joint_nodes.index(n))):
        visit(r)
    index_of = {n: i for i, n in enumerate(order)}

    joints = []
    for n in order:
        t, q, s = node_trs(nodes[n])
        p = parent_of.get(n)
        if p in joint_set:
            parent = index_of[p]
        else:
            parent = -1
            # Bake the world transform of the non-joint ancestors into the root.
            chain_t, chain_q = np.zeros(3), np.array([0.0, 0, 0, 1])
            a = p
            while a is not None:
                at, aq, a_s = node_trs(nodes[a])
                if not np.allclose(a_s, 1.0, atol=1e-6):
                    raise ValueError("scaled armature ancestors are not supported")
                chain_t = mm.quat_rotate(aq, chain_t) + at
                chain_q = mm.quat_mul(aq, chain_q)
                a = parent_of.get(a)
            t = mm.quat_rotate(chain_q, t) + chain_t
            q = mm.quat_mul(chain_q, q)
        joints.append({
            "name": nodes[n].get("name", f"node{n}"),
            "parent": parent,
            "translation": [float(v) for v in t],
            "rotation": [float(v) for v in mm.quat_normalize(q)],
            "scale": [float(v) for v in s],
            "inverse_bind": [float(v) for v in ibm_of[n]],
            "gltf_node": n,
        })
    return {"source": path, "units_per_meter": 1.0, "joints": joints}


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    skel = extract(sys.argv[1])
    with open(sys.argv[2], "w") as f:
        json.dump(skel, f, indent=1)
    print(f"wrote {len(skel['joints'])} joints to {sys.argv[2]}")


if __name__ == "__main__":
    main()
