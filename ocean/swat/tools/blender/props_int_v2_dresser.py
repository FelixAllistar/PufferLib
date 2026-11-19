# Interior props v2: long low walnut double dresser (2 x 3 drawers, chrome
# bar pulls, short tapered legs) with a framed wall mirror above.
# Reference: John Margolies, "Bavarian Manor, room, motel annex, Purling, New
# York" (1977, Library of Congress LCCN 2017712643, public domain).
# Texture: Poly Haven american_walnut_veneer (CC0), warm-tinted, 1K embedded.
# Origin: floor centre of the back edge (against the wall), front toward +Z.
# Dresser 1.10 x 0.45 x 0.76 m; mirror 0.80 x 0.92 m, bottom at 0.80 m.
#   blender -b --factory-startup --python-exit-code 1 -P props_int_v2_dresser.py -- OUT_DIR [--render PNG]
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

reset()
W, D, H = 1.10, .45, .76
TOP_T = .02
LEG_H = .175
GAP = .005
MW, MH, MB, MF = .80, .92, .80, .06    # mirror width, height, bottom, frame width

veneer = polyhaven("american_walnut_veneer", "diff", "1k", size=1024)
wood = mat("dr_walnut", (0.50, 0.27, 0.16), .22, image=veneer)
wood_dark = mat("dr_walnut_dark", (0.36, 0.19, 0.11), .45, image=veneer)
chrome = mat("dr_chrome", (0.8, 0.8, 0.78), .22, metal=1.0)
# In-game iteration 1: the engine has no planar reflections and a metallic
# mirror picked up the bright sky probe (flat white panel). Bake a dim,
# smoky "reflected room" instead: dark warm-grey with a soft diagonal sheen
# and darker desilvered edges, non-metal.
_n = 256
_y, _x = (np.mgrid[0:_n, 0:_n] + .5) / _n
_sheen = np.exp(-((_x * .9 + _y * .6 - .75) / .16) ** 2) * .22
_edge = np.clip(np.minimum.reduce([_x, 1 - _x, _y, 1 - _y]) / .06, 0, 1)
_spots = periodic_noise(np.random.default_rng(77), _n, 20, 3)
_v = (.20 + _sheen + .05 * _y) * (.55 + .45 * _edge) * (.9 + .1 * _spots)
_mirror = to_image("dr_mirror_reflect", np.dstack([_v * 1.02, _v * .98, _v * .92]))
glass = mat("dr_mirror", image=_mirror, rough=.15)
leg = mat("dr_leg", (0.10, 0.07, 0.05), .45)

p = Part()
# Carcass and slightly overhanging top.
p.add(bm_box((0, (LEG_H + H - TOP_T) / 2, D / 2 - .005), (W / 2 - .01, (H - TOP_T - LEG_H) / 2, D / 2 - .005), bevel=.004, seg=2), wood_dark, uv_scale=1.0)
p.add(bm_box((0, H - TOP_T / 2, D / 2 + .004), (W / 2, TOP_T / 2, D / 2 + .004), bevel=.006, seg=3), wood, uv_scale=1.0)
# Short tapered legs at the corners.
for sx in (-1, 1):
    for z in (.05, D - .05):
        bm = bm_cyl((sx * (W / 2 - .07), LEG_H / 2, z), .010, LEG_H, seg=8, r2=.017)
        p.add(bm, leg)
# Drawer fronts: two columns of three, chrome bar pulls.
cols = (-W / 4 + .002, W / 4 - .002)
row_h = (H - TOP_T - LEG_H - 4 * GAP) / 3
for cx in cols:
    for r in range(3):
        y0 = LEG_H + GAP + r * (row_h + GAP)
        cy = y0 + row_h / 2
        p.add(bm_box((cx, cy, D - .002), (W / 4 - .012, row_h / 2, .010), bevel=.003, seg=2), wood, uv_scale=1.0, uv_offset=(.13 * r + .4 * (cx > 0), .07 * r))
        p.add(bm_box((cx, cy + row_h * .22, D + .011), (.030, .008, .005), bevel=.002), chrome)
# Mirror on the wall: wood frame, glass, small back spacer.
mcy = MB + MH / 2
p.add(bm_box((0, mcy, .015), (MW / 2, MH / 2, .015), bevel=.004, seg=2), wood_dark, uv_scale=1.0)
p.add(bm_box((0, mcy, .031), (MW / 2 - MF, MH / 2 - MF, .002)), glass, uv_mode="fit")
export("dresser", p)

REF_VIEW = {"cam": (-1.55, 1.25, 2.2), "target": (2.2, .62, .45), "fov": 70, "res": (960, 663),
            "room": {"box": (-3.0, 3.6, -.0, 3.8, 2.5), "walls": ["back", "right", "ceiling"],
                     "wall": (0.22, 0.09, 0.045), "floor": (0.36, 0.37, 0.42)},
            "lights": [((3.0, 1.6, 2.0), 300, (1, .95, .85), 1.2)], "ambient": .5, "sky": (.9, .85, .75)}
preview(REF_VIEW)
