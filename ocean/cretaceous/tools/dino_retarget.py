#!/usr/bin/env python3
"""Copies animation clips from one converted dinosaur onto another's skeleton.

    dino_retarget.py SOURCE.dino TARGET.dino OUT.dino --rig RIG [--clips a=b,c ...]

Some models come with one or four clips (the tyrannosaur: idle, walk, run,
attack; the gallimimus: one run cycle) while the raptor has twenty-five. Both
are the same kind of animal, so the raptor's eating, drinking, sleeping,
calling, flinching and dying can be played on their skeletons.

How: both skeletons are compared in a standing reference pose (each model's
idle, or the average of two gait frames half a cycle apart when it has none).
For every frame of a source clip, each source bone's turn away from its
reference orientation is taken in model space (both models face +x with +y
up after tools/dino_convert.py) and applied to the matching target bone's
reference orientation. Model-space turns do not depend on how either rig
orients its bones. Chains of different lengths (spine, neck, tail) match by
how far along the chain a bone sits. The pelvis moves with the source's,
scaled by hip height. Bones without a match keep their reference pose and
follow their parents. Bones a rig hangs elsewhere (the gallimimus's head,
which is not a child of its neck; its toes, which hang from foot targets)
are carried rigidly by the bone they belong to.

--clips NAME or SOURCE_NAME=NAME: which clips, and what to call them in the
target (the game finds clips by name: idle, idle2, eat, drink, call, sleep,
hurt, death, attack, jump). NAME+loop marks the copy as looping (an eating
clip that ends standing then repeats its bites). Existing clips of the same
name are replaced.
Needs numpy. The source's licence carries over: the output is a derivative
of both models (assets/dinos/CREDITS.md).
"""
import argparse, struct, sys
import numpy as np

VERT = 36   # bytes per vertex (render/dino.c DVert)


def load(path):
    d = open(path, "rb").read()
    o = 40
    nj = struct.unpack_from("I", d, o)[0]; o += 4
    names, parent, rest = [], [], []
    for _ in range(nj):
        names.append(d[o:o + 32].split(b"\0")[0].decode()); parent.append(struct.unpack_from("i", d, o + 32)[0])
        rest.append(struct.unpack_from("8f", d, o + 100)); o += 132
    joints_end = o
    nmat = struct.unpack_from("I", d, o)[0]; o += 4 + nmat * 36
    nlod = struct.unpack_from("I", d, o)[0]; o += 4
    for _ in range(nlod):
        nv, ni, ns = struct.unpack_from("III", d, o); o += 12 + ns * 12 + nv * VERT + ni * 4
    clips_at = o
    nc = struct.unpack_from("I", d, o)[0]; o += 4
    clips = []
    for _ in range(nc):
        nm = d[o:o + 32].split(b"\0")[0].decode(); dur, spd, fl, nf = struct.unpack_from("ffII", d, o + 32); o += 48
        fr = np.frombuffer(d, dtype="<f4", count=nf * nj * 8, offset=o).reshape(nf, nj, 8).copy(); o += nf * nj * 32
        clips.append(dict(name=nm, duration=dur, speed=spd, flags=fl, frames=fr))
    assert o == len(d), "trailing bytes in " + path
    return dict(head=d[:clips_at], names=names, parent=parent, rest=np.array(rest, dtype=np.float32), clips=clips, nj=nj, joints_end=joints_end)


def save(path, m):
    out = bytearray(m["head"]); out += struct.pack("I", len(m["clips"]))
    for c in m["clips"]:
        out += c["name"].encode()[:31].ljust(32, b"\0") + struct.pack("ffII", c["duration"], c["speed"], c["flags"], len(c["frames"]))
        out += c["frames"].astype("<f4").tobytes()
    open(path, "wb").write(out)


def q_mat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def mat_q(m):
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = np.sqrt(t + 1) * 2; q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1 + m[0, 0] - m[1, 1] - m[2, 2]) * 2; q = [s / 4, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1 + m[1, 1] - m[0, 0] - m[2, 2]) * 2; q = [(m[0, 1] + m[1, 0]) / s, s / 4, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1 + m[2, 2] - m[0, 0] - m[1, 1]) * 2; q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, s / 4, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q); return q / np.linalg.norm(q)


