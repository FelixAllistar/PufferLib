# Laundry cart (canvas bin truck on casters) for the Briar Court laundry,
# interior props layer v2. Origin: floor, centre of the back edge; front +Z.
#   blender -b --factory-startup --python-exit-code 1 \
#       -P ocean/swat/tools/blender/props_int_v2_laundry_cart.py -- OUT_DIR [--render PNG]
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

W, D, H = .75, .50, .85          # outer size
WHEEL_R = .038
RIM_Y = H - .03                  # top rim tube height
BIN_BOTTOM = .13
TUBE = .011

reset()
rng = np.random.default_rng(4211)

# Canvas: CC0 Poly Haven rough_linen, greyed toward an old duck-canvas tone.
canvas_src = polyhaven("rough_linen", "diff", "1k", size=512)
canvas_img = tint_image(canvas_src, "cart_canvas", mul=(.95, .88, .70), add=(.30, .27, .19))
terry = polyhaven("terry_cloth", "diff", "1k", size=512)
# Bake handling grime: darker, warmer toward the base and the rim corners.
_w, _h = canvas_img.size
_px = np.array(canvas_img.pixels[:], dtype=np.float32).reshape(_h, _w, 4)
_g = periodic_noise(np.random.default_rng(77), _h, 3)
_px[:, :, :3] *= (.86 + .14 * _g)[:, :, None]
canvas_img.pixels.foreach_set(_px.ravel()); canvas_img.pack()
m_canvas = mat("canvas", image=canvas_img, rough=.95)
m_steel = mat("steel", (.20, .21, .21), rough=.62, metal=.5)
m_rubber = mat("rubber", (.03, .03, .03), rough=.8)
m_towel = mat("towel", image=terry, rough=1.0)
m_sheet = mat("sheet", (.86, .85, .80), rough=.9)

p = Part()
cx, cz = 0.0, D / 2
x0, x1 = -W / 2 + .02, W / 2 - .02
z0, z1 = .02, D - .02

# Casters: swivel yoke, wheel and plate under each corner.
for sx in (-1, 1):
    for sz in (-1, 1):
        wx, wz = sx * (W / 2 - .06), cz + sz * (D / 2 - .06)
        p.add(bm_cyl((wx, WHEEL_R, wz), WHEEL_R, .028, seg=14, axis='x'), m_rubber)
        p.add(bm_cyl((wx, WHEEL_R, wz), .014, .034, seg=8, axis='x'), m_steel)
        for k in (-1, 1):
            p.add(bm_box((wx + k * .019, WHEEL_R + .022, wz), (.003, .03, .016)), m_steel)
        p.add(bm_box((wx, 2 * WHEEL_R + .018, wz), (.03, .006, .03)), m_steel)
        p.add(bm_cyl((wx, 2 * WHEEL_R + .03, wz), .008, .02, seg=8), m_steel)

# Steel tube frame: base rectangle, four corner posts, top rim.
BASE_Y = 2 * WHEEL_R + .045
corners = [(x0, z0), (x1, z0), (x1, z1), (x0, z1), (x0, z0)]
p.add(bm_tube([(x, BASE_Y, z) for x, z in corners], TUBE, seg=8, cap=False), m_steel)
p.add(bm_tube([(x, RIM_Y, z) for x, z in corners], TUBE * 1.2, seg=8, cap=False), m_steel)
for x, z in corners[:4]:
    p.add(bm_tube([(x, BASE_Y, z), (x, RIM_Y, z)], TUBE * .8, seg=8), m_steel)
# Flat steel X straps on the two ends and the long sides, outside the canvas.
def strap(a, b):
    d = Vector(b) - Vector(a); L = d.length
    bm = bm_box(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2), (.012, L / 2, .003))
    return bm, a, b
for (ax, az, bx, bz, nx, nz) in ((x0, z0, x1, z0, 0, -1), (x0, z1, x1, z1, 0, 1), (x0, z0, x0, z1, -1, 0), (x1, z0, x1, z1, 1, 0)):
    for flip in (0, 1):
        A = (ax, BASE_Y, az) if not flip else (bx, BASE_Y, bz)
        B = (bx, RIM_Y, bz) if not flip else (ax, RIM_Y, az)
        o = .028
        pts = [(A[0] + nx * .004, A[1], A[2] + nz * .004), ((A[0] + B[0]) / 2 + nx * o, (A[1] + B[1]) / 2, (A[2] + B[2]) / 2 + nz * o), (B[0] + nx * .004, B[1], B[2] + nz * .004)]
        p.add(bm_tube(pts, .006, seg=4), m_steel)
# Cross bars on the base carrying the canvas floor.
for t in (.33, .66):
    xx = x0 + t * (x1 - x0)
    p.add(bm_tube([(xx, BASE_Y, z0), (xx, BASE_Y, z1)], TUBE * .8, seg=6), m_steel)

# Canvas bin: hangs from the rim, sides sag inward a little mid-height,
# rolled hem over the rim.
inset = .012
bx0, bx1, bz0, bz1 = x0 + inset, x1 - inset, z0 + inset, z1 - inset
per = 2 * (bx1 - bx0) + 2 * (bz1 - bz0)
def perimeter(u):
    s = u * per
    for (ax, az, bx, bz) in ((bx0, bz0, bx1, bz0), (bx1, bz0, bx1, bz1), (bx1, bz1, bx0, bz1), (bx0, bz1, bx0, bz0)):
        L = abs(bx - ax) + abs(bz - az)
        if s <= L + 1e-9:
            t = s / L
            nx, nz = (0, -1) if az == bz and bz == bz0 else (1, 0) if ax == bx and bx == bx1 else (0, 1) if az == bz else (-1, 0)
            return ax + (bx - ax) * t, az + (bz - az) * t, nx, nz, t
        s -= L
    return bx0, bz0, 0, -1, 0
