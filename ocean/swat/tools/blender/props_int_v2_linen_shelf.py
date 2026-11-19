# Linen shelving (open plywood utility shelves with folded towels, sheets and
# supplies) for the Briar Court laundry, interior props layer v2.
# Origin: floor, centre of the back edge; front +Z.
#   blender -b --factory-startup --python-exit-code 1 \
#       -P ocean/swat/tools/blender/props_int_v2_linen_shelf.py -- OUT_DIR [--render PNG]
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

W, D, H = .90, .40, 1.80
T = .025                         # plywood thickness (3/4" + edge banding)
SHELVES = [.09, .47, .85, 1.23, 1.61]   # top faces of the shelf boards (kick board at 0.09)

reset()
rng = np.random.default_rng(5150)
ply_src = polyhaven("plywood", "diff", "1k", size=512)
ply_img = tint_image(ply_src, "shelf_ply", mul=(.58, .55, .50), add=(.10, .10, .09))
terry_src = polyhaven("terry_cloth", "diff", "1k", size=512)
# Desaturate the CC0 terry weave to white motel towels (keep only its luminance).
_w, _h = terry_src.size
_px = np.array(terry_src.pixels[:], dtype=np.float32).reshape(_h, _w, 4)
_l = _px[:, :, :3].mean(axis=2, keepdims=True); _l = .78 + .5 * (_l - _l.mean())
terry = to_image("towel_white", np.clip(np.repeat(_l, 3, axis=2) * np.array([1, .99, .95]), 0, 1)[::-1])
# Handling wear: blotchy grime and scuffs baked over the CC0 plywood.
_w, _h = ply_img.size
_px = np.array(ply_img.pixels[:], dtype=np.float32).reshape(_h, _w, 4)
_g = periodic_noise(np.random.default_rng(51), _h, 4); _s = periodic_noise(np.random.default_rng(52), _h, 40)
_px[:, :, :3] *= (.78 + .26 * _g)[:, :, None] * (1 - .12 * (_s > .78))[:, :, None]
ply_img.pixels.foreach_set(_px.ravel()); ply_img.pack()
def as_jpeg(img, name):
    # Embed baked maps as JPEG (the PNG of a noisy 512 map is ~4x larger).
    path = os.path.join(CACHE, f"_{name}.jpg"); sc = bpy.context.scene
    sc.render.image_settings.file_format = 'JPEG'; sc.render.image_settings.quality = 88
    img.save_render(path, scene=sc); out = bpy.data.images.load(path); out.name = name; out.pack(); return out
ply_img = as_jpeg(ply_img, "shelf_ply_j"); terry = as_jpeg(terry, "towel_white_j")
m_ply = mat("plywood", image=ply_img, rough=.75)
m_back = mat("hardboard", (.55, .50, .42), rough=.85)
m_towel = mat("towel", image=terry, rough=1.0)
m_sheet = mat("sheet", (.88, .87, .83), rough=.9)
m_card = mat("carton", (.20, .12, .06), rough=.9)
m_jug = mat("jug", (.80, .80, .76), rough=.5)

p = Part()
cz = D / 2
# Carcass: two sides, top, back, kick board, shelves.
for sx in (-1, 1):
    p.add(bm_box((sx * (W / 2 - T / 2), H / 2, cz), (T / 2, H / 2, D / 2)), m_ply, uv_scale=1.2)
p.add(bm_box((0, H - T / 2, cz), (W / 2, T / 2, D / 2)), m_ply, uv_scale=1.2)
p.add(bm_box((0, H / 2, .004), (W / 2 - T, H / 2 - T, .004)), m_back)
p.add(bm_box((0, .045, D - .03), (W / 2 - T, .045, .012)), m_ply, uv_scale=1.2)
# Face frame: stiles and a deep top rail, like built-in linen-room shelving.
for sx in (-1, 1):
    p.add(bm_box((sx * (W / 2 - .0225), H / 2, D + .009), (.0225, H / 2, .009)), m_ply, uv_scale=1.2)
