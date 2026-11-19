# Briar Court interior props v2: two-seat tandem waiting chairs (reception).
# Moulded seat shells with avocado vinyl upholstery on a chrome beam carried
# by two T-legs, after a classic waiting-room beam seat. Vinyl: Poly Haven
# CC0 "fabric_leather_02" (tinted); everything else is plain PBR factors.
# Origin: floor, centre of the back edge; front faces +Z.
#   blender -b --factory-startup --python-exit-code 1 \
#       -P ocean/swat/tools/blender/props_int_v2_waiting_chairs.py -- OUT_DIR [--render PNG] [--iter N]
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

reset()
W, D, H, SEAT = 1.15, .60, .82, .44
SW = .47                     # one shell's width
PITCH = .58                  # seat spacing
leather = polyhaven("fabric_leather_02", "diff", "1k", size=256)
# Desaturate the brown leather and grade it to faded avocado vinyl.
_px = np.array(leather.pixels[:], dtype=np.float32).reshape(leather.size[1], leather.size[0], 4)
_lum = (_px[:, :, :3] @ np.array([.3, .59, .11], np.float32))
_lum = (_lum - _lum.mean()) * 1.6 + .5
# In-game iteration 1: darker, greyer avocado so the shells sit in the dim lobby.
vinyl_img = to_image("vinyl_avocado", np.clip(_lum[..., None] * srgb((112, 116, 66)) * 1.45, 0, 1)[::-1])
m_vinyl = mat("chair_vinyl", image=vinyl_img, rough=.42)
m_rim = mat("chair_rim", (.30, .36, .16), rough=.35)
m_chrome = mat("chair_chrome", (.80, .80, .78), rough=.2, metal=1.0)
m_dark = mat("chair_dark", (.05, .05, .05), rough=.7)

def shell(cx):
    """Seat + back as one curved sheet: profile along (y, z), rounded plan corners."""
    prof = []
    # Front roll -> seat pan -> cove -> back (leaning ~12 deg).
    for k in range(6):                                  # front roll
        a = math.pi * (.5 + .5 * k / 5)
        prof.append((SEAT - .05 + .05 * math.sin(a), D - .09 + .05 * math.cos(a) + .05))
    for k in range(1, 5):                               # pan, slightly dished
        t = k / 5
        prof.append((SEAT + .012 * math.sin(math.pi * t) * -1 + .005 * t, D - .09 - t * .34))
    for k in range(1, 6):                               # cove
        a = math.pi / 2 * k / 5
        prof.append((SEAT + .08 - .08 * math.cos(a), D - .43 - .08 * math.sin(a) + .0))
    for k in range(1, 6):                               # back
        t = k / 5
        prof.append((SEAT + .08 + t * (H - SEAT - .08), D - .51 - t * .06 - .02 * t * t))
    # Arc-length parametrisation of the profile.
    L = [0.0]
    for i in range(1, len(prof)):
        L.append(L[-1] + math.hypot(prof[i][0] - prof[i - 1][0], prof[i][1] - prof[i - 1][1]))
    Ltot = L[-1]
    def at(l):
        l = min(max(l, 0), Ltot)
        for i in range(1, len(L)):
            if l <= L[i] + 1e-9:
                f = (l - L[i - 1]) / max(L[i] - L[i - 1], 1e-9)
                return (prof[i - 1][0] + f * (prof[i][0] - prof[i - 1][0]), prof[i - 1][1] + f * (prof[i][1] - prof[i - 1][1]))
    R_TOP, R_FRONT = .10, .05
    def fn(u, v):
        sx, l = (u - .5) * SW, v * Ltot
        # Round the four plan corners of the unrolled sheet.
        for lc, r in ((Ltot - R_TOP, R_TOP), (R_FRONT, R_FRONT)):
            ex = abs(sx) - (SW / 2 - r)
            el = (l - lc) if lc > Ltot / 2 else (lc - l)
            if ex > 0 and el > 0:
                d = math.hypot(ex, el)
                if d > r:
                    ex, el = ex * r / d, el * r / d
                    sx = math.copysign(SW / 2 - r + ex, sx)
                    l = lc + el if lc > Ltot / 2 else lc - el
        y, z = at(l)
        return (cx + sx, y, z)
    bm = bm_grid(12, 28, fn)
    # Rolled rim (piping) around the shell's edge.
    ring = [fn(i / 12, 0) for i in range(13)] + [fn(1, j / 28) for j in range(1, 29)] + \
           [fn(1 - i / 12, 1) for i in range(1, 13)] + [fn(0, 1 - j / 28) for j in range(1, 29)]
    clean = [ring[0]]
    for q in ring[1:]:
        if math.dist(q, clean[-1]) > .004: clean.append(q)
    shell.rims.append(clean)
    bmesh.ops.solidify(bm, geom=list(bm.faces), thickness=.035)
    return bm

shell.rims = []
p = Part()
for i in (-1, 1):
    cx = i * PITCH / 2
    p.add(shell(cx), m_vinyl, uv_scale=3.0)
    rim = shell.rims[-1]
    p.add(bm_tube(rim + [rim[0]], .014, seg=6, cap=False), m_rim)
    # Seat bracket from the shell to the beam.
    p.add(bm_box((cx, .395, D - .30), (.025, .025, .10)), m_dark)
# Beam: round chrome tube right under the seats.
p.add(bm_cyl((0, .36, D - .30), .024, W - .06, seg=16, axis='x'), m_chrome)
# T-legs: post and floor foot, with glides.
for sx in (-1, 1):
    lx = sx * (W / 2 - .17)
    p.add(bm_cyl((lx, .185, D - .30), .024, .35, seg=14), m_chrome)
    p.add(bm_cyl((lx, .022, D - .30), .02, .54, seg=12, axis="z"), m_chrome)
    for gz in (-1, 1):
        p.add(bm_cyl((lx, .004, D - .30 + gz * .25), .018, .008, seg=10), m_dark)

export("waiting_chairs", p)
# Reference: beam seating (bfick, Flickr 3214261837, CC BY 2.0): straight-on
# frontal view at seat height.
REF_VIEW = {"cam": (0, .95, 3.0), "target": (0, .42, .3), "fov": 30, "res": (900, 684),
            "room": {"box": (-2, 2, 0, 3.5, 2.6), "walls": ["back"], "wall": (.85, .55, .12), "floor": (.45, .22, .12)},
            "lights": [((.0, 2.2, 2.2), 260, (1, .95, .85), 1.2), ((-1.5, 1.0, 2.5), 60, (1, 1, 1), 1.5)]}
preview(REF_VIEW)