BIN_TOP = RIM_Y - .005
def side(u, v):
    x, z, nx, nz, t = perimeter(u)
    y = BIN_BOTTOM + (BIN_TOP - BIN_BOTTOM) * v
    edge = math.sin(math.pi * t)                 # 0 at corners, 1 mid side
    taper = -.035 * (1 - v)                      # bin narrows toward the floor
    bulge = .03 * edge * math.sin(math.pi * v) ** 1.2   # laden canvas pushes out
    wr = .004 * math.sin(7 * math.pi * v + 3 * u) * edge
    wr += .010 * math.sin(13 * u + 1.5 * math.sin(3 * v)) * math.sin(math.pi * v) * edge  # broad slack folds
    o = taper + bulge + wr
    return x + nx * o, y, z + nz * o
p.add(bm_grid(72, 12, side), m_canvas, uv_scale=1.6)
# Inner face 4 mm inside (a coincident reversed copy z-fights in renderers).
def side_in(u, v):
    x, y, z = side(u, v); px, pz, nx, nz, t = perimeter(u)
    return x - nx * .004, y, z - nz * .004
inner = bm_grid(72, 12, side_in); bmesh.ops.reverse_faces(inner, faces=list(inner.faces))
p.add(inner, m_canvas, uv_scale=1.6)
# Canvas floor, slightly bellied.
def floor(u, v):
    x = bx0 + (bx1 - bx0) * u; z = bz0 + (bz1 - bz0) * v
    return x, BIN_BOTTOM - .015 * math.sin(math.pi * u) * math.sin(math.pi * v), z
p.add(bm_grid(8, 6, floor, double=True), m_canvas, uv_scale=1.6)
# Rolled hem over the rim.
# Canvas laced under the rim with grommets (dark dots) instead of a padded hem.
for gx in np.linspace(bx0 + .05, bx1 - .05, 6):
    for gz, sgn in ((bz0, -1), (bz1, 1)):
        p.add(bm_cyl((gx, RIM_Y - .03, gz + sgn * .006), .007, .004, seg=8, axis='z'), m_steel)

# Heap of used linen: a lumpy mound inside the bin, peaking above the rim,
# plus a towel hanging over the front rim.
def heap(u, v):
    x = bx0 + .01 + (bx1 - bx0 - .02) * u; z = bz0 + .01 + (bz1 - bz0 - .02) * v
    r = math.sin(math.pi * u) * math.sin(math.pi * v)
    lump = .03 * math.sin(9 * u + 2) * math.sin(7 * v + 1) + .02 * math.sin(17 * u * v + 4)
    return x, RIM_Y - .10 + .24 * r ** .5 + lump * r, z
# Folded sheets thrown on top of the heap: soft slabs with drooping edges.
for k, (fx, fz, fy, fw, fd, fyaw) in enumerate(((-.12, .22, .06, .30, .22, .25), (.10, .30, .10, .28, .20, -.4), (.02, .18, .15, .24, .18, .9))):
    def slab(u, v, fx=fx, fz=fz, fy=fy, fw=fw, fd=fd, fyaw=fyaw):
        lx = (u - .5) * fw; lz = (v - .5) * fd
        c, sn = math.cos(fyaw), math.sin(fyaw)
        droop = -.05 * (max(0, abs(u - .5) * 2 - .6) ** 2 + max(0, abs(v - .5) * 2 - .6) ** 2) / .16
        return fx + c * lx + sn * lz, RIM_Y + fy + droop + .006 * math.sin(9 * u + k), fz + D / 2 - .25 - sn * lx + c * lz
    sb = bm_grid(8, 6, slab); bmesh.ops.solidify(sb, geom=list(sb.faces), thickness=.02)
    p.add(sb, m_sheet, uv_scale=2)
hb = bm_grid(16, 12, heap)
soften(hb, rng, .003)
p.add(hb, m_sheet, uv_scale=2)
def towel(u, v):
    # Draped over the front rim: inside part, over the tube, down the front.
    x = -.10 + .30 * u
    if v < .35:
        s = v / .35; return x, RIM_Y + .03 * s + .01 * math.sin(6 * u), bz1 - .10 + .10 * s
    s = (v - .35) / .65
    return x + .02 * s * math.sin(5 * u), RIM_Y + .03 - .30 * s, bz1 + .025 + .015 * s + .012 * math.sin(11 * u + 4 * s)
p.add(bm_grid(8, 10, towel, double=True), m_sheet, uv_scale=2.5)

export("laundry_cart", p)

REF_VIEW = {
    "cam": (1.75, 1.45, 1.05), "target": (0, .40, .25), "fov": 36, "res": (640, 860),
    "room": {"box": (-1.2, 1.6, -.02, 2.2, 2.5), "wall": (.62, .6, .55), "floor": (.55, .55, .52), "walls": ["back", "left"]},
    "lights": [((.8, 2.4, 1.6), 260, (1, .95, .88), .6), ((-.6, 1.8, 2.0), 90, (.9, .95, 1), .8)],
    "ambient": .5,
}
preview(REF_VIEW)
