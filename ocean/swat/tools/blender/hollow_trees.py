# The Hollow woods v2: late-autumn Appalachian trees, understory, forest-floor
# scatter and their deterministic placement (assets/environment/hollow_woods/).
# Drawn instanced by hollow_woods.h; presentation only. See HOLLOW.md "Woods".
#
#   python3.13 hollow_foliage_textures.py OUT_DIR            (card atlas first)
#   blender -b --factory-startup --python-exit-code 1 -P hollow_trees.py -- OUT_DIR
#       [--only oak,poplar,...] [--no-impostors] [--no-placement]
#       [--render PNG --view NAME]     (Cycles CPU preview for the hill-climb)
#
# Species (each 2-3 variants, all recursive branching with seeded noise):
#   oak       white/red oak + hickory form: tall clear bole, ascending sinuous
#             limbs, fine twig cards, a few marcescent brown leaves.
#   poplar    tulip poplar: very straight, tall, narrow high crown.
#   sycamore  creek sycamore: crooked white upper limbs, pale twigs.
#   hemlock   eastern hemlock: excurrent, drooping 2-ranked spray cards, dark.
#   pine      eastern white pine: whorled tiers, needle plumes, dead lower stubs.
#   laurel    rhododendron / mountain-laurel thicket shrub (no impostor).
# LODs per tree variant: lod0 full branches to the third order + twig cards;
# lod1 trunk + first two orders + whole-limb cards; lod2 a crossed-quad
# impostor rendered from lod0 (impostor_atlas.png, 8 x 4 cells of 256 x 512).
# Every GLB has material slot 1 = bark/wood/stone (texture chosen by the
# engine from woods_meshes.txt) and slot 2 = foliage cards (foliage_atlas.png),
# exported as placeholders: no images are embedded, the engine binds shared maps.
# Vertex colours carry value variation (wet bases, pale sycamore limbs,
# per-card value) and multiply the albedo in the lit shader.
import sys, os, math, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy, bmesh
import numpy as np
from mathutils import Vector, Matrix
import props_int_v2_common as C

ARGS = C.ARGS
OUT = C.OUT
ONLY = set(ARGS[ARGS.index("--only") + 1].split(",")) if "--only" in ARGS else None
IMPOSTORS = "--no-impostors" not in ARGS
PLACEMENT = "--no-placement" not in ARGS
VIEW = ARGS[ARGS.index("--view") + 1] if "--view" in ARGS else None

# Foliage atlas cells (row, column) -> UV rect; see hollow_foliage_textures.py.
ATLAS = {
    "twig_bare_a": (0, 0), "twig_bare_b": (0, 1), "twig_oak": (0, 2), "twig_poplar": (0, 3),
    "hemlock_a": (1, 0), "hemlock_b": (1, 1), "pine_a": (1, 2), "pine_b": (1, 3),
    "laurel_a": (2, 0), "laurel_b": (2, 1), "fern": (2, 2), "goldenrod": (2, 3),
    "grass": (3, 0), "twig_syc": (3, 1), "branch_big": (3, 2), "branch_big_b": (3, 3),
}
def cell_uv(name):
    r, c = ATLAS[name]; pad = 1.5 / 2048
    # glTF/Blender UV v=0 is the image bottom; card base (v=0) is the cell bottom.
    return (c / 4 + pad, 1 - (r + 1) / 4 + pad, (c + 1) / 4 - pad, 1 - r / 4 - pad)

BARK = {  # Poly Haven CC0 scans (1K colour + normal), see README.md.
    "oak": "jolcham_oak_bark_01", "poplar": "bark_brown_02", "sycamore": "bark_platanus",
    "hemlock": "bark_brown_01", "pine": "pine_bark", "laurel": "bark_willow",
    "rock": "mossy_rock", "log": "jolcham_oak_bark_01",
}

# (saturation kept, value) applied to each bark colour map at export.
BARK_GRADE = {"oak": (.35, 1.1), "poplar": (.35, 1.15), "sycamore": (.4, 1.25), "hemlock": (.6, .95),
              "pine": (.5, 1.0), "laurel": (.5, 1.0), "rock": (.5, 1.0)}

# ------------------------------------------------------------------ mesh builder
class MeshB:
    """Engine-space (x, y up, z) triangles with uv, colour and normal per corner."""
    def __init__(self):
        self.v = []; self.n = []; self.uv = []; self.col = []; self.mat = []; self.tri = []
    def vert(self, p, n, uv, c):
        self.v.append(tuple(p)); self.n.append(tuple(n)); self.uv.append(tuple(uv)); self.col.append(tuple(c)); return len(self.v) - 1
    def face(self, a, b, c, m):
        self.tri.append((a, b, c)); self.mat.append(m)
    def count(self):
        return len(self.tri)

def nrm(v):
    l = float(np.linalg.norm(v)); return v / l if l > 1e-9 else v

def frame(d):
    d = nrm(np.asarray(d, float))
    up = np.array([0, 1.0, 0]) if abs(d[1]) < .95 else np.array([1.0, 0, 0])
    a = nrm(np.cross(d, up)); b = np.cross(a, d)
    return a, b

def tube(mb, pts, radii, sides, col_fn, uv_tile=1.2, v0=0.0):
    """Tapered tube through engine points; smooth normals; bark UVs in metres."""
    pts = [np.asarray(p, float) for p in pts]
    rings = []; v = v0
    prev_a = None
    for i, p in enumerate(pts):
        d = pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]
        a, b = frame(d)
        if prev_a is not None:  # parallel transport: avoid twisting
            a = nrm(prev_a - np.dot(prev_a, nrm(d)) * nrm(d)); b = np.cross(a, nrm(d)) * -1
            b = nrm(np.cross(nrm(d), a))
        prev_a = a
        if i: v += float(np.linalg.norm(pts[i] - pts[i - 1])) / uv_tile
        r = radii[i]; circ = 2 * math.pi * max(r, .04) / uv_tile
        ring = []
        c = col_fn(i / max(1, len(pts) - 1), p)
        for k in range(sides + 1):
            t = 2 * math.pi * k / sides
            n = math.cos(t) * a + math.sin(t) * b
            ring.append(mb.vert(p + r * n, n, (circ * k / sides, v), c))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(sides):
            a0, a1, b0, b1 = rings[i][k], rings[i][k + 1], rings[i + 1][k], rings[i + 1][k + 1]
            mb.face(a0, b1, b0, 0); mb.face(a0, a1, b1, 0)   # outward winding
    return v

def card(mb, base, up, side, w, h, cellname, col, nfn, bend=0.0):
    """Alpha card: base centre, growth axis `up`, width axis `side`. Two
    segments along the length so it can droop (bend, metres at the tip)."""
    u0, v0, u1, v1 = cell_uv(cellname)
    up = nrm(np.asarray(up, float)); side = nrm(np.asarray(side, float))
    rows = []
    for j, t in enumerate((0, .5, 1)):
        c = np.asarray(base, float) + up * h * t + np.array([0, -bend * t * t, 0])
        row = []
        for s in (-1, 1):
            p = c + side * w * .5 * s
            row.append(mb.vert(p, nfn(p), (u0 if s < 0 else u1, v0 + (v1 - v0) * t), col))
        rows.append(row)
    for j in range(2):
        a, b = rows[j]; c2, d = rows[j + 1]
        mb.face(a, b, d, 1); mb.face(a, d, c2, 1)

