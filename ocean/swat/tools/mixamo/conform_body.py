#!/usr/bin/env python3
"""Conform a Mixamo body onto an existing animated SWAT rig.

The target GLB (an officer bank or the civilian rig) keeps its skeleton, rest
pose, clips and props untouched. Only its body geometry is replaced:

  1. The source body is scaled so its hip height matches the target's.
  2. Each source bone gets the rotation that turns its rest direction onto
     the matching target bone's rest direction (facing aligned first).
  3. Every vertex is carried by linear blend skinning from the source bind
     to the target bind:  v' = sum_j w_j (P_target_j + R_j (v - P_source_j)).
  4. Influences are remapped by bone name to the target skin's joints.

The result is a body in the target's own bind space, so all measured runtime
fits (aim layer, hand IK, banks, props) apply unchanged.

  conform_body.py --rig officer|civilian SOURCE_BODY.glb TARGET.glb OUT.glb [--textures DIR]
"""
import argparse, json, math, os, struct

# ---------------------------------------------------------------- GLB I/O
def read_glb(path):
    d = open(path, "rb").read()
    assert d[:4] == b"glTF", path
    jl = struct.unpack("<I", d[12:16])[0]
    j = json.loads(d[20:20 + jl])
    off = 20 + jl
    bl = struct.unpack("<I", d[off:off + 4])[0]
    return j, bytearray(d[off + 8:off + 8 + bl])

def write_glb(path, j, binary):
    while len(binary) % 4: binary.append(0)
    j["buffers"] = [{"byteLength": len(binary)}]
    js = json.dumps(j, separators=(",", ":")).encode()
    while len(js) % 4: js += b" "
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(binary), 0x004E4942)); f.write(binary)

COMP = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}

def accessor(j, binary, index):
    a = j["accessors"][index]; v = j["bufferViews"][a["bufferView"]]
    fmt, size = COMP[a["componentType"]]; n = NCOMP[a["type"]]
    stride = v.get("byteStride", size * n)
    base = v.get("byteOffset", 0) + a.get("byteOffset", 0)
    out = []
    for i in range(a["count"]):
        vals = struct.unpack_from("<" + fmt * n, binary, base + i * stride)
        if a.get("normalized"):
            scale = {5121: 255.0, 5123: 65535.0, 5120: 127.0, 5122: 32767.0}[a["componentType"]]
            vals = tuple(x / scale for x in vals)
        out.append(vals)
    return out

def append(j, binary, values, ctype, atype, target=None, minmax=False):
    fmt, size = COMP[ctype]; n = NCOMP[atype]
    while len(binary) % 4: binary.append(0)
    start = len(binary)
    flat = [x for v in values for x in (v if isinstance(v, (tuple, list)) else (v,))]
    binary += struct.pack("<" + fmt * len(flat), *flat)
    view = {"buffer": 0, "byteOffset": start, "byteLength": len(binary) - start}
    if target: view["target"] = target
    j["bufferViews"].append(view)
    acc = {"bufferView": len(j["bufferViews"]) - 1, "componentType": ctype, "count": len(values), "type": atype}
    if minmax:
        acc["min"] = [min(v[k] for v in values) for k in range(n)]
        acc["max"] = [max(v[k] for v in values) for k in range(n)]
    j["accessors"].append(acc)
    return len(j["accessors"]) - 1

# ---------------------------------------------------------------- math
def mat4(cm):  # column-major 16 -> row-major nested
    return [[cm[c * 4 + r] for c in range(4)] for r in range(4)]

def inv_rigid(m):  # inverse of an affine matrix with uniform scale (bind matrices)
    r = [[m[i][k] for k in range(3)] for i in range(3)]
    # general 3x3 inverse (handles scale)
    a, b, c = r[0]; d, e, f = r[1]; g, h, i = r[2]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    inv = [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det],
           [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
           [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]
    t = [m[0][3], m[1][3], m[2][3]]
    it = [-sum(inv[r_][k] * t[k] for k in range(3)) for r_ in range(3)]
    return [inv[0] + [it[0]], inv[1] + [it[1]], inv[2] + [it[2]], [0, 0, 0, 1]]

def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def add(a, b): return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
def scale(a, s): return [a[0] * s, a[1] * s, a[2] * s]
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def norm(a):
    l = math.sqrt(dot(a, a)); return [a[0] / l, a[1] / l, a[2] / l] if l > 1e-12 else [0, 0, 0]
def mv(m, v): return [m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2], m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2], m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]]
def mm(a, b): return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
IDENT = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]

