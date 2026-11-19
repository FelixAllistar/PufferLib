# Briar Court interior props v2: stamped gold-anodised aluminium motel
# advertising ashtray with two ribbed cigarette rests and a couple of butts.
# Reference: "Blue Bird Motel and Restaurant Ashtray - Valparaiso, Indiana" by
# Shook Photos, Flickr, CC BY 2.0 (https://www.flickr.com/photos/24724221@N07/32572608418).
# The printed band here is abstract glyph blocks, not real lettering or names.
#
#   blender -b --factory-startup --python-exit-code 1 -P props_int_v2_ashtray.py -- OUT_DIR [--render PNG]
#
# Tabletop piece: origin at the centre of its base (y = 0 on the support).
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

P = dict(R=.067, bowl=.046, depth=.014, H=.020, tab=.078, tab_w=.026,
         gold=(.72, .58, .27), ink=(.03, .03, .02))
reset()
rng = np.random.default_rng(1964)
S = .16  # texture spans 16 cm square, centred
n = 512
yy, xx = (np.mgrid[0:n, 0:n] + .5) / n
X = (xx - .5) * S; Z = (yy - .5) * S
r = np.sqrt(X * X + Z * Z); a = np.arctan2(Z, X)
base = np.ones((n, n, 3)) * np.array(P["gold"])
spark = periodic_noise(rng, n, 40, 2)
base *= (.92 + .12 * spark)[..., None]
# Printed band: rows of block glyphs on arcs (upper arc: two lines, lower arc: two lines).
ink = np.zeros((n, n))
def arc_text(r0, r1, a0, a1, seed, glyph=.028, gap=.18):
    g = np.random.default_rng(seed)
    t = (a - a0) / (a1 - a0)
    inside = (r > r0) & (r < r1) & (t > 0) & (t < 1)
    k = np.floor(t / glyph * (a1 - a0) / (a1 - a0))
    count = int(1 / glyph) + 1
    on = g.random(count) > gap
    hgt = g.uniform(.55, 1.0, count)
    kk = np.clip((t / glyph).astype(int), 0, count - 1)
    frac = t / glyph - kk
    rr = (r - r0) / (r1 - r0)
    return inside & on[kk] & (frac > .12) & (frac < .88) & (rr < hgt[kk])
for (r0, r1, a0, a1, seed) in [(.057, .066, -2.7, -.45, 1), (.052, .056, -2.3, -.85, 2),
                               (.058, .066, .55, 2.6, 3), (.052, .057, .9, 2.25, 4)]:
    ink = np.maximum(ink, arc_text(r0, r1, a0, a1, seed))
rgb = base * (1 - ink[..., None]) + np.array(P["ink"]) * ink[..., None]
# Scuffs: the print wears through on the rim; a few ash stains in the bowl.
wear = periodic_noise(rng, n, 25, 3) > .78
rgb = np.where((wear & (ink > 0))[..., None], base, rgb)
ash = (periodic_noise(rng, n, 30, 3) > .80) & (r < P["bowl"] * .8)
rgb = rgb * np.where(ash, .75, 1.0)[..., None]
img = to_image("ashtray_print", rgb[::-1])
tin = mat("ashtray_gold", image=img, rough=.45, metal=.6)
paper = mat("butt_paper", (.86, .84, .78), .8)
filt = mat("butt_filter", (.72, .48, .22), .8)
char = mat("butt_char", (.10, .09, .08), .9)

part = Part()
R, B, Dp, H = P["R"], P["bowl"], P["depth"], P["H"]
# Pressed dish: outer wall, flat rim, rounded bowl (surface of revolution).
prof = [(R - .004, 0.0), (R, .004), (R, H - .003), (R - .003, H), (B + .004, H), (B + .001, H - .002), (B - .001, H - .008)]
for k in range(1, 7):
    t = k / 6
    prof.append(((B - .001) * (1 - t) ** 1.3 + .002, H - .008 - (Dp - .004) * math.sin(t * math.pi / 2)))
prof.append((0.0005, H - .004 - Dp))
bm = bm_lathe(prof, seg=48)
part.add(bm, tin, uv_mode="top", uv_scale=1 / S, uv_offset=(.5, .5))
# Underside disc so the dish is closed from below.
part.add(bm_cyl((0, .001, 0), R - .004, .002, seg=48), tin, uv_mode="top", uv_scale=1 / S, uv_offset=(.5, .5))
# Two ribbed cigarette rests across the rim (ends overhang the dish).
for sx in (-1, 1):
    cx = sx * (R - .006)
    for b in [bm_box((cx, H - .002, 0), (.020, .005, P["tab_w"] / 2), bevel=.003, seg=2)] + \
             [bm_cyl((cx, H + .0035, gz), .0026, .038, seg=8, axis='x') for gz in (-.006, .006)]:
        part.add(b, tin, uv_mode="top", uv_scale=1 / S, uv_offset=(.5, .5))
# Butts: one stubbed in the bowl, one resting in a groove.
def butt(c, yaw, pitch, length=.030):
    for (x0, x1, m) in ((0, .020, filt), (.020, length, paper), (length, length + .003, char)):
        b = bm_cyl(((x0 + x1) / 2, 0, 0), .0039, x1 - x0, seg=10, axis='x')
        tilt_bm(b, pitch, 'z')
        rotate_bm(b, yaw)
        bmesh.ops.translate(b, vec=E(*c) - E(0, 0, 0), verts=b.verts)
        part.add(b, m, uv_scale=40)
butt((-.012, H - .004 - Dp + .005, .010), .7, .18)
butt((.040, H + .008, .0), math.pi, 0.0, .026)
ob = export("ashtray", part)

if RENDER:
    ob.location = E(0, 0, 0)
    bpy.data.objects["ashtray"].location = E(0, 0, 0)
    preview({"cam": (0, .48, 0.0001), "target": (0, 0, 0), "fov": 22,
             "room": {"box": (-.5, .5, -.5, .5, 1.0), "walls": [], "floor": (.01, .01, .01)},
             "lights": [((-.25, .28, -.18), 5, (1, .97, .92), .2), ((.2, .4, .2), 1.5, (1, 1, 1), .3)],
             "ambient": .15, "sky": (.5, .5, .5), "res": (960, 820)})
