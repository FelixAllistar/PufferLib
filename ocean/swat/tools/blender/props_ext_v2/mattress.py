# BC2_MATTRESS01: discarded single mattress, blue quilted ticking with white
# piping, propped on its long edge (lean it against the dumpster).
# Reference: Wikimedia Commons "Discarded mattress by trash bin
# (18822330575).jpg" by Eric Fischer (CC BY 2.0), see LOG.md. The quilt map
# is drawn by this script (original, CC0). Origin: ground under the bottom edge.
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(L=1.90, W=0.92, T=0.20, tilt=58.0, bevel=0.08, ticking=[0.31, 0.33, 0.40], dirt=0.7, piping=[0.86, 0.86, 0.84],
         cell=0.11, stitch=0.68, sag=0.03, seed=3)

def quilt_image(p, size=256):
    """Hexagonal quilting stitches on a tiling 1 m tile (white = cloth)."""
    img = bpy.data.images.new("T_quilt", size, size)
    px = [1.0] * (size * size * 4)
    cell = p["cell"]; n = max(1, round(1.0 / cell)); c = 1.0 / n
    rnd = random.Random(p["seed"])
    for y in range(size):
        for x in range(size):
            u, v = x / size, y / size
            # offset rows -> hex-ish lattice; distance to the cell border
            row = int(v / (c * 0.866)); uu = u + (0.5 * c if row % 2 else 0)
            fu = (uu % c) / c - 0.5; fv = (v % (c * 0.866)) / (c * 0.866) - 0.5
            d = max(abs(fu) * 1.0 + abs(fv) * 0.58, abs(fv) * 1.15)
            k = 1.0 - (1 - p["stitch"]) * max(0.0, 1 - abs(d - 0.5) * 22) - 0.18 * max(0.0, d - 0.35)
            k *= 0.94 + 0.06 * rnd.random()
            i = 4 * (y * size + x); px[i] = px[i + 1] = px[i + 2] = k
    img.pixels = px; img.file_format = "PNG"; img.pack()
    return img

def build(p):
    tex = quilt_image(p)
    grain = image("dirty_concrete", 256, gray=True, mean=0.9, gain=0.4)
    cloth = material("ticking", tex, rough=0.95)
    pipe = material("piping", grain, rough=0.9)
    L, W, T = p["L"], p["W"], p["T"]
    bm = bmesh.new(); bm_box(bm, 0, 0, 0, W, L, T)
    bmesh.ops.subdivide_edges(bm, edges=[e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) < 1e-6], cuts=5, use_grid_fill=True)
    bmesh.ops.bevel(bm, geom=[e for e in bm.edges if (e.verts[0].co - e.verts[1].co).length > 0.1 and abs(abs(e.verts[0].co.z) - T / 2) < 1e-4 and abs(abs(e.verts[1].co.z) - T / 2) < 1e-4 and (abs(abs(e.verts[0].co.x) - W / 2) < 1e-4 and abs(abs(e.verts[1].co.x) - W / 2) < 1e-4 or abs(abs(e.verts[0].co.y) - L / 2) < 1e-4 and abs(abs(e.verts[1].co.y) - L / 2) < 1e-4)], offset=p["bevel"], segments=3, affect="EDGES", profile=0.6, clamp_overlap=True)
    # sag: the middle of the faces bulges/dips a little
    for v in bm.verts:
        f = (1 - (2 * v.co.x / W) ** 2) * (1 - (2 * v.co.y / L) ** 2)
        v.co.z += math.copysign(p["sag"] * f, v.co.z) if abs(v.co.z) > T * 0.3 else 0
    bm.normal_update()
    m = new_obj("mattress", bm, cloth, p["ticking"], smooth=True)
    for poly in m.data.polygons: poly.use_smooth = True
    box_uv(m, 1.0); paint(m, p["ticking"], noise=0.06, seed=2, grad=(-0.46, 0.2, p.get("dirt", 1.0)))
    # piping around both faces
    for z in (-T / 2, T / 2):
        r = W / 2 - p["bevel"] * 0.35; q = L / 2 - p["bevel"] * 0.35
        pts = [(-r, -q, z), (r, -q, z), (r, q, z), (-r, q, z), (-r, -q, z)]
        t = tube("pipe", pts, 0.008, 5, pipe, p["piping"]); box_uv(t, 0.3)
    # pose: long edge on the ground, leaning back by tilt (deg from horizontal)
    a = math.radians(p["tilt"])
    for ob in list(bpy.context.scene.objects):
        if ob.type != "MESH": continue
        ob.rotation_euler = (0, -a, 0)
        ob.location = (math.cos(a) * W / 2 - math.sin(a) * (-T / 2) * 0, 0, math.sin(a) * W / 2 + math.cos(a) * T / 2)

def view(p):
    return dict(cam_loc=(-1.7, 1.9, 1.45), target=(0.25, -0.1, 0.42), focal=26, sun=(60, 220), res=(960, 720), ground=(0.42, 0.40, 0.38))

finish("BC2_MATTRESS01", build, D, view)
