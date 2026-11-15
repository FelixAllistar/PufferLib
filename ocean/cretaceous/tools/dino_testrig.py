#!/usr/bin/env python3
"""Synthetic test rigs in the game's .dino format (render/dino.c load_model).

Development only: box-limbed stand-ins with a named skeleton (spine, neck,
head, tail, two or four IK legs) and simple procedural clips (idle, walk,
run), so render/dino.c's animation path -- clips, foot IK and the procedural
layers (render/dino_proc.h) -- can run and be captured without the licensed
models (tools/fetch_dinos.sh). Pure code, no third-party data.

    python3 ocean/cretaceous/tools/dino_testrig.py OUT_DIR [raptor|stego ...]

Writes OUT_DIR/<species>.dino (no textures: the loader falls back to flat
colours). The game reads ocean/cretaceous/assets/dinos/, which git ignores;
delete the files again to get the capsules (or the real models) back.
"""
import math
import os
import struct
import sys

MAGIC, VERSION = 0x4F4E4443, 1


def quat_z(a):
    return (0.0, 0.0, math.sin(a / 2), math.cos(a / 2))


def quat_y(a):
    return (0.0, math.sin(a / 2), 0.0, math.cos(a / 2))


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


class Rig:
    def __init__(self):
        self.names, self.parent, self.pos, self.thick = [], [], [], []

    def add(self, name, parent, pos, thick):
        self.names.append(name)
        self.parent.append(parent)
        self.pos.append(pos)
        self.thick.append(thick)
        return len(self.names) - 1

    def local(self, j):
        p = self.parent[j]
        if p < 0:
            return self.pos[j]
        return tuple(self.pos[j][k] - self.pos[p][k] for k in range(3))


def biped(L):
    r, s = Rig(), L / 3.0
    root = r.add("root", -1, (0, 0, 0), 0)
    pel = r.add("pelvis", root, (0, 1.0 * s, 0), 0.22 * s)
    sp1 = r.add("spine1", pel, (0.35 * s, 1.05 * s, 0), 0.24 * s)
    sp2 = r.add("spine2", sp1, (0.7 * s, 1.1 * s, 0), 0.22 * s)
    n0 = r.add("neck", sp2, (0.95 * s, 1.25 * s, 0), 0.12 * s)
    n1 = r.add("neck1", n0, (1.15 * s, 1.45 * s, 0), 0.10 * s)
    r.add("head", n1, (1.4 * s, 1.52 * s, 0), 0.12 * s)
    t = pel
    for k, (x, th) in enumerate([(-0.3, 0.18), (-0.65, 0.14), (-1.0, 0.10), (-1.35, 0.07), (-1.6, 0.04)]):
        t = r.add("tail" if k == 0 else "tail%d" % k, t, (x * s, (1.0 - 0.04 * k) * s, 0), th * s)
    for side, z in (("l", -0.16), ("r", 0.16)):
        h = r.add("upperleg." + side, pel, (0, 0.95 * s, z * s), 0.09 * s)
        k = r.add("lowerleg." + side, h, (0.16 * s, 0.52 * s, z * s), 0.07 * s)
        a = r.add("ankle." + side, k, (-0.06 * s, 0.1 * s, z * s), 0.05 * s)
        r.add("toe." + side, a, (0.14 * s, 0.0, z * s), 0.04 * s)
    return r