def arc(u, v):
    """Rotation matrix taking unit u onto unit v (minimal arc)."""
    u, v = norm(u), norm(v); c = dot(u, v); ax = cross(u, v); s = math.sqrt(dot(ax, ax))
    if s < 1e-9:
        if c > 0: return IDENT
        p = norm(cross(u, [1, 0, 0] if abs(u[0]) < .9 else [0, 1, 0])); return axis_angle(p, math.pi)
    return axis_angle(scale(ax, 1 / s), math.atan2(s, c))

def axis_angle(k, a):
    x, y, z = k; c, s = math.cos(a), math.sin(a); t = 1 - c
    return [[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
            [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
            [t * x * z - s * y, t * y * z + s * x, t * z * z + c]]

# ---------------------------------------------------------------- rigs
def base(name):  # strip Mixamo prefixes: mixamorig:, mixamorig5:, mixamorig_
    for sep in (":", "_"):
        if name.lower().startswith("mixamorig") and sep in name:
            return name.split(sep, 1)[1]
    return name

SIDES = ("Left", "Right")
FINGERS = ("Thumb", "Index", "Middle", "Ring", "Pinky")
# Source (Mixamo base name) -> direction child.
CHILD = {"Hips": "Spine", "Spine": "Spine1", "Spine1": "Spine2", "Spine2": "Neck", "Neck": "Head", "Head": "HeadTop_End"}
for s in SIDES:
    CHILD.update({s + "Shoulder": s + "Arm", s + "Arm": s + "ForeArm", s + "ForeArm": s + "Hand",
                  s + "Hand": s + "HandMiddle1", s + "UpLeg": s + "Leg", s + "Leg": s + "Foot",
                  s + "Foot": s + "ToeBase", s + "ToeBase": s + "Toe_End"})
    for f in FINGERS:
        CHILD[f"{s}Hand{f}1"] = f"{s}Hand{f}2"; CHILD[f"{s}Hand{f}2"] = f"{s}Hand{f}3"; CHILD[f"{s}Hand{f}3"] = f"{s}Hand{f}4"
PARENT_FALLBACK = {}  # filled from source hierarchy

def civilian_map():
    m = {"Hips": "Hips", "Spine": "Spine", "Spine1": "Chest", "Spine2": "Chest", "Neck": "Neck", "Head": "Head", "HeadTop_End": "Head"}
    for s, t in (("Left", "L"), ("Right", "R")):
        m.update({s + "Shoulder": "Clavicle_" + t, s + "Arm": "UpperArm_" + t, s + "ForeArm": "Forearm_" + t,
                  s + "Hand": "Hand_" + t, s + "UpLeg": "Thigh_" + t, s + "Leg": "Shin_" + t, s + "Foot": "Foot_" + t,
                  s + "ToeBase": "Toe_" + t, s + "Toe_End": "Toe_" + t})
        for f in FINGERS:
            m[f"{s}Hand{f}1"] = f"{f}1_{t}"; m[f"{s}Hand{f}2"] = f"{f}2_{t}"
            m[f"{s}Hand{f}3"] = f"{f}2_{t}"; m[f"{s}Hand{f}4"] = f"{f}2_{t}"
    return m

# ---------------------------------------------------------------- bone lengths
# Civilian rig joint -> source joint whose position marks it.
def civilian_inverse():
    m = {"Hips": "Hips", "Spine": "Spine", "Chest": "Spine1", "Neck": "Neck", "Head": "Head"}
    for s, t in (("Left", "L"), ("Right", "R")):
        m.update({"Clavicle_" + t: s + "Shoulder", "UpperArm_" + t: s + "Arm", "Forearm_" + t: s + "ForeArm",
                  "Hand_" + t: s + "Hand", "Thigh_" + t: s + "UpLeg", "Shin_" + t: s + "Leg", "Foot_" + t: s + "Foot",
                  "Toe_" + t: s + "ToeBase"})
        for f in FINGERS: m[f"{f}1_{t}"] = f"{s}Hand{f}1"; m[f"{f}2_{t}"] = f"{s}Hand{f}2"
    return m

def quat_mat(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]

def node_local(n):
    if "matrix" in n: return mat4(n["matrix"])
    r = quat_mat(n.get("rotation", [0, 0, 0, 1])); s = n.get("scale", [1, 1, 1]); t = n.get("translation", [0, 0, 0])
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]

def m4(a, b): return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

def world_matrices(j):
    parent = {c: i for i, n in enumerate(j["nodes"]) for c in n.get("children", [])}
    cache = {}
    def world(i):
        if i in cache: return cache[i]
        w = node_local(j["nodes"][i]); w = m4(world(parent[i]), w) if i in parent else w
        cache[i] = w; return w
    return [world(i) for i in range(len(j["nodes"]))]

def retarget_lengths(j, binary, s_pos, skin):
    names = [n.get("name", "") for n in j["nodes"]]
    index = {n: i for i, n in enumerate(names)}
    parent = {c: i for i, n in enumerate(j["nodes"]) for c in n.get("children", [])}
    inv = civilian_inverse()
    factor = {}
    for t, src in inv.items():
        if t in ("Hips",) or t not in index or src not in s_pos: continue
        i = index[t]; p = parent.get(i)
        # nearest mapped ancestor gives the source segment start
        while p is not None and names[p] not in inv: p = parent.get(p)
        if p is None or inv[names[p]] not in s_pos: continue
        n = j["nodes"][i]
        if "matrix" in n: continue
        old = n.get("translation", [0, 0, 0]); ol = math.sqrt(dot(old, old))
        if ol < 1e-5: continue
        # only rescale when this joint is the direct child carrying the segment
        if parent.get(i) != p: continue
        sl = math.sqrt(dot(sub(s_pos[src], s_pos[inv[names[p]]]), sub(s_pos[src], s_pos[inv[names[p]]])))
        f = sl / ol
        if not (0.4 < f < 2.5): continue
        factor[i] = f
        n["translation"] = [x * f for x in old]
    # Translation keyframes: scale the same joints (new accessors).
    for a in j.get("animations", []):
        for ch in a["channels"]:
            node = ch["target"]["node"]
            if ch["target"]["path"] != "translation" or node not in factor: continue
            smp = a["samplers"][ch["sampler"]]
            vals = accessor(j, binary, smp["output"])
            smp["output"] = append(j, binary, [tuple(x * factor[node] for x in v) for v in vals], 5126, "VEC3")
    # Rebuild inverse binds from the new rest pose.
    worlds = world_matrices(j)
    joints = j["skins"][skin]["joints"]
    ibm = []
    for jn in joints:
        w = inv_rigid(worlds[jn])
        ibm.append(tuple(w[r][c] for c in range(4) for r in range(4)))
    j["skins"][skin]["inverseBindMatrices"] = append(j, binary, ibm, 5126, "MAT4")
    print(f"retargeted {len(factor)} bone lengths")

# ---------------------------------------------------------------- main
def skin_binds(j, binary, skin):
    sk = j["skins"][skin]
    ibm = accessor(j, binary, sk["inverseBindMatrices"])
    names = [j["nodes"][n].get("name", "") for n in sk["joints"]]
    binds = [inv_rigid(mat4(m)) for m in ibm]
    return names, binds

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rig", choices=("officer", "civilian"), required=True)
    ap.add_argument("source"); ap.add_argument("target"); ap.add_argument("out")
    ap.add_argument("--textures", help="body directory: writes textures/ and body_textures.txt")
    ap.add_argument("--name", default=None, help="body mesh node name")
    args = ap.parse_args()

    sj, sb = read_glb(args.source)
    tj, tb = read_glb(args.target)
    # Source skin + body primitives (all skinned mesh nodes).
    s_nodes = [n for n in sj["nodes"] if "mesh" in n and "skin" in n]
    assert s_nodes, "source has no skinned mesh"
    s_names, s_binds = skin_binds(sj, sb, s_nodes[0]["skin"])
    s_base = [base(n) for n in s_names]
    s_pos = {s_base[i]: [s_binds[i][0][3], s_binds[i][1][3], s_binds[i][2][3]] for i in range(len(s_names))}
    # Target skin.
    t_skin = 0
    t_names, t_binds = skin_binds(tj, tb, t_skin)
    t_index = {n: i for i, n in enumerate(t_names)}
    t_pos = {n: [t_binds[i][0][3], t_binds[i][1][3], t_binds[i][2][3]] for i, n in enumerate(t_names)}
    if args.rig == "officer":
        mapping = {b: "mixamorig:" + b for b in s_base}
        tbase = lambda b: "mixamorig:" + b
    else:
        mapping = civilian_map(); tbase = lambda b: mapping.get(b)
    # Source hierarchy for unmapped-joint fallback.
    parent = {}
    for i, n in enumerate(sj["nodes"]):
        for c in n.get("children", []): parent[c] = i
    s_joint_nodes = sj["skins"][s_nodes[0]["skin"]]["joints"]
    node_to_joint = {n: k for k, n in enumerate(s_joint_nodes)}

    def mapped(b):
        t = mapping.get(b)
        return t if t in t_index else None

    def target_of(k):  # source joint index -> target joint name (walk up if unmapped)
        node = s_joint_nodes[k]
        while True:
            t = mapped(base(sj["nodes"][node].get("name", "")))
            if t: return t
            if node not in parent: return mapped("Hips")
            node = parent[node]

    # 1. Height: hips height over the lowest vertex (soles).
    def lowest(j, b, nodes):
        lo = 1e9
        for n in nodes:
            for p in j["meshes"][n["mesh"]]["primitives"]:
                for v in accessor(j, b, p["attributes"]["POSITION"]): lo = min(lo, v[1])
        return lo
    s_floor = lowest(sj, sb, s_nodes)
    t_body = [n for n in tj["nodes"] if "mesh" in n and "skin" in n and n.get("skin") == t_skin]
    t_floor = lowest(tj, tb, [n for n in t_body if args.rig == "civilian" or "full body" in n.get("name", "")])
    k = (t_pos[tbase("Hips")][1] - t_floor) / (s_pos["Hips"][1] - s_floor)
    def S(p): return [p[0] * k, (p[1] - s_floor) * k + t_floor, p[2] * k]
    s_pos = {b: S(p) for b, p in s_pos.items()}
    # 2. Facing: align the hip lateral axis about +Y.
    def lateral(pos, l, r):
        d = sub(pos[r], pos[l]); d[1] = 0; return norm(d)
    ls, lt = lateral(s_pos, "LeftUpLeg", "RightUpLeg"), lateral(t_pos, tbase("LeftUpLeg"), tbase("RightUpLeg"))
    yaw = math.atan2(cross(ls, lt)[1], dot(ls, lt)); G = axis_angle([0, 1, 0], yaw)
    hips = s_pos["Hips"]
    s_pos = {b: add(mv(G, sub(p, hips)), hips) for b, p in s_pos.items()}
    # 2b. Civilian rig: give the target skeleton this body's bone lengths.
    # Each mapped joint's rest offset and every translation keyframe are
    # scaled along their own direction (rotations untouched), then the bind
    # matrices are rebuilt, so pivots and proportions both match the body.
    if args.rig == "civilian":
        retarget_lengths(tj, tb, s_pos, t_skin)
        t_names, t_binds = skin_binds(tj, tb, t_skin)
        t_pos = {n: [t_binds[i][0][3], t_binds[i][1][3], t_binds[i][2][3]] for i, n in enumerate(t_names)}
    # 3. Per-bone rotations (minimal arc of rest directions), inherited down.
    R = {}
    order = sorted(s_base, key=lambda b: 0)  # resolved lazily with parent fallback
    def rot(b, depth=0):
        if b in R: return R[b]
        child = CHILD.get(b)
        t, tc = mapped(b), mapped(child) if child else None
        r = None
        # Civilian rig: the core keeps the body's own upright orientation.
        # Aligning Mixamo's curved spine and level clavicles to the rig's
        # straight spine and raised clavicles hunched the torso and shrugged
        # the shoulders; only the limbs need turning into its A-pose.
        if args.rig == "civilian" and b in ("Hips", "Spine", "Spine1", "Spine2", "Neck", "Head", "HeadTop_End", "LeftShoulder", "RightShoulder"):
            R[b] = IDENT; return IDENT
        if t and child in s_pos:
            src_dir = sub(s_pos[child], s_pos[b])
            if tc and tc != t: tgt_dir = sub(t_pos[tc], t_pos[t])
            else:
                # e.g. Spine1/Spine2 -> Chest: use the target bone's own child.
                kids = [c for c in t_names if c != t and t_pos[c] and parent_name_t.get(c) == t]
                tgt_dir = sub(t_pos[kids[0]], t_pos[t]) if kids else None
            if tgt_dir and math.sqrt(dot(src_dir, src_dir)) > 1e-4 and math.sqrt(dot(tgt_dir, tgt_dir)) > 1e-4:
                r = arc(src_dir, tgt_dir)
        if r is None:
            pn = src_parent.get(b)
            r = rot(pn, depth + 1) if pn and depth < 64 else IDENT
        R[b] = r; return r
    # target parent names
    parent_name_t = {}
    for i, n in enumerate(tj["nodes"]):
        for c in n.get("children", []): parent_name_t[tj["nodes"][c].get("name", "")] = n.get("name", "")
    src_parent = {}
    for kk, node in enumerate(s_joint_nodes):
        if node in parent and parent[node] in node_to_joint:
            src_parent[s_base[kk]] = s_base[node_to_joint[parent[node]]]
    # Joint-level carry: P_target(mapped), R(source bone) composed with facing.
    # Source positions are already facing-aligned, so R alone carries them.
    # The civilian rig's stylised neck is much longer than a real one: neck
    # and head keep the source's own proportions above the chest joint and
    # follow the chest's rotation (their pivots stay on the target bones).
    keep = ("Neck", "Head", "HeadTop_End") if args.rig == "civilian" else ()
    chest_t = mapped("Spine2")
    carry = []
    for kk in range(len(s_names)):
        b = s_base[kk]; t = target_of(kk)
        if b in keep and chest_t and "Spine2" in s_pos and b in s_pos:
            Rc = IDENT  # upright source orientation; the chest arc would tip the head
            anchor = add(t_pos[chest_t], mv(Rc, sub(s_pos[b], s_pos["Spine2"])))
            carry.append((t_index[t], anchor, s_pos[b], Rc, t)); continue
        carry.append((t_index[t], t_pos[t], s_pos.get(b), rot(b), t))
    # 4. Geometry.
    prims_out = []; materials_used = []
    for n in s_nodes:
        for p in sj["meshes"][n["mesh"]]["primitives"]:
            mat = p.get("material")
            at = p["attributes"]
            P = accessor(sj, sb, at["POSITION"]); N = accessor(sj, sb, at["NORMAL"])
            UV = accessor(sj, sb, at["TEXCOORD_0"]) if "TEXCOORD_0" in at else None
            J = accessor(sj, sb, at["JOINTS_0"]); W = accessor(sj, sb, at["WEIGHTS_0"])
            J1 = accessor(sj, sb, at["JOINTS_1"]) if "JOINTS_1" in at else None
            W1 = accessor(sj, sb, at["WEIGHTS_1"]) if "WEIGHTS_1" in at else None
            I = [i[0] for i in accessor(sj, sb, p["indices"])] if "indices" in p else list(range(len(P)))
            pos, nor, joints, weights = [], [], [], []
            for vi in range(len(P)):
                v = mv(G, sub(S(P[vi]), hips)); v = add(v, hips)
                nv = mv(G, list(N[vi]))
                ids = list(J[vi]) + (list(J1[vi]) if J1 else []); ws = list(W[vi]) + (list(W1[vi]) if W1 else [])
                acc_p = [0, 0, 0]; acc_n = [0, 0, 0]; tw = {}
                total = sum(w for w in ws if w > 0) or 1
                for jid, w in zip(ids, ws):
                    if w <= 0: continue
                    w /= total
                    ti, tp, sp, Rb, _ = carry[jid]
                    sp = sp or tp
                    local = sub(v, sp)
                    acc_p = add(acc_p, scale(add(tp, mm_vec(Rb, local)), w))
                    acc_n = add(acc_n, scale(mm_vec(Rb, nv), w))
                    tw[ti] = tw.get(ti, 0) + w
                top = sorted(tw.items(), key=lambda x: -x[1])[:4]
                s_ = sum(w for _, w in top)
                top = [(j_, w / s_) for j_, w in top]
                while len(top) < 4: top.append((0, 0.0))
                ws4 = [float(struct.unpack("<f", struct.pack("<f", w))[0]) for _, w in top]
                ws4[0] = 1.0 - sum(ws4[1:])  # exact float32 normalization for the loader
                pos.append(tuple(acc_p)); nor.append(tuple(norm(acc_n)))
                joints.append(tuple(j_ for j_, _ in top)); weights.append(tuple(ws4))
            prims_out.append((pos, nor, UV, joints, weights, I, p.get("material")))
            materials_used.append(p.get("material"))
    # 5. Write into the target.
    attrs_list = []
    for pos, nor, UV, joints, weights, I, mat in prims_out:
        a = {"POSITION": append(tj, tb, pos, 5126, "VEC3", 34962, True), "NORMAL": append(tj, tb, nor, 5126, "VEC3", 34962),
             "JOINTS_0": append(tj, tb, joints, 5123, "VEC4", 34962), "WEIGHTS_0": append(tj, tb, weights, 5126, "VEC4", 34962)}
        if UV: a["TEXCOORD_0"] = append(tj, tb, UV, 5126, "VEC2", 34962)
        idx = append(tj, tb, [(i,) for i in I], 5125, "SCALAR", 34963)
        cut = mat is not None and sj["materials"][mat].get("alphaMode", "OPAQUE") != "OPAQUE"
        tj.setdefault("materials", []).append(dict({"name": f"body_{len(attrs_list)}"}, **({"doubleSided": True} if cut else {})))
        attrs_list.append({"attributes": a, "indices": idx, "material": len(tj["materials"]) - 1})
    tj["meshes"].append({"name": "conformed body", "primitives": attrs_list})
    body_mesh = len(tj["meshes"]) - 1
    if args.rig == "officer":
        for n in tj["nodes"]:
            if "full body" in n.get("name", ""): n["mesh"] = body_mesh; break
        else: raise SystemExit("target has no full body mesh")
    else:
        placed = False
        for n in tj["nodes"]:
            if "mesh" in n and n.get("skin") == t_skin:
                if not placed: n["mesh"] = body_mesh; n["name"] = args.name or "Civilian full body"; placed = True
                else: del n["mesh"]; n.pop("skin", None)
    write_glb(args.out, tj, tb)
    # 6. Textures (base color + normal per primitive), straight from the source GLB.
    if args.textures:
        os.makedirs(os.path.join(args.textures, "textures"), exist_ok=True)
        lines = []
        for k_, mat in enumerate(materials_used):
            entry = [str(k_), "-", "-", "1" if mat is not None and sj["materials"][mat].get("alphaMode", "OPAQUE") != "OPAQUE" else "0"]
            if mat is not None:
                m = sj["materials"][mat]
                for slot, key in ((1, ("pbrMetallicRoughness", "baseColorTexture")), (2, (None, "normalTexture"))):
                    tex = (m.get(key[0], {}) if key[0] else m).get(key[1])
                    if tex is None: continue
                    img = sj["images"][sj["textures"][tex["index"]]["source"]]
                    view = sj["bufferViews"][img["bufferView"]]
                    data = sb[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]]
                    ext = ".png" if img.get("mimeType", "image/png") == "image/png" else ".jpg"
                    fname = f"image{sj['textures'][tex['index']]['source']}{ext}"  # shared images written once
                    out_path = os.path.join(args.textures, "textures", fname)
                    if not os.path.exists(out_path):
                        open(out_path, "wb").write(data)
                        try:  # 1024 px is plenty at game distance and keeps GPU memory low
                            from PIL import Image
                            im = Image.open(out_path)
                            if max(im.size) > 1024: im.thumbnail((1024, 1024), Image.LANCZOS); im.save(out_path)
                        except ImportError: pass
                    entry[slot] = fname
            lines.append(" ".join(entry))
        open(os.path.join(args.textures, "body_textures.txt"), "w").write("\n".join(lines) + "\n")
    print(f"conformed {args.source} -> {args.out}: scale {k:.3f}, yaw {math.degrees(yaw):.1f}, "
          f"{sum(len(p[0]) for p in prims_out)} vertices, {len(prims_out)} primitives")

def mm_vec(m, v): return mv(m, v)

if __name__ == "__main__":
    main()