p.add(bm_box((0, H - .04, D + .009), (W / 2 - .045, .04, .009)), m_ply, uv_scale=1.2)
# Shelf boards sag a little at mid-span under years of linen.
for y in SHELVES:
    sb = bm_box((0, y - T / 2, cz + .005), (W / 2 - T, T / 2, D / 2 - .012))
    bmesh.ops.subdivide_edges(sb, edges=[e for e in sb.edges if abs((e.verts[0].co - e.verts[1].co).x) > .5], cuts=6)
    for v in sb.verts:
        v.co.z -= .006 * max(0, 1 - (v.co.x / (W / 2 - T)) ** 2)
    p.add(sb, m_ply, uv_scale=1.2)

IW = W - 2 * T
def towel_stack(x, y, z, n, w=.30, d=.22, t=.045, yaw=0.0):
    """Folded towels: soft slabs, rounded front fold, slight jitter."""
    for k in range(n):
        jx, jz = rng.normal(0, .008, 2)
        ww = w * (1 + rng.normal(0, .03)); dd = d * (1 + rng.normal(0, .03))
        bm = bm_box((x + jx, y + t * (k + .5), z + jz), (ww / 2, t / 2 - .001, dd / 2), bevel=.019, seg=3)
        # Terry is soft: the folded front bulges, the middle sags under the load above.
        for v in bm.verts:
            e = v.co.copy(); ex, ey, ez = e.x - (x + jx), e.z - (y + t * (k + .5)), -e.y - (z + jz)
            v.co.z += -.004 * (1 - (2 * ex / ww) ** 2) * (1 if ey > 0 else .3)
            if ez > 0: v.co.y -= .006 * (1 - (2 * ex / ww) ** 2)
        rotate_bm(bm, yaw + rng.normal(0, .05), (x, y, z))
        p.add(bm, m_towel, uv_scale=3)
def sheet_stack(x, y, z, n, w=.36, d=.26, t=.035):
    for k in range(n):
        jx, jz = rng.normal(0, .006, 2)
        p.add(bm_box((x + jx, y + t * (k + .5), z + jz), (w / 2, t / 2 - .001, d / 2), bevel=.008, seg=2), m_sheet, uv_scale=2)

# Shelf 1 (floor shelf): cartons and a jug.
y = SHELVES[0]
p.add(bm_box((-.22, y + .13, .20), (.17, .13, .15), bevel=.004), m_card, uv_scale=2)
p.add(bm_box((.08, y + .10, .18), (.12, .10, .14), bevel=.004), m_card, uv_scale=2)
p.add(bm_cyl((.30, y + .13, .22), .07, .26, seg=14), m_jug)
p.add(bm_cyl((.30, y + .28, .22), .022, .04, seg=10), m_jug)
# Shelf 2: towels, three stacks.
y = SHELVES[1]
towel_stack(-.26, y, .21, 6); towel_stack(.02, y, .21, 5, yaw=.05); towel_stack(.27, y, .22, 4, w=.26)
# Shelf 3: sheets two stacks + a short towel stack.
y = SHELVES[2]
sheet_stack(-.20, y, .21, 7); sheet_stack(.18, y, .21, 5)
# Shelf 4: hand towels and a carton.
y = SHELVES[3]
towel_stack(-.25, y, .20, 7, w=.24, d=.18, t=.03); towel_stack(.02, y, .20, 5, w=.24, d=.18, t=.03)
p.add(bm_box((.26, y + .09, .18), (.10, .09, .13), bevel=.004), m_card, uv_scale=2)
# Top shelf: pillowcases / spare blanket, low.
y = SHELVES[4]
sheet_stack(-.12, y, .20, 3, w=.50, d=.30, t=.05)

# In-game fit (iteration 8): the laundry nook between the north wall and the
# desk is 0.80 m wide, so the finished unit is built 0.76 m wide (x scaled).
FIT_W = .76
for v in p.bm.verts:
    v.co.x *= FIT_W / W
export("linen_shelf", p)

REF_VIEW = {
    "cam": (.30, 1.20, 3.0), "target": (0, .92, .2), "fov": 42, "res": (560, 980),
    "room": {"box": (-1.2, 1.2, -.02, 3.0, 2.4), "wall": (.62, .6, .55), "floor": (.40, .38, .35), "walls": ["back", "left", "right", "ceiling"]},
    "lights": [((-.3, 2.3, 1.6), 220, (1, 1, 1), .8), ((.8, 1.6, 2.4), 60, (1, 1, 1), 1.0)],
    "ambient": .4,
}
preview(REF_VIEW)