# ------------------------------------------------------------------ species
SPECIES = {
    #            H range      r base      crown  n1  ang1  len1  up1  n2 ang2 len2 n3 ang3 len3 wig  twigs                      card m
    "oak":      dict(H=(16, 26), R=(.30, .40), crown=.40, n1=12, a1=40, l1=.36, up1=.30, n2=5, a2=42, l2=.55, n3=3, a3=40, l3=.6, wig=.28, cards=("twig_bare_a", "twig_bare_b", "twig_bare_a", "twig_oak"), cs=1.7, forks=2),
    "poplar":   dict(H=(25, 31), R=(.34, .46), crown=.55, n1=13, a1=40, l1=.30, up1=.38, lean=.03, n2=4, a2=38, l2=.55, n3=3, a3=36, l3=.6, wig=.1, cards=("twig_poplar", "twig_bare_b"), cs=1.9, forks=0),
    "sycamore": dict(H=(18, 24), R=(.45, .6), crown=.26, n1=8,  a1=60, l1=.52, up1=.12, lean=.16, n2=5, a2=46, l2=.58, n3=3, a3=40, l3=.55, wig=.26, cards=("twig_syc", "twig_bare_a"), cs=2.0, forks=2),
}
VARIANTS = {"oak": 3, "poplar": 2, "sycamore": 2, "hemlock": 3, "pine": 2, "laurel": 3}

def rot_about(v, axis, ang):
    axis = nrm(axis); v = np.asarray(v, float)
    return v * math.cos(ang) + np.cross(axis, v) * math.sin(ang) + axis * np.dot(axis, v) * (1 - math.cos(ang))

def grow(rng, p0, d0, length, r0, sides, segs, wig, up_bias, droop=0.0):
    """Sinuous branch polyline: returns points, radii, unit dirs."""
    pts = [np.asarray(p0, float)]; d = nrm(np.asarray(d0, float)); radii = [r0]; dirs = [d]
    for s in range(segs):
        t = (s + 1) / segs
        d = nrm(d + rng.normal(0, wig, 3) + np.array([0, up_bias - droop * t, 0]))
        pts.append(pts[-1] + d * length / segs); radii.append(r0 * (1 - .78 * t) + .004); dirs.append(d)
    return pts, radii, dirs

