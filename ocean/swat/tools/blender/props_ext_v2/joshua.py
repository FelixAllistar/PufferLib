# BC2_JOSHUA01: Joshua tree (Yucca brevifolia habit): shaggy trunk forking
# into angular arms that end in spiky green leaf rosettes, with dead leaves
# hanging under them. Reference: Wikimedia Commons "Joshua Tree (Yucca
# brevifolia) (51632975337).jpg" by Joshua Tree National Park (public
# domain), see LOG.md. Origin at the trunk base.
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(taper=0.85, trunk_h=3.6, trunk_r=0.26, levels=4, fork_len=(2.4, 2.0, 1.5, 1.1), spread=45.0, rosette=0.75, leaves=110, leaf_w=0.034,
         bark=[0.40, 0.34, 0.28], leaf=[0.56, 0.60, 0.28], dead=[0.46, 0.40, 0.31], seed=17, lean=6.0)

F = (0.05, 0.0, 3.5, 0.21)
L = (-1.25, 0.15, 5.15, 0.13)
R = (0.95, 0.05, 5.7, 0.13)
SKELETON = [
    [(0, 0, 0, 0.30), (0.18, 0, 1.6, 0.25), (0.12, 0, 2.6, 0.23), F],
    [F, (-0.55, 0.1, 4.5, 0.16), L],
    [L, (-1.9, 0.25, 5.15, 0.10), (-2.4, 0.3, 5.0, 0.08)],
    [L, (-1.55, -0.2, 6.0, 0.10), (-1.85, -0.3, 6.6, 0.08)],
    [L, (-1.0, 0.4, 6.2, 0.10), (-0.85, 0.5, 7.1, 0.08)],
    [F, (-0.25, -0.35, 4.6, 0.12), (-0.5, -0.7, 5.3, 0.09), (-0.95, -0.9, 5.6, 0.08)],
    [F, (0.55, 0.0, 4.7, 0.16), R],
    [R, (0.45, 0.2, 6.6, 0.10), (0.55, 0.1, 7.4, 0.08)],
    [R, (1.35, -0.1, 6.5, 0.10), (1.75, 0.0, 7.1, 0.08)],
    [R, (1.65, 0.3, 5.9, 0.09), (2.15, 0.35, 5.95, 0.08)],
    [(0.55, 0.0, 4.7, 0.16), (0.95, -0.5, 5.0, 0.09), (1.2, -0.75, 5.5, 0.08)],
]
MID_ROSETTES = [(-0.55, 0.1, 4.5, -0.6, 0.4, -0.2), (0.6, 0.0, 4.75, 0.7, -0.3, -0.1)]

