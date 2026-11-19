# BC2_PALM01: desert fan palm (Washingtonia filifera habit): fibrous trunk,
# long skirt of dead fronds, crown of grey-green fan leaves. Reference:
# Wikimedia Commons "Washingtonia filifera- oasis.jpg" by Thayne Tuason
# (CC BY 4.0), see LOG.md. Origin at the trunk base.
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(height=11.0, trunk_r=(0.44, 0.34), skirt_len=4.6, skirt_r=(1.35, 0.6), leaves=40, leaf_r=2.3, crown_up=0.5, el_span=135,
         petiole=1.6, leaf_col=[0.42, 0.49, 0.36], dead_col=[0.52, 0.45, 0.34], skirt_col=[0.40, 0.35, 0.29], hem=1.2, ridge=0.18,
         trunk_col=[0.42, 0.33, 0.26], droop=0.55, seed=7)

def fan(bm, base, d, up, r, folds=16, spread=2.4, jag=0.25, rnd=None, droop=0.0, inner=0.35):
    """Palmate leaf: solid palm (inner part) and separate pointed segments
    radiating to the tips, with gaps. Both sides (engine culls back faces)."""
    side = d.cross(up).normalized(); nrm = side.cross(d).normalized()
    def P(a, rr, k):
        dirk = (d * math.cos(a) + side * math.sin(a)).normalized()
        t = rr / r
        sag = droop * t * t
        return base + dirk * rr + nrm * (0.04 * r * (1 if k % 2 else -1) * (1 - t)) + Vector((0, 0, -sag * r)) + nrm * (-sag * 0.3 * r)
    step = spread / folds
    tris = []
    for k in range(folds):
        a0 = -spread / 2 + step * k; a1 = a0 + step; am = (a0 + a1) / 2
        tip = r * (1 - jag * (rnd.random() if rnd else 0.5))
        # inner web
        tris.append((P(a0, 0.02, k), P(a0, r * inner, k), P(a1, r * inner, k)))
        tris.append((P(a0, 0.02, k), P(a1, r * inner, k), P(a1, 0.02, k)))
        # segment: narrows from the web to a pointed tip, split along the fold
        w0, w1 = a0 + step * 0.12, a1 - step * 0.12
        tris.append((P(w0, r * inner, k), P(am, tip, k), P(am, r * (inner + 0.05), k)))
        tris.append((P(am, r * (inner + 0.05), k), P(am, tip, k), P(w1, r * inner, k)))
    for t in tris:
        for front in (True, False):
            vs = [bm.verts.new(v) for v in (t if front else reversed(t))]
            bm.faces.new(vs)

