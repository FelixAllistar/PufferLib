# Interior props v2: compact under-counter refrigerator with woodgrain vinyl
# cabinet (1970s-80s motel / dorm style).
# Reference: "Small refrigerator" by theslowlane (Flickr 9373843725, CC BY 2.0).
# Texture: Poly Haven american_walnut_veneer (CC0), warm-tinted, grain vertical.
# Origin: floor centre of the back edge, front (door) toward +Z.
#   blender -b --factory-startup --python-exit-code 1 -P props_int_v2_mini_fridge.py -- OUT_DIR [--render PNG]
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

reset()
W, D, H = .48, .50, .85
TOP = .03            # laminate top thickness
KICK = .045           # black plinth height

def rotated(img, name):
    """Copy of an image rotated 90 degrees so the veneer grain runs vertically."""
    w, h = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    px = np.ascontiguousarray(np.rot90(px))
    out = bpy.data.images.new(name, px.shape[1], px.shape[0])
    out.pixels.foreach_set(px.ravel()); out.file_format = 'PNG'; out.pack()
    return out

veneer = rotated(polyhaven("american_walnut_veneer", "diff", "1k", size=512), "fridge_veneer")
wood = mat("fr_wood", (0.58, 0.21, 0.12), .55, image=veneer)
top = mat("fr_top", (0.60, 0.26, 0.15), .35, image=veneer)
black = mat("fr_black", (0.03, 0.03, 0.03), .6)
seal = mat("fr_seal", (0.10, 0.09, 0.085), .7)
chrome = mat("fr_chrome", (0.7, 0.7, 0.68), .3, metal=1.0)

p = Part()
# Cabinet carcass (sides/back), set back from the door.
p.add(bm_box((0, (KICK + H - TOP) / 2, (D - .03) / 2), (W / 2, (H - TOP - KICK) / 2, (D - .03) / 2), bevel=.006, seg=3), wood, uv_scale=1.2)
# Laminate top, small overhang.
p.add(bm_box((0, H - TOP / 2, D / 2), (W / 2 + .006, TOP / 2, D / 2 + .006), bevel=.006, seg=3), top, uv_scale=1.2)
# Recessed black plinth / vent.
p.add(bm_box((0, KICK / 2, D / 2 - .02), (W / 2 - .01, KICK / 2, D / 2 - .02)), black)
for k in range(6):
    p.add(bm_box((0, .010 + k * .0055, D - .039), (W / 2 - .05, .0012, .0015)), seal)
# Door: woodgrain slab, rubber seal line, recessed handle on the right edge.
DT = .028
p.add(bm_box((-.004, (KICK + H - TOP) / 2, D - .03 + DT / 2), (W / 2 - .026, (H - TOP - KICK) / 2 - .003, DT / 2), bevel=.004, seg=3), wood, uv_scale=1.2)
p.add(bm_box((0, (KICK + H - TOP) / 2, D - .031), (W / 2 - .001, (H - TOP - KICK) / 2 - .001, .002)), seal)
p.add(bm_box((W / 2 - .034, (KICK + H - TOP) / 2, D - .03 + DT / 2), (.004, (H - TOP - KICK) / 2 - .02, DT / 2 + .001)), black)
# Front stiles of the cabinet either side of the door.
p.add(bm_box((0, (KICK + H - TOP) / 2, D - .03 + DT / 2 - .004), (W / 2, (H - TOP - KICK) / 2, DT / 2 - .004)), wood, uv_scale=1.2)
# Chrome maker's plate on the right side, near the top front.
p.add(bm_box((W / 2 + .001, H - TOP - .07, D - .12), (.0015, .012, .05)), chrome)
# Small chrome badge on the door.
p.add(bm_box((-W / 2 + .07, H - TOP - .06, D - .002 + .001), (.03, .006, .001)), chrome)
# Wear: two old inspection/price stickers on the door.
label = mat("fr_label", (0.78, 0.74, 0.64), .8)
p.add(bm_box((-.10, H - TOP - .19, D - .0005), (.022, .028, .0008)), label)
p.add(bm_box((-.045, H - TOP - .25, D - .0005), (.016, .022, .0008)), label)
export("mini_fridge", p)

REF_VIEW = {"cam": (.32, 1.6, 1.12), "target": (-.04, .40, .22), "fov": 60, "res": (960, 720),
            "room": {"box": (-1.2, 1.2, -.01, 1.5, 2.4), "walls": ["back", "right"],
                     "wall": (0.36, 0.45, 0.43), "floor": (0.05, 0.05, 0.05)},
            "lights": [((-.4, 2.2, 1.2), 120, (1, .95, .88), .3)], "ambient": .5, "sky": (.7, .75, .75)}
preview(REF_VIEW)
