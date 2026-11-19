# Briar Court interior props v2: 1960s-70s bottled water cooler (reception).
# Enamelled steel cabinet with a chrome top, front push-button faucet over a
# drip grille, an inverted 5-gallon bottle and a side paper-cup dispenser.
# Original geometry; maps are baked from seeded numpy noise (no downloads).
# Origin: floor, centre of the back edge; front faces +Z.
#   blender -b --factory-startup --python-exit-code 1 \
#       -P ocean/swat/tools/blender/props_int_v2_water_cooler.py -- OUT_DIR [--render PNG] [--iter N]
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

reset()
rng = np.random.default_rng(1966)

W, D, H = .32, .32, .95          # cabinet
CZ = D / 2                       # cabinet centre z (back at z=0)

def tex_enamel(seed, base, n=256):
    r = np.random.default_rng(seed)
    y, x = np.mgrid[0:n, 0:n] / n
    nz = periodic_noise(r, n, 6)
    grime = periodic_noise(r, n, 3) * np.clip((1 - y) * 1.6 - .9, 0, 1)   # dirt near the bottom (image bottom)
    rgb = np.ones((n, n, 3)) * srgb(base)
    rgb *= (.94 + .08 * nz)[..., None]
    rgb *= (1 - .35 * grime)[..., None]
    # Scuffs: a few dark specks.
    for _ in range(6):
        cx, cy = r.integers(0, n, 2); rr = r.integers(1, 3)
        rgb[max(cy - rr, 0):cy + rr, max(cx - rr, 0):cx + rr] *= .85
    return rgb

enamel = to_image("cooler_enamel", tex_enamel(7, (206, 192, 162)))
m_body = mat("cooler_body", image=enamel, rough=.45)
m_chrome = mat("cooler_chrome", (.78, .78, .76), rough=.22, metal=1.0)
m_dark = mat("cooler_dark", (.05, .05, .05), rough=.6)
m_bottle = mat("cooler_bottle", (.05, .18, .30), rough=.04, alpha=1.0)
m_tap = mat("cooler_tap", (.88, .88, .86), rough=.35)
m_knob_c = mat("cooler_knob_c", (.15, .35, .75), rough=.4)
m_cups = mat("cooler_cups", (.92, .92, .88), rough=.8)

p = Part()
RX, RY0, RY1, RD = .105, .54, .89, .06     # front recess half-width, bottom, top, depth
# Cabinet shell around a tall front recess (rounded vertical edges).
p.add(bm_box((0, (RY0 + .02) / 2, CZ), (W / 2, (RY0 - .02) / 2, D / 2)), m_body, uv_scale=1.0)
for sx in (-1, 1):
    cx = sx * (RX + (W / 2 - RX) / 2)
    p.add(bm_box((cx, (RY0 + H) / 2, CZ), ((W / 2 - RX) / 2, (H - RY0) / 2, D / 2)), m_body, uv_scale=1.0)
p.add(bm_box((0, (RY1 + H) / 2, CZ), (RX, (H - RY1) / 2, D / 2)), m_body, uv_scale=1.0)
p.add(bm_box((0, (RY0 + RY1) / 2, (D - RD) / 2), (RX, (RY1 - RY0) / 2, (D - RD) / 2)), m_body, uv_scale=1.0)
# Rounded top cap with a small overhang.
p.add(bm_box((0, H - .008, CZ), (W / 2 + .004, .009, D / 2 + .004), bevel=.006, seg=2), m_body, uv_scale=1.0)
# Rounded upper corners of the recess: fillet prisms (corner square minus
# the quarter circle), r = 4 cm.
def fillet(sx, r=.04, seg=6):
    bm = bmesh.new()
    cx, cy = sx * (RX - r), RY1 - r
    pts = [(sx * RX, RY1)] + [(cx + sx * r * math.cos(a), cy + r * math.sin(a))
                              for a in [math.pi / 2 * k / seg for k in range(seg + 1)]]
    z0, z1 = D - RD, D
    lo = [bm.verts.new(E(x, y, z0)) for x, y in pts]; hi = [bm.verts.new(E(x, y, z1)) for x, y in pts]
    bm.faces.new(hi if sx > 0 else list(reversed(hi)))
    bm.faces.new(list(reversed(lo)) if sx > 0 else lo)
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        f = bm.faces.new((lo[i], lo[j], hi[j], hi[i]) if sx > 0 else (hi[i], hi[j], lo[j], lo[i]))
    bm.normal_update()
    return bm
for sx in (-1, 1):
    p.add(fillet(sx), m_body, uv_scale=1.0)
# Recessed kick plinth.
p.add(bm_box((0, .012, CZ), (W / 2 - .012, .012, D / 2 - .012)), m_dark)
# Bottle seat: shallow chrome ring.
p.add(bm_cyl((0, H + .008, CZ), .09, .016, seg=24), m_chrome)
# Drip grille at the bottom of the recess.
for k in range(5):
    p.add(bm_box((0, RY0 + .006, D - RD + .008 + k * .01), (RX - .004, .004, .003)), m_chrome)
# Two push faucets (cold / hot) with knobs.
for sx, knob in ((-1, m_knob_c), (1, m_tap)):
    fx = sx * .045
    p.add(bm_cyl((fx, .80, D - RD + .022), .014, .045, seg=12, axis='z'), m_tap)
    p.add(bm_cyl((fx, .775, D - RD + .04), .007, .035, seg=8), m_tap)
    p.add(bm_box((fx, .83, D - RD + .04), (.012, .014, .01), bevel=.004), knob)
# Name plate band below the recess.
p.add(bm_box((0, RY0 + .035, D - RD + .002), (.09, .025, .002)), m_tap)
# Paper-cup dispenser on the right side.
p.add(bm_cyl((W / 2 + .04, .78, CZ + .06), .038, .24, seg=16), m_chrome)
p.add(bm_cyl((W / 2 + .04, .64, CZ + .06), .036, .05, r2=.03, seg=16), m_cups)
p.add(bm_box((W / 2 + .01, .78, CZ + .06), (.012, .05, .015)), m_chrome)
# Inverted 5-gallon bottle: neck down into the collar, ribbed body, shoulder.
prof = [(.03, 0), (.03, .045), (.07, .075), (.14, .11), (.148, .135)]
for k in range(4):
    y0 = .135 + k * .07
    prof += [(.148, y0 + .008), (.141, y0 + .016), (.141, y0 + .05), (.148, y0 + .062)]
prof += [(.148, .43), (.135, .455), (.06, .465), (0.0, .468)]
bottle = bm_lathe(prof, c=(0, H - .025, CZ), seg=28, cap_bottom=False, cap_top=False)
p.add(bottle, m_bottle)
export("water_cooler", p)
# Reference: "Watercooler Wikinews.jpg" (Jason Pratt, CC BY 1.0): low camera
# right of the front, looking slightly up; tall portrait frame.
REF_VIEW = {"cam": (.75, .5, 2.6), "target": (0, .72, .16), "fov": 34, "res": (520, 1000),
            "room": {"box": (-1.2, 1.2, 0, 2.5, 2.6), "walls": ["back", "left"], "wall": (.75, .72, .70), "floor": (.35, .3, .25)},
            "lights": [((-.6, 2.3, 1.8), 220, (1, .96, .9), 1.2), ((1.6, 1.2, 1.5), 90, (1, 1, 1), 1.5)]}
preview(REF_VIEW)