def quadruped(L):
    r, s = Rig(), L / 8.5
    root = r.add("root", -1, (0, 0, 0), 0)
    pel = r.add("pelvis", root, (-1.0 * s, 2.6 * s, 0), 0.6 * s)
    sp1 = r.add("spine1", pel, (0.2 * s, 2.7 * s, 0), 0.75 * s)
    sp2 = r.add("spine2", sp1, (1.3 * s, 2.3 * s, 0), 0.6 * s)
    n0 = r.add("neck", sp2, (2.0 * s, 1.9 * s, 0), 0.3 * s)
    n1 = r.add("neck1", n0, (2.6 * s, 1.6 * s, 0), 0.25 * s)
    r.add("head", n1, (3.2 * s, 1.4 * s, 0), 0.25 * s)
    t = pel
    for k, (x, th) in enumerate([(-1.9, 0.4), (-2.8, 0.3), (-3.6, 0.22), (-4.4, 0.15), (-5.0, 0.08)]):
        t = r.add("tail" if k == 0 else "tail%d" % k, t, (x * s, (2.5 - 0.3 * k) * s, 0), th * s)
    for side, z in (("l", -0.5), ("r", 0.5)):
        h = r.add("upperleg." + side, pel, (-1.0 * s, 2.4 * s, z * s), 0.3 * s)
        k = r.add("lowerleg." + side, h, (-0.8 * s, 1.2 * s, z * s), 0.22 * s)
        r.add("backfoot." + side, k, (-1.0 * s, 0.05 * s, z * s), 0.18 * s)
        h = r.add("upperarm." + side, sp2, (1.3 * s, 1.9 * s, z * s), 0.22 * s)
        k = r.add("lowerarm." + side, h, (1.2 * s, 0.9 * s, z * s), 0.18 * s)
        r.add("foot." + side, k, (1.3 * s, 0.05 * s, z * s), 0.15 * s)
    return r


def box_mesh(r):
    """One box per bone (parent to child), skinned to the parent."""
    verts, idx = [], []
    for j in range(len(r.names)):
        p = r.parent[j]
        if p < 0 or r.thick[j] <= 0:
            continue
        a, b, th = r.pos[p], r.pos[j], max(r.thick[j], 0.02)
        d = [b[k] - a[k] for k in range(3)]
        ln = math.sqrt(sum(x * x for x in d)) or 1e-3
        u = [x / ln for x in d]
        ref = (0, 0, 1) if abs(u[2]) < 0.9 else (1, 0, 0)
        v = [u[1] * ref[2] - u[2] * ref[1], u[2] * ref[0] - u[0] * ref[2], u[0] * ref[1] - u[1] * ref[0]]
        vl = math.sqrt(sum(x * x for x in v))
        v = [x / vl for x in v]
        w = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        h = th * 0.5
        corners = []
        for t in (0, 1):
            for sv in (-1, 1):
                for sw in (-1, 1):
                    corners.append([a[k] + d[k] * t + v[k] * sv * h + w[k] * sw * h for k in range(3)])
        faces = [((0, 1, 3, 2), [-x for x in u]), ((4, 6, 7, 5), u), ((0, 4, 5, 1), [-x for x in v]),
                 ((2, 3, 7, 6), v), ((0, 2, 6, 4), [-x for x in w]), ((1, 5, 7, 3), w)]
        for quad, n in faces:
            base = len(verts)
            for q in quad:
                verts.append((corners[q], n, p))
            # Wind each quad outward.
            c = [corners[q] for q in quad]
            e1 = [c[1][k] - c[0][k] for k in range(3)]
            e2 = [c[2][k] - c[0][k] for k in range(3)]
            cr = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
            if sum(cr[k] * n[k] for k in range(3)) >= 0:
                idx += [base, base + 1, base + 2, base, base + 2, base + 3]
            else:
                idx += [base, base + 2, base + 1, base, base + 3, base + 2]
    return verts, idx


def clip_frames(r, kind, nf, dur, quad):
    frames = []
    for f in range(nf):
        ph = 2 * math.pi * f / (nf - 1)
        fr = []
        for j, nm in enumerate(r.names):
            t = list(r.local(j))
            q = (0.0, 0.0, 0.0, 1.0)
            amp = {"walk": 0.45, "run": 0.75}.get(kind, 0.0)
            if nm == "pelvis":
                bob = {"walk": 0.02, "run": 0.05, "idle": 0.004}[kind]
                t[1] += bob * math.sin(2 * ph) * (r.pos[j][1] or 1)
            side = 1 if nm.endswith(".r") else -1
            off = 0 if side < 0 else math.pi
            if nm.startswith(("upperleg", "upperarm")) and amp:
                q = quat_z(amp * math.sin(ph + off + (math.pi / 2 if nm.startswith("upperarm") else 0)))
            if nm.startswith(("lowerleg", "lowerarm")) and amp:
                q = quat_z(-amp * 0.8 * max(0.0, math.sin(ph + off + 1.2)))
            if nm.startswith("tail") and kind != "idle":
                q = quat_y(0.04 * math.sin(ph + 0.6 * len(nm)))
            if nm == "neck":
                q = quat_z(0.03 * math.sin(2 * ph))
            fr.extend(t + list(q) + [1.0])
        frames.append(fr)
    return frames