def deciduous(sp, variant, lod):
    P = SPECIES[sp]; rng = np.random.default_rng({"oak": 11, "poplar": 23, "sycamore": 37}[sp] * 100 + variant)
    H = rng.uniform(*P["H"]); R = rng.uniform(*P["R"])
    mb = MeshB(); cards = []
    lean = rng.normal(0, P.get("lean", .11), 2)
    P = dict(P); P["crown"] = float(np.clip(P["crown"] + rng.normal(0, .07), .25, .7))
    def bark_col(base_dark):
        def f(t, p):
            # wet, darker base; sycamore pales sharply above the lower bole
            y = p[1]
            if sp == "sycamore":
                k = .42 + .58 * min(1, max(0, (y - 2.5) / 4))
                return (k, k, k * .97, 1)
            k = base_dark + (1 - base_dark) * min(1, y / 2.5)
            return (k, k * .98, k * .96, 1)
        return f
    sides0 = 7 if lod == 0 else 5
    trunk_pts = []; trunk_r = []
    segs0 = 16 if lod == 0 else 8
    d = np.array([lean[0], 1, lean[1]])
    p = np.zeros(3); wig0 = P["wig"] * (.35 if sp != "sycamore" else .8)
    for s in range(segs0 + 1):
        t = s / segs0
        trunk_pts.append(p.copy())
        flare = 1 + .55 * math.exp(-t * H / .7)          # root flare
        trunk_r.append(R * (1 - .82 * t ** 1.1) * flare + .02)
        d = nrm(d + rng.normal(0, wig0 * .25, 3) * (1 if t > .3 else .3) + np.array([0, .05, 0]))
        p = p + d * H / segs0
    tube(mb, trunk_pts, trunk_r, sides0, bark_col(.95))
    # dead lower limbs: shade-killed stubs and snags below the crown
    if lod == 0:
        for k in range(int(rng.integers(3, 7))):
            t = rng.uniform(.15, P["crown"]); kk = int(t * segs0)
            az = rng.uniform(0, 6.3); dd = nrm(np.array([math.cos(az), rng.uniform(-.2, .5), math.sin(az)]))
            bp, br, _ = grow(rng, trunk_pts[kk], dd, rng.uniform(.4, 2.2), trunk_r[kk] * .18 + .015, 0, 2, .15, -.05)
            tube(mb, bp, br, 3, bark_col(.7))
    crown_c = np.array([0, H * (P["crown"] + 1) / 2, 0])
    def card_n(p):
        return nrm(nrm(p - crown_c) + np.array([0, .9, 0]))
    golden = math.radians(137.5); phase = rng.uniform(0, 6.3)
    n1 = P["n1"] + int(rng.integers(-1, 2))
    forks = P.get("forks", 0)
    for i in range(n1 + forks):
        fork = i >= n1
        t = P["crown"] + (1 - P["crown"]) * (i + rng.uniform(.1, .9)) / n1 * .92 if not fork else P["crown"] + rng.uniform(.0, .12)
        k = min(int(t * segs0), segs0 - 1); f = t * segs0 - k
        base = trunk_pts[k] * (1 - f) + trunk_pts[k + 1] * f
        rb = trunk_r[k] * (1 - f) + trunk_r[k + 1] * f
        az = phase + golden * i
        out = np.array([math.cos(az), 0, math.sin(az)])
        ang = math.radians(P["a1"] + rng.normal(0, 8)) * (1 - .35 * (t - P["crown"]) / (1 - P["crown"]))
        if fork: ang = math.radians(rng.uniform(16, 28))
        d1 = nrm(np.array([0, math.cos(ang), 0]) + out * math.sin(ang))
        L1 = H * P["l1"] * (1.15 - .6 * (t - P["crown"]) / (1 - P["crown"])) * rng.uniform(.8, 1.15)
        if fork: L1 = H * (1 - t) * rng.uniform(.85, 1.0)
        r1 = min(rb * .66, .62 * R * (L1 / (H * P["l1"])) ** 1.2 + .035)
        if fork: r1 = rb * .7
        pts, radii, dirs = grow(rng, base, d1, L1, r1, 0, 6 if lod == 0 else 3, P["wig"], P["up1"] * .2)
        tube(mb, pts, radii, 4, bark_col(1.0))
        # second order
        n2 = P["n2"] + int(rng.integers(-1, 2))
        for j in range(n2):
            u = .25 + .7 * (j + rng.uniform(.2, .8)) / n2
            q = min(int(u * (len(pts) - 1)), len(pts) - 2); g = u * (len(pts) - 1) - q
            b2 = pts[q] * (1 - g) + pts[q + 1] * g; r2b = radii[q] * (1 - g) + radii[q + 1] * g
            pd = dirs[q]
            d2 = rot_about(pd, frame(pd)[0], math.radians(P["a2"] + rng.normal(0, 10)))
            d2 = rot_about(d2, pd, rng.uniform(0, 2 * math.pi))
            d2 = nrm(d2 + np.array([0, P["up1"] * .5, 0]))
            L2 = L1 * P["l2"] * (1.1 - .5 * u) * rng.uniform(.75, 1.15)
            r2 = min(r2b * .7, r1 * .55)
            pts2, rad2, dirs2 = grow(rng, b2, d2, L2, r2, 0, 3 if lod == 0 else 2, P["wig"] * 1.2, P["up1"] * .15)
            if lod == 1:   # no second-order tubes: the limb card carries them
                # whole-limb card from the second-order branch end
                side = nrm(np.cross(dirs2[-1], np.array([0, 1, 0])) + rng.normal(0, .3, 3))
                cards.append((pts2[len(pts2) // 2], dirs2[-1], side, L2 * 1.5, L2 * 1.25, "branch_big_b" if (sp == "oak" and rng.random() < .35) else "branch_big"))
                continue
            tube(mb, pts2, rad2, 3, bark_col(.9))
            # third order as twig cards only: sprays along the outer limb
            n3 = P["n3"] + int(rng.integers(-1, 2))
            for m in range(max(1, n3)):
                w = .12 + .8 * (m + rng.uniform(.2, .8)) / max(1, n3)
                q = min(int(w * (len(pts2) - 1)), len(pts2) - 2); g = w * (len(pts2) - 1) - q
                b3 = pts2[q] * (1 - g) + pts2[q + 1] * g; pd = dirs2[q]
                d3 = rot_about(pd, frame(pd)[0], math.radians(P["a3"] + rng.normal(0, 12)))
                d3 = nrm(rot_about(d3, pd, rng.uniform(0, 2 * math.pi)) + np.array([0, .1, 0]))
                side = nrm(np.cross(d3, np.array([0, 1, 0])) + rng.normal(0, .5, 3))
                cs = P["cs"] * rng.uniform(.6, .95)
                cards.append((b3, d3, side, cs * 1.05, cs, P["cards"][int(rng.integers(len(P["cards"])))]))
            # twig card from the second-order tip too
            side = nrm(np.cross(dirs2[-1], np.array([0, 1, 0])) + rng.normal(0, .5, 3))
            cs = P["cs"] * rng.uniform(.75, 1.05)
            cards.append((pts2[-1] - dirs2[-1] * .2, dirs2[-1], side, cs, cs, P["cards"][int(rng.integers(len(P["cards"])))]))
        # first-order tip: a limb card only at lod1 (lod0 tips carry their own sprays)
        if lod == 1:
            cs = P["cs"] * 1.3
            cards.append((pts[-1] - dirs[-1] * .3, dirs[-1], nrm(np.cross(dirs[-1], [0, 1, 0]) + rng.normal(0, .4, 3)), cs, cs, "branch_big"))
    # leader
    pts, radii, dirs = grow(rng, trunk_pts[-1], [0, 1, 0], H * .12, trunk_r[-1], 0, 3, P["wig"], .1)
    tube(mb, pts, radii, 3, bark_col(.9))
    cards.append((pts[-1] - np.array([0, .4, 0]), [0, 1, 0], [1, 0, 0], P["cs"] * 1.2, P["cs"] * 1.2, P["cards"][0]))
    for (b, u, s, w, h, name) in cards:
        v = float(rng.uniform(.82, 1.0)); col = (v, v, v, 1)
        u = nrm(np.asarray(u, float) + np.array([0, .25, 0]))
        s = nrm(np.asarray(s, float) - np.dot(s, u) * u)
        card(mb, b, u, s, w, h, name, col, card_n, bend=.05 * h)
    return mb, H

def hemlock(variant, lod):
    rng = np.random.default_rng(4100 + variant)
    H = rng.uniform(15, 21); R = rng.uniform(.24, .34); mb = MeshB()
    segs = 16 if lod == 0 else 8
    pts = [np.array([0, H * s / segs, 0]) + np.array([rng.normal(0, .04), 0, rng.normal(0, .04)]) * (s > 0) for s in range(segs + 1)]
    radii = [R * (1 - .9 * s / segs) * (1 + .4 * math.exp(-H * s / segs / .6)) + .015 for s in range(segs + 1)]
    tube(mb, pts, radii, 8 if lod == 0 else 5, lambda t, p: (.55 + .45 * min(1, p[1] / 2), .55 + .45 * min(1, p[1] / 2), .55 + .45 * min(1, p[1] / 2), 1))
    crown_c = np.array([0, H * .5, 0])
    def card_n(p): return nrm(nrm(p - crown_c) * 1.0 + np.array([0, .8, 0]))
    base = (rng.uniform(.08, .15) if variant == 0 else rng.uniform(.28, .4)) * H   # v0 edge-grown, others forest-grown
    n = 64 if lod == 0 else 32
    for i in range(n):
        y = base + (H - base) * (i + rng.uniform(0, 1)) / n
        h = (y - base) / (H - base)
        L = (4.3 * (1 - h) ** .85 + .5) * rng.uniform(.8, 1.15)
        az = rng.uniform(0, 2 * math.pi); out = np.array([math.cos(az), 0, math.sin(az)])
        d = nrm(out + np.array([0, -.15 + .5 * h, 0]))
        p0 = np.array([0, y, 0])
        bp, br, bd = grow(rng, p0, d, L, .07 * (1 - h) + .02, 0, 3 if lod == 0 else 2, .06, -.04, droop=.2)
        if lod == 0 and h < .6: tube(mb, bp, br, 3, lambda t, p: (.8, .8, .8, 1))
        k = 3 if lod == 0 else 2
        for j in range(k):
            t = (j + .1) / k
            q = min(int(t * (len(bp) - 1)), len(bp) - 2)
            cb = bp[q]
            dd = nrm(bd[q] + np.array([0, -.1, 0]))
            side = nrm(np.cross(dd, [0, 1, 0]))
            side = rot_about(side, dd, rng.normal(0, .9))
            w = (L * (1.05 - .3 * t) + .6) * (1.3 if lod else 1.0)
            v = float(rng.uniform(.8, 1.0))
            card(mb, cb, dd, side, w, w * 1.1, "hemlock_a" if rng.random() < .5 else "hemlock_b", (v, v, v, 1), card_n, bend=.25 * w)
    # leader tuft
    card(mb, np.array([0, H - .8, 0]), [0, 1, 0], [1, 0, 0], 1.2, 1.6, "hemlock_a", (1, 1, 1, 1), card_n)
    card(mb, np.array([0, H - .8, 0]), [0, 1, 0], [0, 0, 1], 1.2, 1.6, "hemlock_b", (1, 1, 1, 1), card_n)
    return mb, H

def pine(variant, lod):
    rng = np.random.default_rng(5200 + variant)
    H = rng.uniform(22, 28); R = rng.uniform(.30, .40); mb = MeshB()
    segs = 18 if lod == 0 else 9
    pts = []; p = np.zeros(3); d = np.array([rng.normal(0, .03), 1, rng.normal(0, .03)])
    for s in range(segs + 1):
        pts.append(p.copy()); d = nrm(d + rng.normal(0, .03, 3) + [0, .05, 0]); p = p + d * H / segs
    radii = [R * (1 - .88 * s / segs) * (1 + .35 * math.exp(-H * s / segs / .6)) + .015 for s in range(segs + 1)]
    tube(mb, pts, radii, 8 if lod == 0 else 5, lambda t, p: (.6 + .4 * min(1, p[1] / 2),) * 3 + (1,))
    crown_c = np.array([0, H * .72, 0])
    def card_n(p): return nrm(nrm(p - crown_c) + np.array([0, 1.0, 0]))
    crown = rng.uniform(.42, .55) * H
    y = 2.5
    while y < H - .6:
        h = (y - crown) / (H - crown)
        nb = int(rng.integers(3, 6))
        az0 = rng.uniform(0, 6.3)
        for b in range(nb):
            if y < crown:  # dead lower stubs
                if lod == 0 and rng.random() < .5:
                    az = az0 + 2 * math.pi * b / nb; out = np.array([math.cos(az), -.05, math.sin(az)])
                    bp, br, _ = grow(rng, [0, y, 0], out, rng.uniform(.5, 1.6), .04, 0, 2, .1, -.05)
                    tube(mb, bp, br, 3, lambda t, p: (.7, .7, .7, 1))
                continue
            if rng.random() < .22: continue     # irregular, wind-thinned tiers
            az = az0 + 2 * math.pi * b / nb + rng.normal(0, .25); out = np.array([math.cos(az), 0, math.sin(az)])
            L = (5.2 * (1 - max(h, 0)) ** .6 + .6) * rng.uniform(.65, 1.15)
            bp, br, bd = grow(rng, [0, y, 0], nrm(out + [0, .08, 0]), L, .09 * (1 - h) + .025, 0, 4 if lod == 0 else 2, .08, .06)
            tube(mb, bp, br, 4 if lod == 0 else 3, lambda t, p: (.8, .8, .8, 1))
            k = 5 if lod == 0 else 3
            for j in range(k):
                t = .25 + .75 * (j + .5) / k
                q = min(int(t * (len(bp) - 1)), len(bp) - 2); cb = bp[q]
                dd = nrm(bd[q] + [0, .35, 0])
                side = rot_about(nrm(np.cross(dd, [0, 1, 0])), dd, rng.normal(0, 1.0))
                w = rng.uniform(2.4, 3.3) * (1.3 if lod else 1)
                v = float(rng.uniform(.8, 1.0))
                card(mb, cb, dd, side, w, w * .95, "pine_a" if rng.random() < .5 else "pine_b", (v, v, v, 1), card_n, bend=-.1)
        y += rng.uniform(1.0, 1.6)
    card(mb, np.array([0, H - 1.2, 0]), [0, 1, 0], [1, 0, 0], 1.6, 1.8, "pine_a", (1, 1, 1, 1), card_n)
    return mb, H

def laurel(variant, lod):
    rng = np.random.default_rng(6300 + variant)
    mb = MeshB(); Hs = rng.uniform(1.8, 3.0)
    c0 = np.array([0, Hs * .55, 0])
    def card_n(p): return nrm(nrm(p - c0) + np.array([0, .6, 0]))
    stems = int(rng.integers(5, 8))
    for s in range(stems):
        az = rng.uniform(0, 6.3); out = np.array([math.cos(az), 0, math.sin(az)])
        d = nrm(out * rng.uniform(.4, 1.0) + [0, 1, 0])
        L = Hs * rng.uniform(.7, 1.1)
        bp, br, bd = grow(rng, out * rng.uniform(0, .3), d, L, .03, 0, 4, .25, -.02, droop=.4)
        if lod == 0: tube(mb, bp, br, 3, lambda t, p: (.8, .8, .8, 1))
        for j in range(3 if lod == 0 else 2):
            q = min(len(bp) - 1, 2 + j); cb = bp[q]
            dd = nrm(bd[q] + rng.normal(0, .4, 3) + [0, .3, 0])
            side = rot_about(nrm(np.cross(dd, [0, 1, 0]) + [1e-3, 0, 0]), dd, rng.normal(0, .7))
            w = rng.uniform(1.0, 1.5) * (1.25 if lod else 1)
            v = float(rng.uniform(.75, 1.0))
            card(mb, cb - dd * .2, dd, side, w, w * .9, "laurel_a" if variant != 1 else "laurel_b", (v, v, v, 1), card_n)
    return mb, Hs

# ------------------------------------------------------------------ scatter
def log_mesh(variant):
    rng = np.random.default_rng(7100 + variant); mb = MeshB()
    L = rng.uniform(4, 7.5); R = rng.uniform(.18, .32)
    pts = []; d = np.array([1.0, 0, 0]); p = np.array([-L / 2, R * .6, 0])
    for s in range(9):
        pts.append(p.copy()); d = nrm(d + rng.normal(0, .04, 3) * [1, .2, 1]); p = p + d * L / 8
    radii = [R * (1 - .3 * s / 8) * (1 + rng.normal(0, .05)) for s in range(9)]
    tube(mb, pts, radii, 8, lambda t, p: (.55, .53, .5, 1), uv_tile=1.0)
    for k in range(3):   # broken limb stubs
        t = rng.uniform(.2, .9); q = int(t * 8); b = pts[q]
        dd = nrm(np.array([rng.normal(0, .3), rng.uniform(.2, 1), rng.choice([-1, 1])]))
        bp, br, _ = grow(rng, b, dd, rng.uniform(.4, 1.2), R * .3, 0, 2, .1, 0)
        tube(mb, bp, br, 4, lambda t, p: (.6, .6, .58, 1))
    return mb

def stump_mesh(variant):
    rng = np.random.default_rng(7200 + variant); mb = MeshB()
    R = rng.uniform(.25, .4); h = rng.uniform(.35, .8)
    sides = 10; c = (.6, .58, .55, 1)
    rings = []
    for j, y in enumerate((-.1, .08, h * .6, h)):
        r = R * (1.45 if j < 2 else 1.0)
        ring = []
        for k in range(sides + 1):
            a = 2 * math.pi * k / sides
            yy = y + (rng.uniform(-.12, .12) if j == 3 else 0)
            rr = r * (1 + rng.normal(0, .06))
            n = np.array([math.cos(a), .1, math.sin(a)])
            ring.append(mb.vert((rr * math.cos(a), yy, rr * math.sin(a)), n, (k / sides * 2 * math.pi * r / 1.0, y / 1.0), c))
        rings.append(ring)
    for i in range(3):
        for k in range(sides):
            mb.face(rings[i][k], rings[i + 1][k], rings[i + 1][k + 1], 0); mb.face(rings[i][k], rings[i + 1][k + 1], rings[i][k + 1], 0)
    top = mb.vert((0, h - .05, 0), (0, 1, 0), (.5, .5), (.45, .4, .35, 1))
    for k in range(sides):
        mb.face(rings[3][k], top, rings[3][k + 1], 0)
    return mb

def rock_mesh(variant):
    rng = np.random.default_rng(7300 + variant); mb = MeshB()
    bm = bmesh.new(); bmesh.ops.create_icosphere(bm, subdivisions=2, radius=1.0)
    sx, sy, sz = rng.uniform(.5, 1.1), rng.uniform(.3, .6), rng.uniform(.5, 1.0)
    idx = {}
    for v in bm.verts:
        p = np.array([v.co.x, v.co.z, v.co.y])
        k = 1 + .18 * math.sin(3 * p[0] + variant) * math.cos(2.5 * p[2]) + rng.normal(0, .05)
        q = p * k * np.array([sx, sy, sz]) - np.array([0, sy * .35, 0])
        idx[v.index] = mb.vert(q, nrm(p), (q[0] + q[1] * .5, q[2] + q[1] * .5), (.85, .85, .85, 1))
    for f in bm.faces:
        a, b, c = [idx[v.index] for v in f.verts]
        mb.face(a, c, b, 0)
    bm.free()
    return mb

def clump(name, n, w, h, seed, spread=.25, tilt=.25):
    rng = np.random.default_rng(seed); mb = MeshB()
    for k in range(n):
        az = math.pi * k / n + rng.normal(0, .2)
        side = np.array([math.cos(az), 0, math.sin(az)])
        off = rng.normal(0, spread, 3) * [1, 0, 1]
        up = nrm(np.array([0, 1, 0]) + rng.normal(0, tilt, 3) * [1, 0, 1])
        s = rng.uniform(.75, 1.15); v = float(rng.uniform(.8, 1.0))
        card(mb, off, up, side, w * s, h * s, name, (v, v, v, 1), lambda p: nrm(np.array([p[0], 1.5, p[2]])))
    return mb

def branch_litter(variant):
    rng = np.random.default_rng(7400 + variant); mb = MeshB()
    for k in range(5):
        p0 = np.array([rng.normal(0, .8), .03, rng.normal(0, .8)])
        az = rng.uniform(0, 6.3); d = np.array([math.cos(az), .02, math.sin(az)])
        bp, br, bd = grow(rng, p0, d, rng.uniform(.8, 2.2), .035, 0, 3, .15, -.01)
        bp = [np.array([q[0], max(q[1], .025), q[2]]) for q in bp]
        tube(mb, bp, br, 3, lambda t, p: (.6, .58, .55, 1))
    return mb

# ------------------------------------------------------------------ Blender I/O
MATS = {}
def material(name, image=None, alpha=False):
    if name in MATS: return MATS[name]
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; b = nt.nodes["Principled BSDF"]; b.inputs["Roughness"].default_value = .9
    b.inputs["Specular IOR Level"].default_value = 0.0
    vc = nt.nodes.new("ShaderNodeVertexColor"); vc.layer_name = "Col"
    mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'; mix.inputs[0].default_value = 1
    nt.links.new(vc.outputs["Color"], mix.inputs[7]); nt.links.new(mix.outputs[2], b.inputs["Base Color"])
    if image is not None:
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = image
        nt.links.new(t.outputs["Color"], mix.inputs[6])
        if alpha:
            nt.links.new(t.outputs["Alpha"], b.inputs["Alpha"]); m.blend_method = 'CLIP'
    else:
        mix.inputs[6].default_value = (.5, .5, .5, 1)
    MATS[name] = m
    return m

def to_object(name, mb, mats):
    me = bpy.data.meshes.new(name)
    verts = [(p[0], -p[2], p[1]) for p in mb.v]
    me.from_pydata(verts, [], [tuple(t) for t in mb.tri])
    me.update()
    for m in mats: me.materials.append(m)
    for i, poly in enumerate(me.polygons):
        poly.material_index = mb.mat[i]; poly.use_smooth = True
    # Create both layers first: adding an attribute invalidates earlier
    # Python handles to the mesh's layers, so fetch them again by name.
    me.uv_layers.new(name="UVMap"); me.color_attributes.new("Col", 'FLOAT_COLOR', 'CORNER')
    loops = np.zeros(len(me.loops), int); me.loops.foreach_get("vertex_index", loops)
    uvs = np.asarray(mb.uv, np.float32)[loops]; me.uv_layers["UVMap"].data.foreach_set("uv", uvs.ravel())
    cols = np.asarray(mb.col, np.float32)[loops]; me.color_attributes["Col"].data.foreach_set("color", cols.ravel())
    nn = np.asarray(mb.n, np.float32); nb = np.stack([nn[:, 0], -nn[:, 2], nn[:, 1]], 1)
    me.normals_split_custom_set([tuple(nb[i]) for i in loops])
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    return ob

def export_glb(ob, rel):
    path = os.path.join(OUT, rel + ".glb"); os.makedirs(os.path.dirname(path), exist_ok=True)
    for o in bpy.context.scene.objects: o.select_set(o == ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_yup=True,
                              export_apply=True, export_texcoords=True, export_normals=True,
                              export_materials='PLACEHOLDER', export_vertex_color='ACTIVE',
                              export_extras=False, export_cameras=False, export_lights=False, export_animations=False)
    C.canonical_indices(path)
    return path

# ------------------------------------------------------------------ build
def textures():
    t = {}
    atlas = os.path.join(OUT, "foliage_atlas.png")
    t["atlas"] = bpy.data.images.load(atlas)
    os.makedirs(os.path.join(OUT, "textures"), exist_ok=True)
    for k, asset in BARK.items():
        t[k] = C.polyhaven(asset, "diff", "1k")
        C.polyhaven(asset, "nor_gl", "1k")
        if k == "log": continue   # logs and stumps reuse the oak bark maps (hollow_woods.h)
        for kind, suffix in (("diff", "color"), ("nor_gl", "normal")):
            # 1K, re-encoded at JPEG q82 (raw pixels: no view transform).
            img = bpy.data.images.load(os.path.join(C.CACHE, f"{asset}_{kind}_1k.jpg"))
            if kind != "diff": img.colorspace_settings.name = "Non-Color"
            else:   # grey the warm scans toward lichen-grey Appalachian bark
                px = np.array(img.pixels[:], np.float32).reshape(-1, 4)
                lum = px[:, :3] @ np.array([.3, .59, .11], np.float32)
                keep, val = BARK_GRADE.get(k, (.5, 1.0))
                px[:, :3] = (lum[:, None] + (px[:, :3] - lum[:, None]) * keep) * val
                img.pixels.foreach_set(px.ravel())
            img.file_format = 'JPEG'
            img.save(filepath=os.path.join(OUT, "textures", f"bark_{k}_{suffix}.jpg"), quality=82)
            bpy.data.images.remove(img)
    return t

def build_all():
    C.reset()
    tex = textures()
    cardm = material("cards", tex["atlas"], alpha=True)
    meshes = []  # (rel path, bark key, cards, radius, height, tris)
    objs = {}
    def emit(rel, mb, bark, H):
        barkm = material("bark_" + bark, tex[bark])
        ob = to_object(os.path.basename(rel), mb, [barkm, cardm])
        export_glb(ob, rel)
        v = np.asarray(mb.v); r = float(np.max(np.hypot(v[:, 0], v[:, 2]))) if len(v) else 0
        meshes.append((rel, bark, ("b" if any(m == 0 for m in mb.mat) else "") + ("c" if any(m == 1 for m in mb.mat) else ""), round(r, 2), round(float(v[:, 1].max()), 2), mb.count()))
        objs[rel] = ob
        print("MESH", rel, mb.count(), "tris")
        return ob
    heights = {}
    for sp, nv in VARIANTS.items():
        if ONLY and sp not in ONLY: continue
        for v in range(nv):
            for lod in (0, 1):
                if sp in SPECIES: mb, H = deciduous(sp, v, lod)
                elif sp == "hemlock": mb, H = hemlock(v, lod)
                elif sp == "pine": mb, H = pine(v, lod)
                else: mb, H = laurel(v, lod)
                emit(f"trees/{sp}_{v}_lod{lod}", mb, sp, H); heights[f"{sp}_{v}"] = H
    if not ONLY or "scatter" in ONLY:
        for v in range(2): emit(f"scatter/log_{v}", log_mesh(v), "log", 0)
        for v in range(2): emit(f"scatter/stump_{v}", stump_mesh(v), "log", 0)
        for v in range(3): emit(f"scatter/rock_{v}", rock_mesh(v), "rock", 0)
        for v in range(2): emit(f"scatter/limbs_{v}", branch_litter(v), "log", 0)
        emit("scatter/fern_0", clump("fern", 6, .9, .75, 81, .15, .5), "log", 0)
        emit("scatter/goldenrod_0", clump("goldenrod", 4, 1.0, 1.35, 82, .2, .15), "log", 0)
        emit("scatter/grass_0", clump("grass", 4, .9, .7, 83, .15, .2), "log", 0)
    return meshes, objs, heights, tex

# ------------------------------------------------------------------ impostors
IMP_W, IMP_H = 256, 512
def impostor_order():
    out = []
    for sp in ("oak", "poplar", "sycamore", "hemlock", "pine"):
        for v in range(VARIANTS[sp]): out.append(f"{sp}_{v}")
    return out

def bake_impostors(objs, heights):
    """Orthographic renders of lod0 from two sides under a uniform white sky:
    albedo x ambient occlusion with alpha. Packed 8 x 4 cells (2 per tree)."""
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 96; sc.cycles.use_denoising = False
    sc.cycles.max_bounces = 3; sc.cycles.transparent_max_bounces = 128; sc.render.film_transparent = True
    sc.view_settings.view_transform = 'Standard'; sc.render.resolution_x, sc.render.resolution_y = IMP_W, IMP_H
    sc.render.image_settings.file_format = 'PNG'; sc.render.image_settings.color_mode = 'RGBA'
    world = bpy.data.worlds.new("imp"); sc.world = world; world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (1, 1, 1, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    cd = bpy.data.cameras.new("impcam"); cd.type = 'ORTHO'
    cam = bpy.data.objects.new("impcam", cd); sc.collection.objects.link(cam); sc.camera = cam
    atlas = np.zeros((4 * IMP_H, 8 * IMP_W, 4), np.float32)
    order = impostor_order(); meta = {}; extra = []
    for o in sc.objects:
        if o.type == 'MESH': o.hide_render = True
    for i, key in enumerate(order):
        rel = f"trees/{key}_lod0"
        if rel not in objs: continue
        ob = objs[rel]; ob.hide_render = False
        H = max(v.co.z for v in ob.data.vertices) + .3
        cd.ortho_scale = H  # vertical extent H (portrait), horizontal H/2
        for view in range(2):
            ang = view * math.pi / 2
            # engine camera looking at the tree centre from -Z (view 0) or +X (view 1)
            eng = np.array([-math.sin(ang) * 60, H / 2, -math.cos(ang) * 60]) * [1, 1, 1]
            cam.location = (eng[0], -eng[2], eng[1])
            cam.rotation_euler = (Vector((0, 0, H / 2)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
            path = os.path.join(OUT, f"_imp_{key}_{view}.png"); sc.render.filepath = path
            bpy.ops.render.render(write_still=True)
            img = bpy.data.images.load(path); px = np.array(img.pixels[:], np.float32).reshape(IMP_H, IMP_W, 4)[::-1]
            bpy.data.images.remove(img); os.remove(path)
            cell = 2 * i + view; r, c = cell // 8, cell % 8
            atlas[r * IMP_H:(r + 1) * IMP_H, c * IMP_W:(c + 1) * IMP_W] = px
        meta[key] = H
        ob.hide_render = True
    # bleed colour into transparent texels (alpha-tested + mipmapped)
    a = atlas[:, :, 3:4]; rgb = np.where(a > 1e-3, atlas[:, :, :3], 0)   # Blender pixels are straight alpha
    pm = rgb * (a > .3); w = (a > .3).astype(np.float32)   # bleed only from solid texels
    for it in range(8):
        pm2 = sum(np.roll(np.roll(pm, dy, 0), dx, 1) for dy in (-2, 0, 2) for dx in (-2, 0, 2))
        w2 = sum(np.roll(np.roll(w, dy, 0), dx, 1) for dy in (-2, 0, 2) for dx in (-2, 0, 2))
        fill = (w < .5) & (w2 > 0)
        pm = np.where(fill, pm2 / np.maximum(w2, 1e-6), pm); w = np.where(fill, 1.0, w)
    rgb = np.where(w > 0, pm, .25); rgb = np.where(a > .3, atlas[:, :, :3], rgb)
    # Standard view transform wrote sRGB-encoded colour; keep it as sRGB.
    out = np.dstack([np.clip(rgb, 0, 1), np.clip(a, 0, 1)])
    img = bpy.data.images.new("impostor_atlas", 8 * IMP_W, 4 * IMP_H, alpha=True)
    img.pixels.foreach_set(out[::-1].ravel()); img.filepath_raw = os.path.join(OUT, "impostor_atlas.png"); img.file_format = 'PNG'
    img.save()
    imp_img = bpy.data.images.load(os.path.join(OUT, "impostor_atlas.png"))
    # Camera-facing billboard (the engine turns it to the eye each frame), cut
    # to the tree's silhouette in 10 height bands so the alpha-tested area
    # (the cost on a software rasterizer) stays close to the visible tree.
    bands = 10
    for i, key in enumerate(order):
        if key not in meta: continue
        H = meta[key]; mb = MeshB(); cell = 2 * i; r, c = cell // 8, cell % 8
        A = a[r * IMP_H:(r + 1) * IMP_H, c * IMP_W:(c + 1) * IMP_W, 0]      # rows top-down
        ext = []
        for k in range(bands):   # band k from the bottom
            rows = A[IMP_H - (k + 1) * IMP_H // bands: IMP_H - k * IMP_H // bands]
            cols = np.where(rows.max(0) > .25)[0]
            ext.append((cols.min(), cols.max() + 1) if len(cols) else None)
        def span(k):
            e = [x for x in ext[max(0, k - 1):k + 1] if x is not None]
            if not e: return None
            return (max(0, min(x[0] for x in e) - 3), min(IMP_W, max(x[1] for x in e) + 3))
        rows_v = []
        for k in range(bands + 1):
            sp = span(min(k, bands - 1)) if k < bands else (ext[-1] and (max(0, ext[-1][0] - 3), min(IMP_W, ext[-1][1] + 3)))
            if k > 0 and k < bands:
                e2 = [x for x in (ext[k - 1], ext[k]) if x is not None]
                sp = (max(0, min(x[0] for x in e2) - 3), min(IMP_W, max(x[1] for x in e2) + 3)) if e2 else None
            rows_v.append(sp)
        prev = None
        for k in range(bands + 1):
            sp = rows_v[k]
            if sp is None: prev = None; continue
            t = k / bands; y = t * H
            ids = []
            for px in sp:
                x = (px / IMP_W - .5) * H / 2
                u = (c + px / IMP_W) / 8; v = 1 - (r + 1 - t) / 4
                u = min(max(u, c / 8 + .5 / 2048), (c + 1) / 8 - .5 / 2048); v = min(max(v, 1 - (r + 1) / 4 + .5 / 2048), 1 - r / 4 - .5 / 2048)
                n = nrm(np.array([0, .8, 0]) + np.array([0, 0, 1.0]) + np.array([x / H, 0, 0]))
                ids.append(mb.vert((x, y, 0), n, (u, v), (1, 1, 1, 1)))
            if prev is not None:
                mb.face(prev[0], prev[1], ids[1], 1); mb.face(prev[0], ids[1], ids[0], 1)
            prev = ids
        sp = key.split("_")[0]
        ob = to_object(f"{key}_lod2", mb, [material("bark_" + sp), material("impostor", imp_img, alpha=True)])
        export_glb(ob, f"trees/{key}_lod2")
        extra.append((f"trees/{key}_lod2", "impostor", "c", round(H / 4, 2), round(H, 2), mb.count()))
    extra += forest_walls(out, order, meta)
    return meta, extra

# ------------------------------------------------------------------ far woods
WALL_D = (46.0, 70.0)        # ring offsets from the clearing rectangle (m)
WALL_W, WALL_H = 80.0, 32.0  # metres of forest per strip repeat, strip height
def forest_walls(atlas_out, order, meta):
    """The woods beyond ~40 m: two rings around the clearing, each a strip of
    impostor trees composed side by side (forest_wall.png, two 2048 x 1024
    strips). One alpha-tested layer per ring instead of hundreds of
    overlapping billboards. Rings follow the valley walls (hollow_scene.height)."""
    import hollow_scene as HS
    SW, SH = 2048, 1024; ppm = SW / WALL_W
    strips = np.zeros((2 * SH, SW, 4), np.float32)
    for k in range(2):
        rng = np.random.default_rng(900 + k)
        layer = np.zeros((SH, SW, 4), np.float32)
        x = -40.0
        while x < SW + 40:
            key = order[int(rng.integers(len(order)))]; i = order.index(key); view = int(rng.integers(2))
            cell = 2 * i + view; r, c = cell // 8, cell % 8
            src = atlas_out[r * IMP_H:(r + 1) * IMP_H, c * IMP_W:(c + 1) * IMP_W]
            H = meta[key] * rng.uniform(.8, 1.12)
            th = int(H * ppm); tw = int(H / 2 * ppm)
            ys = (np.arange(th) * IMP_H / th).astype(int); xs = (np.arange(tw) * IMP_W / tw).astype(int)
            spr = src[ys][:, xs]
            if rng.random() < .5: spr = spr[:, ::-1]
            y0 = SH - th - int(rng.uniform(0, .04) * SH)
            x0 = int(x - tw / 2)
            for dx in (-SW, 0, SW):   # wrap so the strip tiles
                a0, a1 = max(0, x0 + dx), min(SW, x0 + dx + tw)
                if a1 <= a0: continue
                sp = spr[max(0, -y0):, a0 - (x0 + dx):a1 - (x0 + dx)]
                dst = layer[max(0, y0):max(0, y0) + sp.shape[0], a0:a1]
                al = sp[:, :, 3:4]
                dst[:, :, :3] = sp[:, :, :3] * al + dst[:, :, :3] * (1 - al)
                dst[:, :, 3:4] = al + dst[:, :, 3:4] * (1 - al)
            x += rng.uniform(2.6, 5.2) * ppm
        strips[k * SH:(k + 1) * SH] = layer
    a = strips[:, :, 3:4]; rgb = strips[:, :, :3] / np.maximum(a, 1e-4)
    rgb = np.where(a > 1e-3, rgb, .2)
    img = bpy.data.images.new("forest_wall", SW, 2 * SH, alpha=True)
    img.pixels.foreach_set(np.dstack([np.clip(rgb, 0, 1), a])[::-1].ravel()); img.filepath_raw = os.path.join(OUT, "forest_wall.png")
    img.file_format = 'PNG'; img.save()
    wall_img = bpy.data.images.load(os.path.join(OUT, "forest_wall.png"))
    x0, x1, z0, z1 = HS.RECT; extra = []
    for k, d in enumerate(WALL_D):
        mb = MeshB(); pts = []
        # rounded rectangle at offset d, sampled every ~3 m
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2; hx, hz = (x1 - x0) / 2, (z1 - z0) / 2
        n = int(2 * (2 * hx + 2 * hz + 2 * math.pi * d) / 6)
        for j in range(n + 1):
            t = 2 * math.pi * j / n
            dx, dz = math.cos(t), math.sin(t)
            # support point of the rectangle in direction (dx, dz), pushed out by d
            px = cx + hx * (1 if dx > 0 else -1) * min(1, abs(dx) * 4) + d * dx
            pz = cz + hz * (1 if dz > 0 else -1) * min(1, abs(dz) * 4) + d * dz
            pts.append((px, pz))
        u = 0.0; prev = None; v0 = 1 - (k + 1) / 2; v1 = 1 - k / 2
        for j, (px, pz) in enumerate(pts):
            if j: u += math.hypot(px - pts[j - 1][0], pz - pts[j - 1][1]) / WALL_W
            y = HS.height(px, pz) - .6
            inward = nrm(np.array([cx - px, 0, cz - pz]))
            nn = nrm(inward + np.array([0, .9, 0]))
            ids = [mb.vert((px, y, pz), nn, (u, v0 + .5 / 2048), (1, 1, 1, 1)), mb.vert((px, y + WALL_H, pz), nn, (u, v1 - .5 / 2048), (1, 1, 1, 1))]
            if prev: mb.face(prev[0], ids[0], ids[1], 1); mb.face(prev[0], ids[1], prev[1], 1)
            prev = ids
        ob = to_object(f"wall_{k}", mb, [material("bark_oak"), material("wall", wall_img, alpha=True)])
        export_glb(ob, f"trees/wall_{k}")
        extra.append((f"trees/wall_{k}", "wall", "c", round(max(hx, hz) * 1.5 + d, 1), WALL_H + 40, mb.count()))
    return extra

# ------------------------------------------------------------------ placement
YARD_KEEP_OUT = [(13.0, 23.0, -9.0, 4.2), (31.0, 42.0, -9.2, 4.8), (23.5, 27.5, 2.5, 6.5), (5.5, 9.0, 1.5, 5.5),
                 (15.5, 20.5, 3.4, 12.5), (-1.6, 2.1, -4.6, 6.6), (26.5, 30.5, -12.5, -8.5), (-6.0, 2.0, -22.0, -15.5)]

def placement(heights):
    import hollow_scene as HS
    rng = np.random.default_rng(2026)
    x0, x1, z0, z1 = HS.RECT
    def edge_dist(x, z):
        dx = max(x0 - x, 0, x - x1); dz = max(z0 - z, 0, z - z1); return math.hypot(dx, dz)
    def ground(x, z):
        if x0 <= x <= x1 and z0 <= z <= z1: return HS.BANK_Y * HS.smooth(HS.BANK_Z - .5, HS.BANK_Z + .5, z) - .02
        return HS.height(x, z) - .05
    lines = ["# Generated by tools/blender/hollow_trees.py. Do not edit.",
             "# set NAME LOD0 LOD1 LOD2   (paths under hollow_woods/, '-' = none)",
             "# i SET X Y Z YAW SCALE FLAGS   (flags: 1 casts sun shadow, 2 clearing edge)"]
    sets = []
    def add_set(name, l0, l1, l2):
        sets.append(name); lines.append(f"set {name} {l0} {l1} {l2}"); return len(sets) - 1
    S = {}
    for sp, nv in VARIANTS.items():
        for v in range(nv):
            l2 = f"trees/{sp}_{v}_lod2" if sp != "laurel" else "-"
            S[f"{sp}_{v}"] = add_set(f"{sp}_{v}", f"trees/{sp}_{v}_lod0", f"trees/{sp}_{v}_lod1", l2)
    for k in range(len(WALL_D)):
        S[f"wall_{k}"] = add_set(f"wall_{k}", f"trees/wall_{k}", f"trees/wall_{k}", f"trees/wall_{k}")
    for name in ("log_0", "log_1", "stump_0", "stump_1", "rock_0", "rock_1", "rock_2", "limbs_0", "limbs_1", "fern_0", "goldenrod_0", "grass_0"):
        S[name] = add_set(name, f"scatter/{name}", "-", "-")
    inst = []
    def put(name, x, z, yaw, s, flags=0, y=None):
        yy = ground(x, z) if y is None else y
        inst.append((S[name], x, yy, z, yaw, s, flags))
    # --- trees: Poisson-disc samples in the ring around the clearing.
    pts = []
    cell = 3.2; grid = {}
    def ok(x, z, r):
        gx, gz = int(math.floor(x / cell)), int(math.floor(z / cell))
        for i in range(gx - 3, gx + 4):
            for j in range(gz - 3, gz + 4):
                for (px, pz, pr) in grid.get((i, j), ()):
                    if math.hypot(px - x, pz - z) < max(r, pr): return False
        return True
    tries = 0
    while tries < 60000:
        tries += 1
        x = rng.uniform(-115, 170); z = rng.uniform(-125, 160)
        d = edge_dist(x, z); inside = d == 0
        if inside:
            # wooded bank top behind the cellar, kept off the cellar mound
            if not (z > HS.BANK_Z + 2.5 and not (13 < x < 23 and z < 21)): continue
        elif d < 1.6: continue
        if d > 40: continue          # beyond: the forest walls
        if HS.seg_dist(x, z, HS.LANE) < 4.2 + .02 * d: continue
        if x < x0 and abs(x - HS.CREEK_X) < 2.2: continue
        r = 4.2 + .05 * d + (.0006 * d * d)
        if not ok(x, z, r): continue
        grid.setdefault((int(math.floor(x / cell)), int(math.floor(z / cell))), []).append((x, z, r))
        pts.append((x, z, d))
    print("trees", len(pts))
    for (x, z, d) in pts:
        creek = x < x0 + 2 and abs(x - HS.CREEK_X) < 14
        north = z > z1 or (d == 0)
        u = rng.random()
        if creek: sp = "sycamore" if u < .38 else "hemlock" if u < .72 else "poplar" if u < .88 else "oak"
        elif north: sp = "oak" if u < .42 else "poplar" if u < .62 else "hemlock" if u < .78 else "pine"
        else: sp = "oak" if u < .36 else "poplar" if u < .58 else "hemlock" if u < .76 else "pine" if u < .92 else "sycamore"
        v = int(rng.integers(VARIANTS[sp]))
        s = rng.uniform(.82, 1.15) * (1 - .12 * HS.smooth(2, 12, -d + 12) if d < 6 else 1)
        flags = (1 if d < 9 else 0) | (2 if d < 6 else 0)
        put(f"{sp}_{v}", x, z, rng.uniform(0, 2 * math.pi), s, flags)
    for k in range(len(WALL_D)):
        inst.append((S[f"wall_{k}"], 0.0, 0.0, 0.0, 0.0, 1.0, 0))
    # --- laurel / rhododendron thicket along the edge, thick in the coves.
    n_l = 0
    for k in range(5000):
        x = rng.uniform(x0 - 30, x1 + 30); z = rng.uniform(z0 - 30, z1 + 30); d = edge_dist(x, z)
        if d < .8 or d > 26: continue
        if d == 0: continue
        if HS.seg_dist(x, z, HS.LANE) < 3.3: continue
        p = .7 * math.exp(-(d - 2.5) ** 2 / 18) + .15 * math.exp(-d / 12)
        if rng.random() > p: continue
        put(f"laurel_{int(rng.integers(3))}", x, z, rng.uniform(0, 6.3), rng.uniform(.75, 1.3), 2 if d < 4 else 0); n_l += 1
    print("laurel", n_l)
    # --- forest floor scatter (within 34 m of the clearing).
    counts = {}
    def scatter(name, n, dmin, dmax, smin, smax, yoff=0.0, inside_edge=0.0, lane=2.5):
        c = 0
        for k in range(n * 8):
            if c >= n: break
            x = rng.uniform(x0 - dmax, x1 + dmax); z = rng.uniform(z0 - dmax, z1 + dmax); d = edge_dist(x, z)
            if d == 0:
                # a strip just inside the yard edge (dead stems, grass)
                e = min(x - x0, x1 - x, z - z0, HS.BANK_Z - z) if z < HS.BANK_Z else 99
                if not (inside_edge > 0 and 0 <= e < inside_edge): continue
                # keep clear of buildings, the cellar approach, the bus, the dog pen and the berm
                if any(a0 <= x <= a1 and b0 <= z <= b1 for (a0, a1, b0, b1) in YARD_KEEP_OUT): continue
            elif not (dmin <= d <= dmax): continue
            if HS.seg_dist(x, z, HS.LANE) < lane: continue
            put(name, x, z, rng.uniform(0, 6.3), rng.uniform(smin, smax), 0)
            if yoff: inst[-1] = inst[-1][:2] + (inst[-1][2] + yoff,) + inst[-1][3:]
            c += 1
        counts[name] = c
    scatter("log_0", 30, 2, 34, .8, 1.2); scatter("log_1", 30, 2, 34, .8, 1.2)
    scatter("stump_0", 20, 1, 30, .8, 1.2); scatter("stump_1", 20, 1, 30, .8, 1.2)
    scatter("rock_0", 40, .5, 34, .5, 1.3); scatter("rock_1", 40, .5, 34, .5, 1.3); scatter("rock_2", 30, .5, 34, .6, 1.6)
    scatter("limbs_0", 70, .5, 30, .8, 1.3); scatter("limbs_1", 70, .5, 30, .8, 1.3)
    scatter("fern_0", 220, .5, 24, .7, 1.3)
    # small field stones half-sunk along the inside of the yard edge (visual only, <= 0.4 m)
    for k, nm in enumerate(("rock_0", "rock_1", "rock_2")):
        scatter(nm, 18, 0, 0, .22, .4, yoff=-.06, inside_edge=6.0, lane=2.6)
    scatter("goldenrod_0", 170, 0, 5, .8, 1.3, inside_edge=3.0, lane=2.8)
    scatter("grass_0", 380, 0, 6, .7, 1.3, inside_edge=4.5, lane=2.4)
    print("scatter", counts)
    for (s, x, y, z, yaw, sc, fl) in inst:
        lines.append(f"i {s} {x:.2f} {y:.2f} {z:.2f} {yaw:.3f} {sc:.3f} {fl}")
    open(os.path.join(OUT, "woods_placement.txt"), "w").write("\n".join(lines) + "\n")
    print("instances", len(inst))

def lineup(objs):
    """Hill-climb preview: the lod (VIEW, default lod0) of every built tree in a
    row, seen from eye height across a foggy clearing at dusk-grey light."""
    sc = bpy.context.scene; lod = VIEW or "lod0"
    keys = [k for k in objs if k.endswith(lod)]
    for o in sc.objects:
        if o.type == 'MESH': o.hide_render = o.name + "" not in [os.path.basename(k) for k in keys]
    rs = np.random.default_rng(5)
    for i, k in enumerate(keys):
        objs[k].location = ((i - (len(keys) - 1) / 2) * 9, -18, 0)
    # a stand: linked duplicates scattered 18-90 m from the camera
    for n in range(46):
        k = keys[n % len(keys)]
        d = bpy.data.objects.new(f"dup{n}", objs[k].data); sc.collection.objects.link(d)
        d.location = (rs.uniform(-60, 60), -rs.uniform(-10, 70), 0); d.rotation_euler = (0, 0, rs.uniform(0, 6.3))
        d.scale = (rs.uniform(.85, 1.15),) * 3
    gb = MeshB(); ids = [gb.vert(p, (0, 1, 0), (0, 0), (.3, .26, .2, 1)) for p in ((-200, 0, -200), (200, 0, -200), (200, 0, 200), (-200, 0, 200))]
    gb.face(ids[0], ids[2], ids[1], 0); gb.face(ids[0], ids[3], ids[2], 0)
    gm = material("_soil"); gm.node_tree.nodes["Mix"].inputs[6].default_value = (.08, .06, .045, 1)
    to_object("_groundq", gb, [gm])
    world = bpy.data.worlds.new("prev"); sc.world = world; world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (.85, .86, .88, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    sun = bpy.data.lights.new("sun", 'SUN'); sun.energy = .6; sun.angle = .3
    so = bpy.data.objects.new("sun", sun); sc.collection.objects.link(so); so.rotation_euler = (math.radians(70), 0, math.radians(-60))
    # Fog: a volume scatter box over the scene.
    vb = MeshB(); P = [(-150, -1, -60), (150, -1, -60), (150, -1, 160), (-150, -1, 160), (-150, 60, -60), (150, 60, -60), (150, 60, 160), (-150, 60, 160)]
    ids = [vb.vert(p, (0, 1, 0), (0, 0), (1, 1, 1, 1)) for p in P]
    for (a, b, c, d) in ((0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
        vb.face(ids[a], ids[b], ids[c], 0); vb.face(ids[a], ids[c], ids[d], 0)
    fm = bpy.data.materials.new("fog"); fm.use_nodes = True; nt = fm.node_tree
    nt.nodes.remove(nt.nodes["Principled BSDF"]); vol = nt.nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Density"].default_value = float(os.environ.get("FOG", ".018")); vol.inputs["Color"].default_value = (.8, .8, .82, 1)
    nt.links.new(vol.outputs[0], nt.nodes["Material Output"].inputs["Volume"])
    to_object("_fog", vb, [fm])
    cd = bpy.data.cameras.new("cam"); cd.lens_unit = 'FOV'; cd.angle = math.radians(60)
    cam = bpy.data.objects.new("cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    dist = float(os.environ.get("DIST", "34"))
    cam.location = (0, dist, 1.7)  # engine z = -dist
    cam.rotation_euler = (Vector((0, 0, 9)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 32; sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 3; sc.cycles.transparent_max_bounces = 128; sc.render.film_transparent = False
    sc.render.resolution_x, sc.render.resolution_y = 960, 540
    sc.view_settings.view_transform = 'AgX'; sc.view_settings.exposure = float(os.environ.get('EXPO', '.8')); sc.render.filepath = C.RENDER
    sc.render.image_settings.file_format = 'JPEG'
    bpy.ops.render.render(write_still=True)

def main():
    meshes, objs, heights, tex = build_all()
    if C.RENDER:
        lineup(objs); return
    if IMPOSTORS and not ONLY:
        meshes += bake_impostors(objs, heights)[1]
    elif not ONLY:   # keep the previously baked impostors and walls listed
        prev = os.path.join(OUT, "woods_meshes.txt")
        for line in (open(prev).read().splitlines() if os.path.exists(prev) else []):
            f = line.split()
            if f and f[0] == "mesh" and f[2] in ("impostor", "wall"):
                meshes.append((f[1], f[2], f[3], float(f[4]), float(f[5]), int(f[6])))
    if not ONLY:
        with open(os.path.join(OUT, "woods_meshes.txt"), "w") as f:
            f.write("# Generated by tools/blender/hollow_trees.py. Do not edit.\n# mesh PATH BARK PARTS RADIUS HEIGHT TRIS   (PARTS: b = bark primitive, c = card primitive, in that order)\n")
            for (rel, bark, parts, r, h, tris) in meshes:
                f.write(f"mesh {rel} {bark} {parts} {r} {h} {tris}\n")
    if PLACEMENT and not ONLY:
        placement(heights)

main()
