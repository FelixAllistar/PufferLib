# Interior props v2: 1970s-80s compact drip coffee maker with glass carafe.
# Reference: "080202 mr coffee jr" by Dan4th (Flickr 2294690862, CC BY 2.0);
# shape and proportions only, no brand marks. Origin: floor (counter) centre
# of the back edge, front toward +Z.
#   blender -b --factory-startup --python-exit-code 1 -P props_int_v2_coffee_maker.py -- OUT_DIR [--render PNG]
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

reset()
rng = np.random.default_rng(7307)

W, D, H = .20, .235, .29          # overall width, depth, height
TW = .048                          # left reservoir tower width
BASE_H = .036
LID = .038                         # lid thickness
CX, CZ = (TW / 2) - .002, D / 2 + .028   # carafe / basket axis
CB = BASE_H + .004                 # carafe bottom
BT = H - LID                       # underside of the lid

shell = mat("cm_plastic", (0.80, 0.76, 0.66), .42)
dark = mat("cm_dark", (0.045, 0.04, 0.038), .35)
glass = mat("cm_glass", (0.15, 0.16, 0.16), .05, metal=.0)
coffee = mat("cm_coffee", (0.025, 0.012, 0.006), .1)
band = mat("cm_band", (0.13, 0.07, 0.06), .5)
chrome = mat("cm_chrome", (0.75, 0.75, 0.75), .25, metal=1.0)
red = mat("cm_lamp", (0.85, 0.08, 0.05), .3, emission=(0.6, 0.02, 0.0))

p = Part()
# Base plate: full footprint, slightly rounded.
p.add(bm_box((0, BASE_H / 2, D / 2), (W / 2, BASE_H / 2, D / 2), bevel=.006, seg=2), shell)
# Left reservoir tower, base to lid, full depth.
p.add(bm_box((-W / 2 + TW / 2, (BT + BASE_H) / 2, D / 2 - .004), (TW / 2, (BT - BASE_H) / 2 + .002, D / 2 - .004), bevel=.006, seg=2), shell)
# Rear spine behind the carafe.
p.add(bm_box((TW / 2, (BT + BASE_H) / 2, .018), (W / 2 - TW / 2, (BT - BASE_H) / 2, .018), bevel=.004), shell)
# Lid: overhangs the carafe, slight front lip.
p.add(bm_box((0, H - LID / 2, D / 2 + .004), (W / 2 + .003, LID / 2, D / 2 + .004), bevel=.007, seg=2), shell)
# Fill grille on the lid top.
p.add(bm_box((0, H + .0005, .06), (.055, .002, .022)), shell)
# Brown decor band across the lid front (plain, no lettering).
p.add(bm_box((0, H - LID * .55, D + .0085), (W / 2 - .004, .0095, .0012)), band)
# Filter basket: dark truncated cone under the lid, resting on the carafe, front tab.
BH = .088
p.add(bm_lathe([(.046, 0), (.066, .045), (.080, BH)], c=(CX, BT - BH, CZ), seg=24, cap_bottom=True), dark)
p.add(bm_box((CX - .006, BT - .007, D - .010), (.070, .007, .012), bevel=.002), dark)
# Warming plate.
p.add(bm_cyl((CX, BASE_H + .002, CZ), .064, .004, seg=24), dark)
# Carafe: glass body, chrome collar, coffee inside, black handle.
CH = BT - BH + .012 - CB
# Opaque engine-friendly carafe: coffee-filled lower body, clear upper glass.
CL = CH * .5
p.add(bm_lathe([(.056, 0), (.072, .025), (.075, CL)], c=(CX, CB, CZ), seg=28, cap_bottom=True), coffee)
p.add(bm_lathe([(.075, 0), (.075, CH * .55 - CL), (.068, CH * .85 - CL), (.063, CH - CL)], c=(CX, CB + CL, CZ), seg=28), glass)
p.add(bm_lathe([(.0655, 0), (.0665, .018)], c=(CX, CB + CH - .024, CZ), seg=28), chrome)
p.add(bm_box((CX + .068, CB + CH * .5, CZ + .045), (.010, CH * .38, .012), bevel=.004), dark)
# Power switch with lamp on the tower front.
p.add(bm_box((-W / 2 + TW / 2, BASE_H + .03, D - .008 + .0015), (.010, .007, .002)), red)
export("coffee_maker", p)

REF_VIEW = {"cam": (-.26, .52, .80), "target": (.0, .15, .12), "fov": 34, "res": (960, 720),
            "room": {"box": (-.6, .6, -.01, .7, .9), "walls": ["back", "right"],
                     "wall": (0.75, 0.75, 0.72), "floor": (0.22, 0.11, 0.05)},
            "lights": [((-.6, 1.4, 1.2), 60, (1, .95, .88), .6)], "ambient": .6, "sky": (.8, .8, .8)}
preview(REF_VIEW)
