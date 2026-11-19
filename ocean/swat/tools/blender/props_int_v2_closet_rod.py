# Interior props v2: open motel closet - wall shelf, end brackets, round rod,
# a few wire hangers (one with a hanging shirt).
# Reference: John Margolies, "Homowack Room 317 closet, Mamakating, New York"
# (1977, Library of Congress LCCN 2017712915, public domain).
# WALL-mounted: origin = wall plane at floor level (y = 0 is the floor), the
# closet projects toward +Z. Length 1.0 m, depth 0.42 m, shelf top 1.78 m,
# rod centre 1.68 m.
#   blender -b --factory-startup --python-exit-code 1 -P props_int_v2_closet_rod.py -- OUT_DIR [--render PNG]
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

reset()
rng = np.random.default_rng(4170)
L, DEP = 1.0, .42
SHELF_Y, SHELF_T = 1.78, .013
ROD_Y, ROD_Z, ROD_R = 1.71, .26, .017

paint = mat("cl_paint", (0.82, 0.80, 0.74), .55)
n = 128
mott = periodic_noise(rng, n, 6)
speck = (periodic_noise(rng, n, 40) > .72).astype(float)
rod_img = to_image("cl_rod", np.dstack([.14 + .16 * mott + .25 * speck, .12 + .13 * mott + .2 * speck, .10 + .10 * mott + .1 * speck]))
rod = mat("cl_rod", (1, 1, 1), .5, metal=.2, image=rod_img)
wire = mat("cl_wire", (0.55, 0.55, 0.53), .35, metal=1.0)
plastic = mat("cl_hanger", (0.93, 0.93, 0.92), .35)
# In-game iteration 1: the pale blue shirt glowed in the dim rooms; a brown
# check flannel shirt (period, darker value) with a woven check pattern.
_yy, _xx = np.mgrid[0:n, 0:n] / n
_check = (np.sin(_xx * 2 * np.pi * 6) > .3).astype(float) * .5 + (np.sin(_yy * 2 * np.pi * 6) > .3).astype(float) * .5
_base = .30 + .06 * periodic_noise(rng, n, 10) - .10 * _check
shirt_img = to_image("cl_shirt", np.dstack([_base * 1.25, _base * .85, _base * .55]))
shirt = mat("cl_shirt", (1, 1, 1), .85, image=shirt_img)

p = Part()
# Shelf board and two end plates that carry the rod (as in the photo's bracket).
p.add(bm_box((0, SHELF_Y - SHELF_T / 2, DEP / 2), (L / 2, SHELF_T / 2, DEP / 2), bevel=.002), paint)
for sx in (-1, 1):
    top_y = SHELF_Y - SHELF_T; low_y = ROD_Y - .045
    p.add(bm_box((sx * (L / 2 - .009), (top_y + low_y) / 2, .17), (.009, (top_y - low_y) / 2, .17), bevel=.002), paint)
# Rod.
p.add(bm_cyl((0, ROD_Y, ROD_Z), ROD_R, L - .036, seg=12, axis='x'), rod, uv_scale=3)

def hanger(x, yaw, material, plastic_body=False):
    """Hanger hanging from the rod; its plane faces along the rod (yaw about Y)."""
    hook = [(0, ROD_Y + .004, ROD_Z - .016), (0, ROD_Y + .020, ROD_Z - .006), (0, ROD_Y + .020, ROD_Z + .010),
            (0, ROD_Y + .004, ROD_Z + .018), (0, ROD_Y - .02, ROD_Z + .012), (0, ROD_Y - .045, ROD_Z)]
    top = ROD_Y - .045; bot = top - .15; half = .20
    tri = [(0, top, ROD_Z), (0, bot, ROD_Z - half), (0, bot, ROD_Z + half), (0, top, ROD_Z)]
    for pts, r in ((hook, .0016), (tri, .0075 if plastic_body else .0016)):
        bm = bm_tube(pts, r, seg=6)
        rotate_bm(bm, yaw, about=(0, ROD_Y, ROD_Z))
        bmesh.ops.translate(bm, vec=E(x, 0, 0), verts=bm.verts)
        p.add(bm, material if pts is tri else wire)
    return top, bot, half

# One white plastic hanger left, a bundle of wire hangers at the right end.
hanger(-.20, math.radians(-14), plastic, plastic_body=True)
for k, (x, yaw) in enumerate([(.36, 14), (.372, 22), (.384, 29), (.396, 35)]):
    hanger(x, math.radians(yaw), wire)
# A shirt on a wire hanger: a draped double-sided sheet in the hanger plane.
top, bot, half = hanger(.08, math.radians(4), wire)
def shirt_fn(u, v):
    zz = ROD_Z + (u - .5) * (2 * half + .04)
    shoulder = top - .02 - .13 * abs(u - .5) * 2
    y = shoulder - v * .72
    bulge = .025 * math.sin(math.pi * u) * (1 - .4 * v) + .006 * math.sin(9 * u + 3 * v)
    return (.08 + bulge, y, zz)
bm = bm_grid(14, 10, shirt_fn, double=True)
p.add(bm, shirt, uv_mode="fitside")
export("closet_rod", p)

# Striped wallpaper like the photo (preview only, not exported).
yy, xx = np.mgrid[0:64, 0:256] / 256
stripe = (np.sin(xx * 2 * math.pi * 22) > .6).astype(float)
wall_rgb = np.dstack([.80 - .12 * stripe, .74 - .38 * stripe, .74 - .34 * stripe])
MATS["_wall"] = mat("_wallimg", image=to_image("wallstripe", wall_rgb), rough=.8)

REF_VIEW = {"cam": (-.45, 1.52, 1.85), "target": (-.12, 1.58, 0), "fov": 38, "res": (960, 660),
            "room": {"box": (-1.5, 1.5, -.0, 1.6, 2.6), "walls": ["back"], "floor": (0.3, 0.1, 0.1)},
            "lights": [((.2, 1.5, 1.6), 80, (1, .97, .9), .2)], "ambient": .5, "sky": (.8, .8, .8)}
preview(REF_VIEW)