def write(path, r, length, quad, speeds):
    height = max(p[1] for p in r.pos) + max(r.thick)
    xs = [p[0] for p in r.pos]
    zs = [p[2] for p in r.pos]
    verts, idx = box_mesh(r)
    with open(path, "wb") as f:
        f.write(struct.pack("<IIff", MAGIC, VERSION, length, height))
        f.write(struct.pack("<3f", min(xs), 0.0, min(zs) - 0.2))
        f.write(struct.pack("<3f", max(xs), height, max(zs) + 0.2))
        f.write(struct.pack("<I", len(r.names)))
        for j, nm in enumerate(r.names):
            g = r.pos[j]
            ib = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -g[0], -g[1], -g[2], 1]   # column-major inverse bind: translate by -g
            f.write(nm.encode().ljust(32, b"\0")[:32])
            f.write(struct.pack("<i", r.parent[j]))
            f.write(struct.pack("<16f", *ib))
            f.write(struct.pack("<8f", *r.local(j), 0, 0, 0, 1, 1))
        f.write(struct.pack("<I", 1))
        f.write(b"body".ljust(32, b"\0") + struct.pack("<I", 0))
        f.write(struct.pack("<I", 1))
        f.write(struct.pack("<III", len(verts), len(idx), 1))
        f.write(struct.pack("<III", 0, 0, len(idx)))
        for p, n, j in verts:
            nb = [max(-127, min(127, int(round(x * 127)))) for x in n]
            # Tangent: perpendicular to the normal (a degenerate TBN shades NaN).
            ref = (0, 1, 0) if abs(n[1]) < 0.9 else (1, 0, 0)
            t = [ref[1] * n[2] - ref[2] * n[1], ref[2] * n[0] - ref[0] * n[2], ref[0] * n[1] - ref[1] * n[0]]
            tl = math.sqrt(sum(x * x for x in t)) or 1.0
            tb = [max(-127, min(127, int(round(x / tl * 127)))) for x in t]
            f.write(struct.pack("<3f2f4b4b4B4B", p[0], p[1], p[2], 0.5, 0.5, nb[0], nb[1], nb[2], 0, tb[0], tb[1], tb[2], 127, j, 0, 0, 0, 255, 0, 0, 0))
        f.write(struct.pack("<%dI" % len(idx), *idx))
        clips = [("idle", 2.0, 0.0), ("walk", 1.0, speeds[0]), ("run", 0.6, speeds[1])]
        f.write(struct.pack("<I", len(clips)))
        for name, dur, spd in clips:
            nf = int(round(dur * 30)) + 1
            fr = clip_frames(r, name, nf, dur, quad)
            f.write(name.encode().ljust(32, b"\0") + struct.pack("<ffII", dur, spd, 1, nf))
            for row in fr:
                f.write(struct.pack("<%df" % len(row), *row))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    species = sys.argv[2:] or ["raptor", "stego"]
    for sp in species:
        if sp == "raptor":
            write(os.path.join(out, "raptor.dino"), biped(3.0), 3.0, False, (1.8, 6.0))
        elif sp == "stego":
            write(os.path.join(out, "stego.dino"), quadruped(8.5), 8.5, True, (2.0, 4.2))
        else:
            print("unknown test rig %s (raptor, stego)" % sp)
            return 2
        print("dino_testrig: wrote %s/%s.dino" % (out, sp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
