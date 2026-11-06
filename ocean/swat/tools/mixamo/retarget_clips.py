#!/usr/bin/env python3
"""Retarget the BC_CIV01 civilian clips onto a Mixamo body's own skeleton.

The body keeps its native skeleton, skin and proportions (no mesh conform).
For every keyframe each Mixamo bone takes its BC counterpart's world-space
rotation change from rest, applied on its own rest orientation:

    W_target(t) = (W_bc(t) * W_bc_rest^-1) * W_target_rest     (rotation)

then converted to a local rotation under its animated parent. The hips also
take the BC hips' world travel, scaled by hip height. A-pose vs T-pose rests
cancel out by construction. The 12 clips keep their exact names and timing.

  retarget_clips.py BODY.glb BC_CIV01.glb OUT.glb
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from conform_body import (arc, CHILD, read_glb, write_glb, accessor, append, base, node_local, m4, world_matrices,
                          quat_mat, inv_rigid, FINGERS)

def bc_source(b):
    m = {"Hips": "Hips", "Spine": "Spine", "Spine1": "Chest", "Spine2": "Chest", "Neck": "Neck", "Head": "Head"}
    for s, t in (("Left", "L"), ("Right", "R")):
        m.update({s + "Shoulder": "Clavicle_" + t, s + "Arm": "UpperArm_" + t, s + "ForeArm": "Forearm_" + t,
                  s + "Hand": "Hand_" + t, s + "UpLeg": "Thigh_" + t, s + "Leg": "Shin_" + t, s + "Foot": "Foot_" + t,
                  s + "ToeBase": "Toe_" + t})
        for f in FINGERS:
            m[f"{s}Hand{f}1"] = f"{f}1_{t}"; m[f"{s}Hand{f}2"] = f"{f}2_{t}"; m[f"{s}Hand{f}3"] = f"{f}2_{t}"
    return m.get(b)

def r3(m): return [[m[i][k] for k in range(3)] for i in range(3)]
def t3(m): return [[m[k][i] for k in range(3)] for i in range(3)]
def mm3(a, b): return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def ortho(m):  # strip scale from a rotation*scale block
    cols = [[m[0][c], m[1][c], m[2][c]] for c in range(3)]
    out = []
    for c in cols:
        l = math.sqrt(sum(x * x for x in c)) or 1; out.append([x / l for x in c])
    return [[out[c][r] for c in range(3)] for r in range(3)]

def mat_quat(m):
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1) * 2; w = .25 * s; x = (m[2][1] - m[1][2]) / s; y = (m[0][2] - m[2][0]) / s; z = (m[1][0] - m[0][1]) / s
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1 + m[0][0] - m[1][1] - m[2][2]) * 2; w = (m[2][1] - m[1][2]) / s; x = .25 * s; y = (m[0][1] + m[1][0]) / s; z = (m[0][2] + m[2][0]) / s
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1 + m[1][1] - m[0][0] - m[2][2]) * 2; w = (m[0][2] - m[2][0]) / s; x = (m[0][1] + m[1][0]) / s; y = .25 * s; z = (m[1][2] + m[2][1]) / s
    else:
        s = math.sqrt(1 + m[2][2] - m[0][0] - m[1][1]) * 2; w = (m[1][0] - m[0][1]) / s; x = (m[0][2] + m[2][0]) / s; y = (m[1][2] + m[2][1]) / s; z = .25 * s
    l = math.sqrt(x * x + y * y + z * z + w * w); return (x / l, y / l, z / l, w / l)

def slerp_key(times, values, t):
    if t <= times[0]: return values[0]
    if t >= times[-1]: return values[-1]
    lo, hi = 0, len(times) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if times[mid] <= t: lo = mid
        else: hi = mid
    u = (t - times[lo]) / ((times[hi] - times[lo]) or 1)
    a, b = values[lo], values[hi]
    if len(a) == 4:
        d = sum(x * y for x, y in zip(a, b))
        if d < 0: b = [-x for x in b]
        q = [x + (y - x) * u for x, y in zip(a, b)]; l = math.sqrt(sum(x * x for x in q)) or 1
        return [x / l for x in q]
    return [x + (y - x) * u for x, y in zip(a, b)]

def main():
    body_path, bc_path, out_path = sys.argv[1:4]
    tj, tb = read_glb(body_path)
    bj, bb = read_glb(bc_path)
    t_names = [n.get("name", "") for n in tj["nodes"]]
    b_names = [n.get("name", "") for n in bj["nodes"]]
    b_index = {n: i for i, n in enumerate(b_names)}
    t_parent = {c: i for i, n in enumerate(tj["nodes"]) for c in n.get("children", [])}
    b_parent = {c: i for i, n in enumerate(bj["nodes"]) for c in n.get("children", [])}
    joints = tj["skins"][0]["joints"]
    # Rest worlds.
    t_rest = world_matrices(tj); b_rest = world_matrices(bj)
    hips_t = next(i for i in joints if base(t_names[i]) == "Hips")
    hips_b = b_index["Hips"]
    # Hip height ratio over each rig's floor (lowest rest foot joint).
    def floor_y(names, worlds, keys):
        ys = [worlds[i][1][3] for i, n in enumerate(names) if any(k in n for k in keys)]
        return min(ys) if ys else 0
    t_floor = floor_y(t_names, t_rest, ("Toe", "Foot")); b_floor = floor_y(b_names, b_rest, ("Toe_", "Foot_", "SoleRef"))
    hip_scale = (t_rest[hips_t][1][3] - t_floor) / (b_rest[hips_b][1][3] - b_floor)
    # Mapped target joints in hierarchy order (parents first).
    order = []
    def depth(i):
        d = 0
        while i in t_parent: i = t_parent[i]; d += 1
        return d
    for i in sorted(joints, key=depth):
        src = bc_source(base(t_names[i]))
        order.append((i, b_index.get(src) if src else None))
    # Rest alignment: turn each mapped bone's rest direction onto its BC
    # counterpart's (the BC rig rests in an A-pose, Mixamo in a T-pose), so
    # the BC deltas from rest land on a matching starting pose.
    def pos(w): return [w[0][3], w[1][3], w[2][3]]
    t_by_base = {base(t_names[i]): i for i in joints}
    align = {}
    for i, src in order:
        b = base(t_names[i]); child = CHILD.get(b)
        if src is None or child not in t_by_base: continue
        bc_child_name = bc_source(child)
        if not bc_child_name or bc_child_name not in b_index or b_index[bc_child_name] == src: continue
        tc = t_by_base[child]; bcc = b_index[bc_child_name]
        if b in ("Hips",): continue
        td = [a - c for a, c in zip(pos(t_rest[tc]), pos(t_rest[i]))]
        bd = [a - c for a, c in zip(pos(b_rest[bcc]), pos(b_rest[src]))]
        if sum(x * x for x in td) < 1e-8 or sum(x * x for x in bd) < 1e-8: continue
        align[i] = arc(td, bd)
    tj["animations"] = []
    for anim in bj["animations"]:
        # BC local TRS curves for this clip.
        curves = {}
        times_all = set()
        for ch in anim["channels"]:
            smp = anim["samplers"][ch["sampler"]]
            ti = [x[0] for x in accessor(bj, bb, smp["input"])]
            vals = [list(v) for v in accessor(bj, bb, smp["output"])]
            curves[(ch["target"]["node"], ch["target"]["path"])] = (ti, vals)
            times_all.update(ti)
        times = sorted(times_all)
        def bc_local(i, t):
            n = bj["nodes"][i]
            def get(path, default):
                c = curves.get((i, path)); return slerp_key(c[0], c[1], t) if c else n.get(path, default)
            r = quat_mat(get("rotation", [0, 0, 0, 1])); s = get("scale", [1, 1, 1]); tr = get("translation", [0, 0, 0])
            return [[r[k][0] * s[0], r[k][1] * s[1], r[k][2] * s[2], tr[k]] for k in range(3)] + [[0, 0, 0, 1]]
        rot_keys = {i: [] for i, _ in order}; hip_keys = []
        for t in times:
            bw = {}
            def bworld(i):
                if i in bw: return bw[i]
                w = bc_local(i, t); w = m4(bworld(b_parent[i]), w) if i in b_parent else w
                bw[i] = w; return w
            tw = {}  # target animated world rotation (3x3) per joint
            def tparent_world(i):
                p = t_parent.get(i)
                if p is None: return [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
                if p in tw: return tw[p]
                return ortho(r3(t_rest[p]))  # unanimated ancestor (armature root)
            for i, src in order:
                rest_t = ortho(r3(t_rest[i]))
                if src is None:
                    # unmapped: keep its rest local rotation under the animated parent
                    local_rest = ortho(r3(node_local(tj["nodes"][i])))
                    world = mm3(tparent_world(i), local_rest)
                else:
                    delta = mm3(ortho(r3(bworld(src))), t3(ortho(r3(b_rest[src]))))
                    world = mm3(delta, mm3(align[i], rest_t) if i in align else rest_t)
                tw[i] = world
                local = mm3(t3(tparent_world(i)), world)
                rot_keys[i].append(mat_quat(local))
            # Hips: rest position + scaled BC hips travel, in the hips' parent space.
            hb = bworld(hips_b); travel = [(hb[k][3] - b_rest[hips_b][k][3]) * hip_scale for k in range(3)]
            world_pos = [t_rest[hips_t][k][3] + travel[k] for k in range(3)]
            p = t_parent.get(hips_t)
            pw = t_rest[p] if p is not None else [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
            ip = inv_rigid(pw)
            hip_keys.append(tuple(sum(ip[k][c] * world_pos[c] for c in range(3)) + ip[k][3] for k in range(3)))
        inp = append(tj, tb, [(t,) for t in times], 5126, "SCALAR", None, True)
        samplers, channels = [], []
        for i, _ in order:
            samplers.append({"input": inp, "output": append(tj, tb, rot_keys[i], 5126, "VEC4"), "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": i, "path": "rotation"}})
        samplers.append({"input": inp, "output": append(tj, tb, hip_keys, 5126, "VEC3"), "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": hips_t, "path": "translation"}})
        tj["animations"].append({"name": anim["name"], "samplers": samplers, "channels": channels})
    # One skinned mesh: the runtime keys body textures by primitive index.
    mesh_nodes = [n for n in tj["nodes"] if "mesh" in n and "skin" in n]
    prims = [p for n in mesh_nodes if "lash" not in n.get("name", "").lower() for p in tj["meshes"][n["mesh"]]["primitives"]]
    # Eyelash cards are alpha-only; opaque they read as black slabs.
    prims = [p for p in prims if p.get("material") is None or "lash" not in tj["materials"][p["material"]].get("name", "").lower()]
    tj["meshes"].append({"name": "civilian body", "primitives": prims})
    for k, n in enumerate(mesh_nodes):
        if k == 0: n["mesh"] = len(tj["meshes"]) - 1; n["name"] = "Civilian body"
        else: del n["mesh"]; n.pop("skin", None)
    # Textures (base color + normal per primitive), 1024 px, shared images once.
    body_dir = os.path.dirname(out_path); os.makedirs(os.path.join(body_dir, "textures"), exist_ok=True)
    lines = []
    for k, p in enumerate(prims):
        entry = [str(k), "-", "-"]; mat = p.get("material")
        if mat is not None:
            m = tj["materials"][mat]
            for slot, (outer, key) in ((1, ("pbrMetallicRoughness", "baseColorTexture")), (2, (None, "normalTexture"))):
                tex = (m.get(outer, {}) if outer else m).get(key)
                if tex is None: continue
                src = tj["textures"][tex["index"]]["source"]; img = tj["images"][src]
                view = tj["bufferViews"][img["bufferView"]]; o = view.get("byteOffset", 0)
                ext = ".png" if img.get("mimeType", "image/png") == "image/png" else ".jpg"
                fname = f"image{src}{ext}"; path = os.path.join(body_dir, "textures", fname)
                if not os.path.exists(path):
                    open(path, "wb").write(tb[o:o + view["byteLength"]])
                    try:
                        from PIL import Image
                        im = Image.open(path)
                        if max(im.size) > 1024: im.thumbnail((1024, 1024), Image.LANCZOS); im.save(path)
                    except ImportError: pass
                entry[slot] = fname
        lines.append(" ".join(entry))
    open(os.path.join(body_dir, "civ_textures.txt"), "w").write("\n".join(lines) + "\n")
    # Runtime textures come from textures/: drop the embedded images and keep
    # only referenced buffer data.
    # Plain opaque materials: the loader accepts no extensions, blending or
    # emission; the runtime binds the textures itself.
    tj["materials"] = [{"name": m.get("name", f"body_{k}")} for k, m in enumerate(tj.get("materials", []))]
    for key in ("images", "textures", "samplers", "extensionsUsed", "extensionsRequired"): tj.pop(key, None)
    used = sorted({a["bufferView"] for a in tj["accessors"] if "bufferView" in a})
    remap, packed = {}, bytearray()
    for v in used:
        view = tj["bufferViews"][v]; o = view.get("byteOffset", 0)
        while len(packed) % 4: packed.append(0)
        nv = dict(view); nv["byteOffset"] = len(packed); packed += tb[o:o + view["byteLength"]]
        remap[v] = len(remap); tj.setdefault("_views", []).append(nv)
    tj["bufferViews"] = tj.pop("_views")
    for a in tj["accessors"]:
        if "bufferView" in a: a["bufferView"] = remap[a["bufferView"]]
    write_glb(out_path, tj, packed)
    print(f"retargeted {len(tj['animations'])} clips onto {body_path} (hip scale {hip_scale:.3f})")

if __name__ == "__main__":
    main()