def build(p):
    rnd = random.Random(p["seed"])
    bark_t = image("palm_tree_bark", 512)
    grain = image("dirty_concrete", 256, gray=True, mean=0.9, gain=0.3)
    bark = material("bark", bark_t, rough=0.92)
    leafm = material("leaf", grain, rough=0.8)
    deadm = material("deadleaf", grain, rough=0.95)
    bm_b = bmesh.new(); bm_l = bmesh.new(); bm_d = bmesh.new()
    def limb(a, b, r0, r1, n=7):
        d = (b - a).normalized(); up = Vector((0, 0, 1)) if abs(d.z) < 0.95 else Vector((1, 0, 0))
        x = d.cross(up).normalized(); y = d.cross(x).normalized()
        rings = []
        for t, r in ((0, r0), (0.5, (r0 + r1) / 2), (1, r1)):
            c = a.lerp(b, t)
            rings.append([bm_b.verts.new(c + r * (math.cos(2 * math.pi * i / n) * x + math.sin(2 * math.pi * i / n) * y)) for i in range(n)])
        for r0_, r1_ in zip(rings, rings[1:]):
            for i in range(n):
                j = (i + 1) % n; bm_b.faces.new((r0_[i], r0_[j], r1_[j], r1_[i]))
    def spike(bm, base, d, L, w):
        d = d.normalized(); up = Vector((0, 0, 1)) if abs(d.z) < 0.95 else Vector((1, 0, 0))
        s = d.cross(up).normalized() * w
        tri = (base - s, base + s, base + d * L)
        for f in (tri, tuple(reversed(tri))):
            bm.faces.new([bm.verts.new(v) for v in f])
    def rosette(c, d, size):
        for k in range(p["leaves"]):
            # leaves fan out of the branch tip, mostly forward/outward
            u = rnd.uniform(-1, 1); az = rnd.uniform(0, 6.283)
            v = Vector((math.sqrt(1 - u * u) * math.cos(az), math.sqrt(1 - u * u) * math.sin(az), u))
            if v.dot(d) < -0.3: v = -v
            spike(bm_l, c + v * 0.04, (v + d * 0.6), size * rnd.uniform(0.75, 1.1), p.get("leaf_w", 0.022))
        for k in range(p["leaves"] // 2):
            az = rnd.uniform(0, 6.283)
            v = Vector((math.cos(az) * 0.45, math.sin(az) * 0.45, -1))
            spike(bm_d, c - d * 0.12 + Vector((math.cos(az), math.sin(az), 0)) * 0.08, v, size * rnd.uniform(0.6, 1.0), 0.02)
    def grow(a, d, level, r):
        L = p["fork_len"][min(level, len(p["fork_len"]) - 1)] * rnd.uniform(0.8, 1.2)
        # angular arms: a kink half way
        mid = a + d * (L * 0.5)
        d2 = (d + Vector((rnd.uniform(-0.25, 0.25), rnd.uniform(-0.25, 0.25), 0.18))).normalized()
        b = mid + d2 * (L * 0.5)
        limb(a, mid, r, r * 0.88); limb(mid, b, r * 0.88, r * 0.78)
        if level >= p["levels"]:
            rosette(b, d2, p["rosette"]); return
        n = 2 if rnd.random() < 0.75 else 3
        for k in range(n):
            az = rnd.uniform(0, 6.283) if k == 0 else az + 2 * math.pi / n + rnd.uniform(-0.4, 0.4)
            sp = math.radians(p["spread"] + rnd.uniform(-10, 15))
            h = Vector((math.cos(az), math.sin(az), 0))
            ph = Vector((d2.x, d2.y, 0))
            if level > 1 and ph.length > 0.05:
                h = (ph.normalized() * p.get("outward", 0.0) + h).normalized()
            nd = (d2 * math.cos(sp) + h * math.sin(sp)).normalized()
            if nd.z < 0.15: nd.z = 0.15; nd.normalize()
            grow(b, nd, level + 1, r * p.get("taper", 0.72))
        if level >= 1 and rnd.random() < 0.5:
            rosette(b + d2 * 0.05, d2, p["rosette"] * 0.8)
    if p.get("skeleton", 1):
        # Authored skeleton after the reference tree: polylines of
        # (x, y, z, radius); the last point of each carries a rosette.
        V = lambda t: Vector(t[:3])
        for line in SKELETON:
            for q0, q1 in zip(line, line[1:]):
                limb(V(q0), V(q1), q0[3], q1[3])
            tip, prev = V(line[-1]), V(line[-2])
            rosette(tip, (tip - prev).normalized(), p["rosette"] * rnd.uniform(0.85, 1.1))
        for q in MID_ROSETTES:
            rosette(V(q), V(q[3:]).normalized(), p["rosette"] * 0.8)
    else:
        lean = math.radians(p["lean"])
        top = Vector((math.sin(lean) * p["trunk_h"], 0, p["trunk_h"]))
        limb(Vector((0, 0, 0)), top * 0.5, p["trunk_r"] * 1.25, p["trunk_r"]); limb(top * 0.5, top, p["trunk_r"], p["trunk_r"] * 0.85)
        grow(top, Vector((math.sin(lean), 0, math.cos(lean))), 1, p["trunk_r"] * 0.8)
    for bm in (bm_b, bm_l, bm_d): bm.normal_update()
    b = new_obj("bark", bm_b, bark, p["bark"], smooth=True); box_uv(b, 0.8)
    paint(b, p["bark"], noise=0.1, seed=2)
    l = new_obj("leaves", bm_l, leafm, p["leaf"]); box_uv(l, 0.4); paint(l, p["leaf"], noise=0.15, seed=3)
    dd = new_obj("dead", bm_d, deadm, p["dead"]); box_uv(dd, 0.4); paint(dd, p["dead"], noise=0.15, seed=4)

def view(p):
    return dict(cam_loc=(0.6, -8.6, 1.3), target=(0.0, 0, 4.5), focal=22, sun=(30, 200), res=(960, 720), ground=(0.55, 0.48, 0.36), sky=(0.35, 0.55, 0.95))

finish("BC2_JOSHUA01", build, D, view)
