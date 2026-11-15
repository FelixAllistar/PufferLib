#!/usr/bin/env python3
"""Headless check of .dino animation clips (python3 + numpy, no Blender, no GL).

    python3 ocean/cretaceous/tools/proc_dino_check.py DIR [SPECIES ...] [--sim sim.c] [--md OUT.md] [--json OUT.json]

For every species and clip it reports, from the file alone:
  slide   the most a planted foot moves along the ground while it stays down
          (mm; gait clips are in place, so a planted foot must move back at
          exactly the clip's ground speed: that motion is taken out first),
  below   how far the skinned foot vertices go under y = 0 (mm),
  seam    for looping clips, how far any joint jumps from the last frame to
          the first (mm) and the change in joint velocity across the seam
          against the clip's ordinary frame-to-frame change (mm/frame),
  speed   for gait clips, the planted feet's measured ground speed against
          the sim's gait speed (CRET_SPECIES_TABLE in sim.c).
A foot is the joint named "toe" (the ball of the foot) on each leg; it is
planted on the frames where it is within 1 mm of its lowest height in the clip.
"""
import sys, os, re, struct, json
import numpy as np

HZ = 30.0
SPECIES = ["raptor", "stego", "trex", "pachy", "sauropelta", "galli"]
GAITS = {"walk": "walk", "trot": "trot", "run": "run"}


def read_dino(path):
    b = open(path, "rb").read(); o = 0
    def take(fmt):
        nonlocal o; v = struct.unpack_from(fmt, b, o); o += struct.calcsize(fmt); return v
    magic, ver = take("<II"); assert magic == 0x4F4E4443 and ver == 1, "not a .dino"
    length, height = take("<ff"); take("<6f")
    nj, = take("<I"); names, parent, ibm, rest = [], [], [], []
    for _ in range(nj):
        names.append(b[o:o + 32].split(b"\0")[0].decode()); o += 32
        parent.append(take("<i")[0]); ibm.append(np.array(take("<16f")).reshape(4, 4).T); rest.append(np.array(take("<8f")))
    nmat, = take("<I"); o += 36 * nmat
    nlod, = take("<I"); lod0 = None
    vt = np.dtype([("p", "<f4", 3), ("uv", "<f4", 2), ("n", "i1", 4), ("t", "i1", 4), ("j", "u1", 4), ("w", "u1", 4)])
    for l in range(nlod):
        nv, ni, ns = take("<III"); o += 12 * ns
        v = np.frombuffer(b, vt, nv, o); o += nv * vt.itemsize + 4 * ni
        if l == 0: lod0 = v
    nclip, = take("<I"); clips = {}
    for _ in range(nclip):
        nm = b[o:o + 32].split(b"\0")[0].decode(); o += 32
        dur, speed, flags, nf = take("<ffII")
        fr = np.frombuffer(b, "<f4", nf * nj * 8, o).reshape(nf, nj, 8); o += fr.nbytes
        clips[nm] = dict(dur=dur, speed=speed, loop=bool(flags & 1), frames=fr)
    return dict(length=length, height=height, names=names, parent=parent, ibm=np.array(ibm), lod0=lod0, clips=clips)


