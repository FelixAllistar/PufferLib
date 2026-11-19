# BC2_TYRES01: dumped car tyres in leaning stacks with dry grass growing
# through, for the service yard by the dumpster. Reference: Wikimedia
# Commons "Old tires stacked near a fence with grass growing on top.jpg" by
# Shixart1985 (CC BY 2.0), see LOG.md. Origin at the pile centre.
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(blades=34, blade_w=0.006, tilt=1.4, tread=1, ro=0.32, ri=0.20, w=0.20, stacks=[[-0.62, 0.05, 6], [0.0, -0.05, 8], [0.64, 0.06, 7], [-1.20, 0.25, 5], [-1.75, 0.45, 4]],
         segs=14, rubber=[0.10, 0.10, 0.10], grass=[0.70, 0.64, 0.46], tufts=7, seed=21, lean=8.0)

def tyre_bm(bm, p, m):
    ro, ri, w, n = p["ro"], p["ri"], p["w"], p["segs"]
    prof = [(ri, -w * 0.42), (ri + 0.012, -w * 0.5), (ro - 0.018, -w * 0.5), (ro, -w * 0.40), (ro, w * 0.40), (ro - 0.018, w * 0.5), (ri + 0.012, w * 0.5), (ri, w * 0.42)]
    rings = []
    for (r, z) in prof:
        rings.append([bm.verts.new(m @ Vector((r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n), z))) for i in range(n)])
    rings.append(rings[0])
    for a, b in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))

def build(p):
    rnd = random.Random(p["seed"])
    grain = image("dirty_concrete", 512, gray=True, mean=p.get("gmean", 0.8), gain=0.6)
    rub = material("rubber", grain, rough=0.9)
    grass = material("grass", grain, rough=0.95)
    bm = bmesh.new()
    tops = []
    for sx, sy, count in p["stacks"]:
        lean = math.radians(rnd.uniform(-p["lean"], p["lean"]))
        for k in range(count):
            z = p["w"] * (k + 0.5)
            off = Vector((sx + rnd.uniform(-0.04, 0.04) + math.sin(lean) * z, sy + rnd.uniform(-0.04, 0.04), z * math.cos(lean)))
            m = Matrix.Translation(off) @ Matrix.Rotation(lean + math.radians(rnd.uniform(-3, 3)), 4, "Y") @ Matrix.Rotation(rnd.uniform(0, 6.28), 4, "Z") @ Matrix.Diagonal((1 + rnd.uniform(-0.05, 0.05), 1 + rnd.uniform(-0.05, 0.05), 1, 1))
            tyre_bm(bm, p, m)
        tops.append(Vector((sx + math.sin(lean) * p["w"] * count, sy, p["w"] * count)))
    # loose tyres: one leaning upright in front, two flat on the ground
    tyre_bm(bm, p, Matrix.Translation((0.55, -0.85, p["ro"] * 0.96)) @ Matrix.Rotation(math.radians(80), 4, "X") @ Matrix.Rotation(math.radians(15), 4, "Y"))
    tyre_bm(bm, p, Matrix.Translation((-1.0, -0.75, p["w"] / 2)) @ Matrix.Rotation(math.radians(6), 4, "X"))
    tyre_bm(bm, p, Matrix.Translation((1.35, 0.35, p["w"] / 2)))
    bm.normal_update()
    ob = new_obj("tyres", bm, rub, p["rubber"], smooth=True); box_uv(ob, 0.5)
    paint(ob, p["rubber"], noise=0.15, seed=3)
    if p.get("tread", 0):
        # tread blocks: darker stripes on the running surface, from the vertex index
        ca = ob.data.color_attributes["Col"]
        for poly in ob.data.polygons:
            if abs(poly.normal.z) > 0.6: continue
            f = 0.65 if poly.index % 2 else 1.0
            for li in poly.loop_indices:
                c = ca.data[li].color; ca.data[li].color = (c[0] * f, c[1] * f, c[2] * f, 1)
    # dry grass tufts: thin blades, two-sided
    g = bmesh.new()
    spots = tops[:3] + [Vector((rnd.uniform(-1.6, 1.6), rnd.uniform(-0.9, 0.3), 0)) for _ in range(p["tufts"])]
    for c in spots:
        for b in range(p.get("blades", 14)):
            a = rnd.uniform(0, 6.28); L = rnd.uniform(0.3, 0.7); tilt = rnd.uniform(0.3, p.get("tilt", 1.1))
            base = c + Vector((rnd.uniform(-0.12, 0.12), rnd.uniform(-0.12, 0.12), 0))
            tip = base + Vector((math.cos(a) * math.sin(tilt) * L, math.sin(a) * math.sin(tilt) * L, math.cos(tilt) * L))
            side = Vector((-math.sin(a), math.cos(a), 0)) * p.get("blade_w", 0.012)
            for f in ((base - side, base + side, tip), (tip, base + side, base - side)):
                g.faces.new([g.verts.new(v) for v in f])
    gr = new_obj("grass", g, grass, p["grass"]); box_uv(gr, 0.5)
    paint(gr, p["grass"], noise=0.2, seed=4)

def view(p):
    return dict(cam_loc=(0.2, -2.2, 1.45), target=(-0.1, 0.1, 0.8), focal=16, sun=(35, 240), res=(960, 640), ground=(0.25, 0.30, 0.18))

finish("BC2_TYRES01", build, D, view)
