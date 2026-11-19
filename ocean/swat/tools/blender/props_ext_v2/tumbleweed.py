# BC2_TUMBLE01: dry tumbleweed (Russian thistle habit): a flattened ball of
# branching pale twigs. Reference: Wikimedia Commons "Tumbleweed 038.jpg" by
# ImperfectTommy / Edmond Meinfelder (CC BY 2.0), see LOG.md. Origin at the
# ground contact.
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(radius=0.5, squash=0.82, mains=72, subs=3, seg=6, twig=0.0055, col=[0.76, 0.68, 0.52], seed=9, wobble=0.16, el_top=1.57)

def build(p):
    rnd = random.Random(p["seed"])
    grain = image("dirty_concrete", 256, gray=True, mean=0.92, gain=0.3)
    m = material("twig", grain, rough=0.9)
    R, sq = p["radius"], p["squash"]
    centre = Vector((0, 0, R * sq))
    base = Vector((0, 0, 0.03))
    bm = bmesh.new()
    def twig(pts, r):
        rings = []
        for k, q in enumerate(pts):
            d = (pts[min(k + 1, len(pts) - 1)] - pts[max(k - 1, 0)]).normalized()
            up = Vector((0, 0, 1)) if abs(d.z) < 0.9 else Vector((1, 0, 0))
            a = d.cross(up).normalized(); b = d.cross(a).normalized()
            rr = r * (1 - 0.6 * k / max(1, len(pts) - 1))
            rings.append([bm.verts.new(q + rr * (math.cos(2 * math.pi * i / 3) * a + math.sin(2 * math.pi * i / 3) * b)) for i in range(3)])
        for r0, r1 in zip(rings, rings[1:]):
            for i in range(3):
                j = (i + 1) % 3; bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
    def surf(dirv, t):
        """point on the squashed sphere along dirv at fraction t of the arc from the base"""
        d = dirv.normalized()
        return centre + Vector((d.x * R, d.y * R, d.z * R * sq))
    for i in range(p["mains"]):
        az = rnd.uniform(0, 2 * math.pi); el = rnd.uniform(-0.6, p["el_top"])
        target = Vector((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)))
        start = Vector((0, 0, -1))
        pts = [base]
        for k in range(1, p["seg"] + 1):
            t = k / p["seg"]
            d = start.lerp(target, t).normalized() if (start.lerp(target, t)).length > 1e-3 else target
            q = surf(d, t) * (0.55 + 0.45 * math.sin(t * math.pi / 2)) + centre * (0.45 - 0.45 * math.sin(t * math.pi / 2))
            q += Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1))) * p["wobble"] * R * 0.3
            pts.append(q)
        twig(pts, p["twig"] * 1.4)
        for s in range(p["subs"]):
            k0 = rnd.randint(2, p["seg"] - 2); q0 = pts[k0]
            az2 = az + rnd.uniform(-1.2, 1.2); el2 = el + rnd.uniform(-0.8, 0.8)
            d2 = Vector((math.cos(el2) * math.cos(az2), math.cos(el2) * math.sin(az2), math.sin(el2)))
            sp = [q0]
            for k in range(1, 5):
                t = k / 4
                q = q0.lerp(surf(d2, 1), t) + Vector((rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1))) * p["wobble"] * R * 0.2
                sp.append(q)
            twig(sp, p["twig"])
    bm.normal_update()
    ob = new_obj("tumbleweed", bm, m, p["col"]); box_uv(ob, 0.3)
    paint(ob, p["col"], noise=0.15, seed=2, grad=(0.0, 0.5, 0.85))

def view(p):
    return dict(cam_loc=(0.15, -1.45, 1.5), target=(0, 0, 0.36), focal=30, sun=(55, 30), res=(960, 720), ground=(0.48, 0.40, 0.28))

finish("BC2_TUMBLE01", build, D, view)