def quat_mats(q):
    x, y, z, w = q[..., 0], q[..., 1], q[..., 2], q[..., 3]
    m = np.empty(q.shape[:-1] + (3, 3))
    m[..., 0, 0] = 1 - 2 * (y * y + z * z); m[..., 0, 1] = 2 * (x * y - z * w); m[..., 0, 2] = 2 * (x * z + y * w)
    m[..., 1, 0] = 2 * (x * y + z * w); m[..., 1, 1] = 1 - 2 * (x * x + z * z); m[..., 1, 2] = 2 * (y * z - x * w)
    m[..., 2, 0] = 2 * (x * z - y * w); m[..., 2, 1] = 2 * (y * z + x * w); m[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return m


def globals_of(d, frames):
    """(nf, nj, 8) local TRS -> (nf, nj, 4, 4) model-space joint matrices."""
    nf, nj = frames.shape[:2]
    Lm = np.zeros((nf, nj, 4, 4)); Lm[..., :3, :3] = quat_mats(frames[..., 3:7].astype(np.float64)) * frames[..., 7:8, None]
    Lm[..., :3, 3] = frames[..., :3]; Lm[..., 3, 3] = 1
    G = np.zeros_like(Lm)
    for j in range(nj):
        p = d["parent"][j]
        G[:, j] = Lm[:, j] if p < 0 else G[:, p] @ Lm[:, j]
    return G


def foot_verts(d):
    """LOD0 vertices whose strongest joint is in a foot (the ankle, foot or toe joints and below)."""
    nm = [n.lower() for n in d["names"]]
    foot = np.array([any(k in n for k in ("toe", "ankle", "foot")) for n in nm])
    v = d["lod0"]; strongest = v["j"][np.arange(len(v)), np.argmax(v["w"], 1)]
    return np.nonzero(foot[strongest])[0]


def skin_min_y(d, G, sel):
    v = d["lod0"][sel]; S = G @ d["ibm"][None]                     # (nf, nj, 4, 4)
    p = np.concatenate([v["p"].astype(np.float64), np.ones((len(v), 1))], 1)
    w = v["w"].astype(np.float64) / 255.0; jj = v["j"].astype(np.int64)
    y = np.zeros((S.shape[0], len(v)))
    for k in range(4):
        y += w[:, k][None] * np.einsum("fvj,vj->fv", S[:, jj[:, k], 1, :], p)
    return y.min()


def check_clip(d, name, c, sim_speed):
    G = globals_of(d, c["frames"]); nf = len(G); L = d["length"]
    feet = [j for j, n in enumerate(d["names"]) if "toe" in n.lower()]
    v = c["speed"] if name in GAITS else 0.0
    t = np.arange(nf) / HZ
    slide = 0.0; speeds = []
    for j in feet:
        P = G[:, j, :3, 3]
        down = P[:, 1] <= P[:, 1].min() + 1e-3
        ground = P.copy(); ground[:, 0] += v * t                        # where the foot is on the ground
        # Runs of planted frames (wrapping round for loops).
        idx = np.nonzero(down)[0]
        if len(idx) == 0: continue
        runs, cur = [], [idx[0]]
        for i in idx[1:]:
            if i == cur[-1] + 1: cur.append(i)
            else: runs.append(cur); cur = [i]
        runs.append(cur)
        if c["loop"] and len(runs) > 1 and runs[0][0] == 0 and runs[-1][-1] == nf - 1:
            # The last frame repeats the first: join the run across the seam, shifting by one cycle.
            last = runs.pop(); first = runs.pop(0)
            seam = [(i, -c["dur"]) for i in last[:-1]] + [(i, 0.0) for i in first]
            runs.append(seam)
        else:
            runs = [[(i, 0.0) for i in r] for r in runs]
        for r in runs:
            if len(r) < 2: continue
            pts = np.array([[ground[i, 0] + v * sh, ground[i, 2]] for i, sh in r])
            slide = max(slide, float(np.max(np.linalg.norm(pts - pts[0], axis=1))))
            if name in GAITS:
                xs = np.array([P[i, 0] for i, _ in r]); ts = np.array([t[i] + sh for i, sh in r])
                speeds.append(-np.polyfit(ts, xs, 1)[0])
    below = max(0.0, -skin_min_y(d, G, foot_verts(d)))
    out = dict(clip=name, frames=nf, dur=c["dur"], loop=c["loop"], slide_mm=slide * 1000, below_mm=below * 1000)
    if c["loop"]:
        X = G[:, :, :3, 3]
        out["seam_mm"] = float(np.max(np.linalg.norm(X[-1] - X[0], axis=1))) * 1000
        if nf > 3:
            step = np.linalg.norm(np.diff(X, axis=0), axis=2)            # (nf-1, nj)
            acc = np.linalg.norm((X[1] - X[0]) - (X[-1] - X[-2]), axis=1).max()
            inner = np.linalg.norm(np.diff(X, 2, axis=0), axis=2).max()
            out["seam_vel_mm"] = float(acc) * 1000; out["inner_vel_mm"] = float(inner) * 1000
    if name in GAITS:
        out["ground_speed"] = float(np.median(speeds)) if speeds else 0.0
        out["clip_speed"] = c["speed"]; out["sim_speed"] = sim_speed
    return out


def sim_speeds(path, asset):
    src = open(path).read()
    m = re.search(r'\{"[^"]*",\s*"%s",\s*CRET_DIET_\w+,([^}]*)' % re.escape(asset), src)
    v = [float(x) for x in re.findall(r"-?\d+\.\d+|-?\d+", m.group(1))]
    return dict(walk=v[4], trot=v[5], run=v[6]), v[0], v[1]


def main():
    args = sys.argv[1:]; opt = {"--sim": os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sim.c"), "--md": "", "--json": ""}
    pos = []
    i = 0
    while i < len(args):
        if args[i] in opt: opt[args[i]] = args[i + 1]; i += 2
        else: pos.append(args[i]); i += 1
    d_dir = pos[0]; species = pos[1:] or SPECIES
    rows = []; worst = dict(slide=0.0, below=0.0, seam=0.0, speed=0.0)
    lines = ["| species | clip | frames | loop | foot slide (mm) | feet below y=0 (mm) | loop seam (mm) | seam vel. step / largest in clip (mm/frame) | ground speed / sim (m/s) |",
             "|---|---|---|---|---|---|---|---|---|"]
    for sp in species:
        path = os.path.join(d_dir, sp + ".dino")
        if not os.path.exists(path): print("missing %s" % path, file=sys.stderr); continue
        d = read_dino(path); spd, sim_len, sim_h = sim_speeds(opt["--sim"], sp)
        for name in sorted(d["clips"], key=lambda n: ["idle", "idle2", "walk", "trot", "run", "jump", "eat", "drink", "call", "attack", "sleep", "hurt", "death"].index(n) if n in ["idle", "idle2", "walk", "trot", "run", "jump", "eat", "drink", "call", "attack", "sleep", "hurt", "death"] else 99):
            r = check_clip(d, name, d["clips"][name], spd.get(name)); r["species"] = sp; rows.append(r)
            seam = "%.2f" % r["seam_mm"] if "seam_mm" in r else "-"
            sv = "%.1f / %.1f" % (r["seam_vel_mm"], r["inner_vel_mm"]) if "seam_vel_mm" in r else "-"
            gs = "%.2f / %.2f" % (r["ground_speed"], r["sim_speed"]) if "ground_speed" in r else "-"
            lines.append("| %s | %s | %d | %s | %.2f | %.1f | %s | %s | %s |" % (sp, name, r["frames"], "yes" if r["loop"] else "no", r["slide_mm"], r["below_mm"], seam, sv, gs))
            if name in GAITS or name in ("idle", "idle2", "eat", "drink", "call", "attack", "hurt"):
                worst["slide"] = max(worst["slide"], r["slide_mm"])
            worst["below"] = max(worst["below"], r["below_mm"])
            if "seam_mm" in r: worst["seam"] = max(worst["seam"], r["seam_mm"])
            if "ground_speed" in r: worst["speed"] = max(worst["speed"], abs(r["ground_speed"] - r["sim_speed"]) / r["sim_speed"])
    print("\n".join(lines))
    print("worst: foot slide %.2f mm, feet below ground %.1f mm, loop seam %.2f mm, ground speed off by %.1f%%"
          % (worst["slide"], worst["below"], worst["seam"], worst["speed"] * 100))
    if opt["--md"]: open(opt["--md"], "w").write("\n".join(lines) + "\n")
    if opt["--json"]: json.dump(rows, open(opt["--json"], "w"), indent=1)


if __name__ == "__main__":
    main()