def slerp(a, b, t):
    d = float(np.dot(a, b))
    if d < 0: b, d = -b, -d
    if d > 0.9995: q = a + (b - a) * t; return q / np.linalg.norm(q)
    th = np.arccos(d); return (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / np.sin(th)


def globals_of(m, pose):
    """Per joint: model-space rotation (3x3, scale removed), position, and accumulated scale."""
    R = np.zeros((m["nj"], 3, 3)); P = np.zeros((m["nj"], 3)); S = np.ones(m["nj"])
    for j in range(m["nj"]):
        r = q_mat(pose[j, 3:7]); t = pose[j, :3]; s = pose[j, 7]; p = m["parent"][j]
        if p < 0: R[j], P[j], S[j] = r, t, s
        else: R[j] = R[p] @ r; P[j] = P[p] + S[p] * (R[p] @ t); S[j] = S[p] * s
    return R, P, S


def find(m, key):
    k = key.lower()
    for i, n in enumerate(m["names"]):
        base = n.lower().rsplit("_", 1)[0]   # the converter appends "_NN"
        if base == k: return i
    raise SystemExit(f"no joint {key!r}")


def reference(m):
    idle = next((c for c in m["clips"] if c["name"] == "idle"), None)
    if idle is not None: return idle["frames"][0].copy()
    gait = next(c for c in m["clips"] if c["name"] in ("walk", "run", "trot"))
    f = gait["frames"]; a, b = f[0], f[len(f) // 2]; ref = a.copy()
    for j in range(m["nj"]): ref[j, 3:7] = slerp(a[j, 3:7], b[j, 3:7], 0.5); ref[j, :3] = 0.5 * (a[j, :3] + b[j, :3])
    return ref


# Rigs: chains (matched along their length), single bones, and bones carried
# rigidly by another ("attach": child follows carrier as in the reference).
# The raptor's "Root" lies on the ground and carries root motion; its hips are Spine_1.
RAPTOR = dict(pelvis="Spine_1", ground="Root", thigh="UpperLeg.L", chains=dict(spine=["Spine_1", "Spine_2", "Spine_3"], neck=["Neck_1", "Neck_2", "Neck_3", "Head"],
                                         tail=[f"Tail_{i}" for i in range(1, 11)]),
              bones=dict(jaw="Jaw_1", thighL="UpperLeg.L", shinL="LowerLeg.L", metaL="Ankle.L", toesL="Foot.L",
                         thighR="UpperLeg.R", shinR="LowerLeg.R", metaR="Ankle.R", toesR="Foot.R",
                         uarmL="UpperArm.L", farmL="LowerArm.L", handL="Hand.L", uarmR="UpperArm.R", farmR="LowerArm.R", handR="Hand.R"))
RIGS = {
    "trex": dict(pelvis="DEF-pelvis", thigh="DEF-thigh.L", chains=dict(spine=[f"DEF-spine.0{i}" for i in range(1, 5)], neck=[f"DEF-neck.0{i}" for i in range(1, 5)] + ["DEF-head"],
                                                 tail=[f"DEF-tail.{i:02d}" for i in range(1, 13)]),
                 bones=dict(jaw="DEF-jaw", thighL="DEF-thigh.L", shinL="DEF-shin.L", metaL="DEF-metatarsus.L",
                            thighR="DEF-thigh.R", shinR="DEF-shin.R", metaR="DEF-metatarsus.R",
                            uarmL="DEF-upperarm.L", farmL="DEF-forearm.L", handL="DEF-hand.L", uarmR="DEF-upperarm.R", farmR="DEF-forearm.R", handR="DEF-hand.R"),
                 multi=dict(toesL=[f"DEF-toe.{n}.01.L" for n in ("I", "II", "III", "IV")], toesR=[f"DEF-toe.{n}.01.R" for n in ("I", "II", "III", "IV")]),
                 attach={}),
    "galli": dict(pelvis="bip_pelvis", thigh="bip_hip_l", chains=dict(spine=["bip_spine_0", "bip_spine_1", "bip_spine_2"], neck=[f"bip_neck_{i}" for i in range(5)] + ["bip_head"],
                                                  tail=["Bone.010", "Bone.012", "Bone.013", "Bone.014", "Bone.015", "Bone.016"]),
                  bones=dict(jaw="bip_jaw", thighL="bip_hip_l", shinL="bip_knee_l", metaL="bip_foot_l", toesL="bip_toe_l",
                             thighR="bip_hip_r", shinR="bip_knee_r", metaR="bip_foot_r", toesR="bip_toe_r",
                             uarmL="bip_upperarm_l", farmL="bip_lowerarm_l", handL="bip_hand_l", uarmR="bip_upperarm_r", farmR="bip_lowerarm_r", handR="bip_hand_r"),
                  multi={},
                  # The head hangs from the chest, not the neck; the foot targets hang from the root.
                  attach={"bip_head": "bip_neck_4", "legik.l": "bip_foot_l", "legik.r": "bip_foot_r"}),
}


def retarget(src, tgt, rig, clip, name, loop=False):
    sref, tref = reference(src), reference(tgt)
    sR0, sP0, _ = globals_of(src, sref); tR0, tP0, tS0 = globals_of(tgt, tref)
    sp, tp = find(src, RAPTOR["pelvis"]), find(tgt, rig["pelvis"])
    hip = tP0[find(tgt, rig["thigh"]), 1] / max(sP0[find(src, RAPTOR["thigh"]), 1], 1e-6)   # leg length ratio, from the hip joints' heights
    sg = find(src, RAPTOR["ground"])
    # target joint -> how to get its turn: ("bone", source joint) or ("chain", [source joints], position along it)
    plan = {tp: ("bone", sp)}
    for k, tj in rig["bones"].items():
        if k in RAPTOR["bones"]: plan[find(tgt, tj)] = ("bone", find(src, RAPTOR["bones"][k]))
    for k, tjs in rig.get("multi", {}).items():
        for tj in tjs: plan[find(tgt, tj)] = ("bone", find(src, RAPTOR["bones"][k]))
    for k, tjs in rig["chains"].items():
        sj = [find(src, n) for n in RAPTOR["chains"][k]]
        for i, tj in enumerate(tjs):
            plan[find(tgt, tj)] = ("chain", sj, i / max(len(tjs) - 1, 1) * (len(sj) - 1))
    attach = {find(tgt, c): find(tgt, p) for c, p in rig["attach"].items()}
    order = range(tgt["nj"])   # file order: parents precede children, and each carrier precedes what it carries in these rigs
    nf = len(clip["frames"]); out = np.zeros((nf, tgt["nj"], 8), dtype=np.float32)
    for f in range(nf):
        sR, sP, _ = globals_of(src, clip["frames"][f])
        D = [sR[j] @ sR0[j].T for j in range(src["nj"])]
        Dq = [mat_q(x) for x in D]
        R = np.zeros((tgt["nj"], 3, 3)); P = np.zeros((tgt["nj"], 3)); S = np.ones(tgt["nj"]); done = [False] * tgt["nj"]
        pose = tref.copy()

        def place(j, Rg=None, Pg=None):
            """Sets joint j's local pose so its model-space rotation is Rg and position Pg (None: as its reference local)."""
            p = tgt["parent"][j]
            Rp, Pp, Sp = (R[p], P[p], S[p]) if p >= 0 else (np.eye(3), np.zeros(3), 1.0)
            if Rg is not None: pose[j, 3:7] = mat_q(Rp.T @ Rg)
            if Pg is not None: pose[j, :3] = (Rp.T @ (Pg - Pp)) / Sp
            R[j] = Rp @ q_mat(pose[j, 3:7]); P[j] = Pp + Sp * (Rp @ pose[j, :3]); S[j] = Sp * pose[j, 7]; done[j] = True

        for j in order:
            if j in attach:   # carried: keeps its reference offset and gets the carrier's turn (or its own, when planned)
                c = attach[j]; assert done[c]
                turn = R[c] @ tR0[c].T
                Pg = P[c] + turn @ (tP0[j] - tP0[c])
                if j in plan: Rg = turn_of(plan[j], D, Dq) @ tR0[j]
                else: Rg = turn @ tR0[j]
                place(j, Rg, Pg)
            elif j in plan:
                # The hips move as the source's do over its ground root (a clip's travel across the ground is left out: the sim moves the animal).
                Pg = tP0[tp] + ((sP[sp] - sP[sg]) - (sP0[sp] - sP0[sg])) * hip if j == tp else None
                place(j, turn_of(plan[j], D, Dq) @ tR0[j], Pg)
            else: place(j)
        out[f] = pose
    return dict(name=name, duration=clip["duration"], speed=0.0, flags=clip["flags"] | (1 if loop else 0), frames=out)


def turn_of(p, D, Dq):
    if p[0] == "bone": return D[p[1]]
    sj, v = p[1], p[2]; i = min(int(v), len(sj) - 2) if len(sj) > 1 else 0
    if len(sj) == 1: return D[sj[0]]
    return q_mat(slerp(Dq[sj[i]], Dq[sj[i + 1]], v - i))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("source"); ap.add_argument("target"); ap.add_argument("out")
    ap.add_argument("--rig", required=True, choices=sorted(RIGS)); ap.add_argument("--clips", required=True)
    a = ap.parse_args()
    src, tgt = load(a.source), load(a.target)
    for spec in a.clips.split(","):
        s, _, n = spec.partition("="); n = n or s
        loop = n.endswith("+loop"); n = n[:-5] if loop else n; s = s[:-5] if s.endswith("+loop") else s
        clip = next((c for c in src["clips"] if c["name"] == s), None)
        if clip is None: sys.exit(f"source has no clip {s!r}")
        new = retarget(src, tgt, RIGS[a.rig], clip, n, loop)
        tgt["clips"] = [c for c in tgt["clips"] if c["name"] != n] + [new]
        print(f"dino_retarget: {s} -> {n}: {len(new['frames'])} frames, {new['duration']:.2f} s")
    save(a.out, tgt)


if __name__ == "__main__":
    main()