def build(p):
    rnd = random.Random(p["seed"])
    bark = image("palm_tree_bark", 512)
    grain = image("dirty_concrete", 512, gray=True, mean=0.88, gain=0.3)
    trunk_m = material("bark", bark, rough=0.9)
    leaf_m = material("leaf", grain, rough=0.8)
    skirt_m = material("skirt", grain, rough=0.95)
    H = p["height"]; r0, r1 = p["trunk_r"]
    prof = [(r0 * 1.25, 0.0), (r0, 0.4)] + [(r0 + (r1 - r0) * t, 0.4 + (H - 0.4) * t) for t in (0.25, 0.5, 0.75, 1.0)]
    trunk = lathe("trunk", prof, 14, trunk_m, p["trunk_col"]); cyl_uv(trunk, 2, 1.2)
    paint(trunk, p["trunk_col"], noise=0.08, seed=3, grad=(0, 2.0, 0.8))
    # skirt: hanging dead fronds, ridged bell with ragged hem
    top = H + 0.3; bot = top - p["skirt_len"]; sr0, sr1 = p["skirt_r"]
    segs = 40; rows = 6
    bm = bmesh.new(); grid = []
    hem = [rnd.uniform(-p.get("hem", 0.6), 0.25) for _ in range(segs)]
    for j in range(rows + 1):
        t = j / rows; ring = []
        for i in range(segs):
            a = 2 * math.pi * i / segs
            ridge = 1 + p.get("ridge", 0.10) * math.sin(i * 2.7) + 0.08 * rnd.random() * (1 + 2 * t)
            r = (sr0 + (sr1 - sr0) * (t ** 0.8)) * ridge
            z = top - p["skirt_len"] * t + (hem[i] if j == rows else 0)
            ring.append(bm.verts.new((r * math.cos(a), r * math.sin(a), z)))
        grid.append(ring)
    for j in range(rows):
        for i in range(segs):
            k = (i + 1) % segs
            bm.faces.new((grid[j][i], grid[j + 1][i], grid[j + 1][k], grid[j][k]))
    bm.faces.new(list(reversed(grid[0])))
    bm.normal_update()
    skirt = new_obj("skirt", bm, skirt_m, p["skirt_col"], smooth=True); cyl_uv(skirt, 3, 1.5)
    # per-column streaks of straw/grey
    me = skirt.data; ca = me.color_attributes["Col"]
    for poly in me.polygons:
        for li in poly.loop_indices:
            v = me.vertices[me.loops[li].vertex_index].co
            a = math.atan2(v.y, v.x); f = 0.82 + 0.25 * (0.5 + 0.5 * math.sin(a * 37.0)) * (0.5 + 0.5 * math.sin(a * 11.0 + 1.3))
            f *= 0.75 + 0.25 * min(max((v.z - bot) / p["skirt_len"], 0), 1)
            c = [x ** 2.2 * f for x in p["skirt_col"]]
            ca.data[li].color = (min(c[0], 1), min(c[1], 1), min(c[2], 1), 1)
    # crown: petioles + fans, dead hanging fans at the skirt top
    bm = bmesh.new(); bmd = bmesh.new()
    crown = Vector((0, 0, top + p.get("crown_up", 0.1)))
    n = p["leaves"]
    for i in range(n):
        az = 2 * math.pi * (i * 0.618034 % 1.0)
        el = math.radians(80 - p.get("el_span", 105) * (i / n) + rnd.uniform(-8, 8))
        d = Vector((math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)))
        base = crown + d * p["petiole"]
        leaf_d = (d + Vector((0, 0, -0.25 * (i / n)))).normalized()
        up = Vector((0, 0, 1)) if abs(leaf_d.z) < 0.95 else Vector((1, 0, 0))
        fan(bm, base, leaf_d, up, p["leaf_r"] * rnd.uniform(0.85, 1.1), rnd=rnd, droop=p["droop"] * (0.4 + i / n))
        tube("pet%d" % i, [crown, crown + d * p["petiole"] * 0.5 + Vector((0, 0, 0.05)), base], 0.025, 4, leaf_m, p["leaf_col"])
    for i in range(10):
        az = 2 * math.pi * i / 10 + 0.3
        d = Vector((math.cos(az) * 0.35, math.sin(az) * 0.35, -0.94)).normalized()
        base = Vector((math.cos(az) * sr0 * 0.95, math.sin(az) * sr0 * 0.95, top - 0.2))
        fan(bmd, base, d, Vector((0, 0, 1)) if abs(d.z) < 0.95 else Vector((1, 0, 0)), 0.8, spread=1.6, rnd=rnd, droop=0.2)
    leaves = new_obj("leaves", bm, leaf_m, p["leaf_col"]); box_uv(leaves, 0.6)
    paint(leaves, p["leaf_col"], noise=0.12, seed=5)
    dead = new_obj("deadfans", bmd, skirt_m, p["dead_col"]); box_uv(dead, 0.6)
    paint(dead, p["dead_col"], noise=0.15, seed=6)

def view(p):
    return dict(cam_loc=(6.0, -1.0, 1.5), target=(0, 0, 7.4), focal=20, sun=(55, 160), res=(720, 960), sky=(0.25, 0.45, 0.95))

finish("BC2_PALM01", build, D, view)
