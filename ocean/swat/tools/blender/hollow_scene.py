# The Hollow: site scenery GLBs (assets/environment/hollow_v1/), engine world
# space (metres, +Y up), drawn at identity by hollow_art.h. Deterministic.
#
#   blender -b --factory-startup --python-exit-code 1 -P hollow_scene.py -- OUT_DIR [--only terrain,woods,house_trim,outbuildings_trim]
#
# - hollow_terrain: the hollow around the playable clearing (valley walls rise
#   on three sides, the hill behind the cellar bank), the gravel lane out to the
#   south-west and a creek in a trough beyond the west edge.
# - hollow_woods: low-poly trunks and crowns, a ring of late-autumn woods.
# - hollow_house_trim / hollow_outbuildings_trim: battens over the board joints
#   (the same column layout as hollow_impl.h hollow_wall_build), corner boards,
#   six-over-six sash bars, a field-stone skirt, ridge caps, the barn's open
#   sliding door. Presentation only; collision is the box map.
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import props_int_v2_common as C
from props_int_v2_common import Part, bm_box, bm_cyl, bm_sphere, bm_tube, bm_grid, rotate_bm, tilt_bm, soften, mat
import numpy as np

ONLY = set(C.ARGS[C.ARGS.index("--only") + 1].split(",")) if "--only" in C.ARGS else None
RECT = (-8.0, 57.0, -24.0, 41.0)      # playable ground box (x0, x1, z0, z1), top y = 0
BANK_Z, BANK_Y = 12.0, 2.2            # hill bank north of the yard
LANE = [(-95, -55), (-60, -40), (-32, -29), (-14, -22.5), (-3, -19.5), (5, -16.5), (10.5, -12.5), (14.5, -9.0)]
CREEK_X = -17.0

def smooth(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0), 1); return t * t * (3 - 2 * t)

def seg_dist(px, pz, pts):
    best = 1e9
    for (ax, az), (bx, bz) in zip(pts, pts[1:]):
        dx, dz = bx - ax, bz - az; t = max(0, min(1, ((px - ax) * dx + (pz - az) * dz) / (dx * dx + dz * dz)))
        best = min(best, math.hypot(px - ax - t * dx, pz - az - t * dz))
    return best

_rng = np.random.default_rng(1938)
_waves = [(_rng.uniform(.02, .09), _rng.uniform(0, 2 * math.pi), _rng.uniform(0, 2 * math.pi)) for _ in range(6)]
def wobble(x, z):
    return sum(math.sin(f * x + p) * math.cos(f * .8 * z + q) for f, p, q in _waves) / 3

def height(x, z):
    x0, x1, z0, z1 = RECT
    base = BANK_Y * smooth(BANK_Z - .5, BANK_Z + .5, z)
    if x0 <= x <= x1 and z0 <= z <= z1:
        return base - .05  # under the authoritative ground and bank boxes
    dx = max(x0 - x, 0, x - x1); dz = max(z0 - z, 0, z - z1); d = math.hypot(dx, dz)
    rise = .30 * d + .010 * d * d + (.55 * d if z > z1 else 0)   # steeper behind the cellar hill
    rise = 38 * math.tanh(rise / 38)                                 # ridgelines level off
    h = base + rise + 2.2 * wobble(x, z) * smooth(0, 12, d)
    lane = seg_dist(x, z, LANE)
    h = h * smooth(4, 16, lane) + (base - .05) * (1 - smooth(4, 16, lane)) * (0 if lane > 16 else 1) + (h * 0 if lane < 4 else 0)
    if lane < 16: h = min(h, base + .25 * max(0, lane - 3))
    creek = abs(x - CREEK_X)
    h -= 1.4 * math.exp(-(creek / 3.0) ** 2) * smooth(z1 + 10, z1 - 5, z) if x < x0 else 0
    return h

def axis_samples(lo, hi, a, b, coarse, fine):
    return sorted(set(np.round(np.concatenate([np.linspace(lo, a, coarse), np.linspace(a, b, fine), np.linspace(b, hi, coarse)]), 3)))

def terrain(m):
    p = Part()
    xs = axis_samples(-120, 175, RECT[0] - 1, RECT[1] + 1, 34, 14)
    zs = axis_samples(-130, 165, RECT[2] - 1, RECT[3] + 1, 34, 14)
    def f(u, v):
        x = xs[min(int(round(u * (len(xs) - 1))), len(xs) - 1)]; z = zs[min(int(round(v * (len(zs) - 1))), len(zs) - 1)]
        return (x, height(x, z), z)
    p.add(bm_grid(len(xs) - 1, len(zs) - 1, f), m["leaves"], uv_scale=.22)
    # Gravel lane: a strip just above the ground box and the flattened valley floor.
    pts = []
    for (ax, az), (bx, bz) in zip(LANE, LANE[1:]):
        n = max(2, int(math.hypot(bx - ax, bz - az) / 2.5))
        for k in range(n):
            t = k / n; pts.append((ax + (bx - ax) * t, az + (bz - az) * t))
    pts.append(LANE[-1])
    def lane(u, v):
        i = min(int(u * (len(pts) - 1)), len(pts) - 2); t = u * (len(pts) - 1) - i
        ax, az = pts[i]; bx, bz = pts[i + 1]; x = ax + (bx - ax) * t; z = az + (bz - az) * t
        dx, dz = bx - ax, bz - az; L = math.hypot(dx, dz); nx, nz = -dz / L, dx / L
        w = (v - .5) * (3.4 if x > -20 else 3.0); x += nx * w; z += nz * w
        y = max(height(x, z), 0) + .018 if not (RECT[0] <= x <= RECT[1] and RECT[2] <= z <= RECT[3]) else .014
        return (x, y, z)
    p.add(bm_grid(len(pts) - 1, 2, lane), m["gravel"], uv_scale=.45)
    # Turnaround at the lane end (staging) and wheel-worn dirt into the yard.
    disc = bm_grid(16, 16, lambda u, v: (-1.5 + 9 * (u - .5) * math.cos(.4) - 6 * (v - .5) * math.sin(.4), .016,
                                          -18.6 + 9 * (u - .5) * math.sin(.4) + 6 * (v - .5) * math.cos(.4)))
    p.add(disc, m["gravel"], uv_scale=.45)
    # Dead grass: rank tufts over the unkempt yard, thick along walls and the
    # woods edge, none on the lane, the paths, the drag marks or inside buildings.
    import bmesh
    rng = np.random.default_rng(77)
    keep_out = [(13.4, 22.6, -8.6, 3.8), (31.4, 41.6, -8.6, 4.4), (23.8, 27.2, 2.8, 6.2), (5.8, 8.7, 1.8, 5.2),
                (16.8, 19.2, 3.4, 12.1), (-1.2, 1.7, -4.3, 6.3), (-5.5, 1.5, -21.5, -16.0), (26.8, 30.2, -12.2, -8.8)]
    tuft = bmesh.new(); tufts = 0
    for k in range(4200):
        x = rng.uniform(RECT[0], RECT[1]); z = rng.uniform(RECT[2], BANK_Z - .2)
        if any(a0 <= x <= a1 and b0 <= z <= b1 for (a0, a1, b0, b1) in keep_out): continue
        if seg_dist(x, z, LANE) < 2.2: continue
        edge = min(x - RECT[0], RECT[1] - x, z - RECT[2], BANK_Z - z)
        if rng.random() > .25 + .75 * math.exp(-edge / 5): continue
        hgt = rng.uniform(.25, .65) * (1.3 if edge < 3 else 1)
        for j in range(7):  # thin blades fanning out from a clump
            a = rng.uniform(0, math.pi); w = rng.uniform(.012, .025); lean = rng.normal(0, .12, 2)
            x0, z0 = x + rng.normal(0, .05), z + rng.normal(0, .05)
            ca, sa = math.cos(a) * w, math.sin(a) * w
            hb = hgt * rng.uniform(.6, 1.1)
            v = [tuft.verts.new(C.E(x0 - ca, .005, z0 - sa)), tuft.verts.new(C.E(x0 + ca, .005, z0 + sa)),
                 tuft.verts.new(C.E(x0 + lean[0] * hb * 2, hb, z0 + lean[1] * hb * 2))]
            tuft.faces.new(v)
        tufts += 1
    print("grass tufts", tufts)
    p.add(tuft, m["straw"])
    # Creek: dark still water in the trough beyond the west edge.
    def water(u, v):
        z = -130 + 300 * u; x = CREEK_X + (v - .5) * 3.2 + 1.5 * math.sin(z * .07)
        return (x, -1.05, z)
    p.add(bm_grid(60, 1, water), m["water"])
    return p

def woods(m):
    p = Part(); rng = np.random.default_rng(1935)
    x0, x1, z0, z1 = RECT
    trees = []
    for gx in np.arange(-48, 100, 4.3):
        for gz in np.arange(-62, 82, 4.3):
            x = gx + rng.uniform(-1.4, 1.4); z = gz + rng.uniform(-1.4, 1.4)
            dx = max(x0 - x, 0, x - x1); dz = max(z0 - z, 0, z - z1); d = math.hypot(dx, dz)
            inside = d == 0
            if inside:
                # The bank top behind the cellar is wooded; the yard is open.
                if not (z > BANK_Z + 2.5 and not (13 < x < 23 and z < 21)) or rng.random() < .35: continue
            elif d < 1.2 or d > 32 or rng.random() < smooth(12, 32, d) * .75: continue
            if seg_dist(x, z, LANE) < 4.5 or abs(x - CREEK_X) < 2.5 and x < x0: continue
            trees.append((x, z))
    bark = m["bark"]; crowns = [m["crown_a"], m["crown_b"], m["crown_c"]]
    for (x, z) in trees:
        y = height(x, z) if not (x0 <= x <= x1 and z0 <= z <= z1) else BANK_Y
        hemlock = rng.random() < .28
        hgt = rng.uniform(13, 22); r = rng.uniform(.14, .32)
        lean = rng.normal(0, .04)
        trunk = bm_cyl((x, y + hgt * .5, z), r, hgt, 5, r2=r * .35, smooth=False, cap=False)
        p.add(tilt_bm(trunk, lean, 'x', (x, y, z)), bark, uv_scale=.6)
        if hemlock:
            for k in range(3):
                cy = y + hgt * (.42 + .2 * k); cr = (2.6 - .7 * k) * rng.uniform(.8, 1.1)
                p.add(bm_cyl((x, cy, z), cr, hgt * .28, 6, r2=.15, smooth=False, cap=False), m["hemlock"])
        else:
            mat_c = crowns[int(rng.integers(3))]
            for k in range(2):  # overlapping masses: the canopy reads as one dark roof of woods
                cy = y + hgt * rng.uniform(.5, .85); cr = rng.uniform(2.8, 4.4)
                ox, oz = rng.normal(0, 1.3, 2)
                p.add(soften(bm_sphere((x + ox, cy, z + oz), cr * 1.12, (1.0, .62, 1.0), 5, 3), rng, .35), mat_c if k else crowns[int(rng.integers(3))])
            # Bare upper limbs poking out: late-autumn deciduous.
            for k in range(2):
                a = rng.uniform(0, 2 * math.pi); L = rng.uniform(2, 3.5)
                b0 = (x, y + hgt * .6, z); b1 = (x + L * math.cos(a), y + hgt * .6 + L * .9, z + L * math.sin(a))
                p.add(bm_tube([b0, b1], r * .25, 4, cap=False), bark)
    # Understory along the woods edge: laurel and brush hide the trunk band.
    shrubs = 0
    for gx in np.arange(x0 - 14, x1 + 14, 2.6):
        for gz in np.arange(z0 - 14, z1 + 14, 2.6):
            x = gx + rng.uniform(-1, 1); z = gz + rng.uniform(-1, 1)
            dx = max(x0 - x, 0, x - x1); dz = max(z0 - z, 0, z - z1); d = math.hypot(dx, dz)
            if d < 1.0 or d > 10 or seg_dist(x, z, LANE) < 3.5 or rng.random() < .45: continue
            y = height(x, z)
            p.add(soften(bm_sphere((x, y + .7, z), rng.uniform(1.0, 1.8), (1.2, .6, 1.0), 5, 3), rng, .15), m["hemlock"] if rng.random() < .5 else crowns[2])
            shrubs += 1
    print("trees", len(trees), "shrubs", shrubs)
    return p

# ----------------------------------------------------------------- trim
BATTEN = .03; BATTEN_T = .02
def joints(a, b, openings, board):
    """Board joints along a wall from a to b (world axis coords), as in
    hollow_wall_build: spans between openings are split into equal boards;
    each opening column is its own span (boards above/below it)."""
    ops = sorted(openings, key=lambda o: o[0] - o[1] / 2)
    out = []  # (pos, y0, y1) relative heights; y1 None = full height
    at = a
    def span(s, e, y0, y1):
        if e - s < .01: return
        n = max(1, math.ceil((e - s) / board - .15)); d = (e - s) / n
        for k in range(1, n): out.append((s + k * d, y0, y1))
    for (c, w, sill, top) in ops:
        lo, hi = c - w / 2, c + w / 2
        span(at, lo - .05, 0, None)
        if sill > .1: span(lo, hi, 0, sill - .04)
        if top is not None: span(lo, hi, top + .06, None)
        at = hi + .05
    span(at, b, 0, None)
    return out

def wall_battens(p, mm, x0, z0, x1, z1, y, h, out_sign, openings=(), board=.30):
    along_x = abs(x1 - x0) > abs(z1 - z0)
    a, b = (min(x0, x1), max(x0, x1)) if along_x else (min(z0, z1), max(z0, z1))
    ops = [(c, w, s, (s + hh) if hh and hh > 0 else None) for (c, w, s, hh) in openings]
    off = .0125 + BATTEN_T / 2 + .001
    rng = np.random.default_rng(int(abs(x0 * 131 + z0 * 17 + y * 7 + h * 3)))
    for (pos, ya, yb) in joints(a, b, ops, board):
        yb = h if yb is None else yb
        if yb - ya < .05: continue
        if rng.random() < .12: continue           # a few battens lost to weather
        bw = BATTEN * rng.uniform(.7, 1.3)
        ya += rng.uniform(0, .08) if ya == 0 else 0  # rotted short at the sill
        cy = y + (ya + yb) / 2; hy = (yb - ya) / 2
        mb = mm if rng.random() < .6 else M2["batten_dark"]
        if along_x: p.add(bm_box((pos, cy, z0 + out_sign * off), (bw, hy, BATTEN_T / 2)), mb)
        else: p.add(bm_box((x0 + out_sign * off, cy, pos), (BATTEN_T / 2, hy, bw)), mb)
    # Splash/rot band: the board feet darken unevenly where rain splashes and
    # the wood rots (each board its own height), only where boards reach the sill.
    if y < .5:
        n = max(1, int((b - a) / board))
        for k in range(n):
            t0 = a + (b - a) * k / n; t1 = a + (b - a) * (k + 1) / n; c = (t0 + t1) / 2
            if any(o[2] <= .1 and abs(c - o[0]) < o[1] / 2 + .05 for o in ops): continue
            hh = rng.uniform(.04, .26) / 2
            if along_x: p.add(bm_box((c, y + hh, z0 + out_sign * (.0125 + .004)), ((t1 - t0) / 2 - .004, hh, .003)), M2["rot"])
            else: p.add(bm_box((x0 + out_sign * (.0125 + .004), y + hh, c), (.003, hh, (t1 - t0) / 2 - .004)), M2["rot"])
    # Corner boards at both ends.
    for e in (a, b):
        if along_x: p.add(bm_box((e, y + h / 2, z0 + out_sign * (off + .005)), (.07, h / 2, .015)), mm)
        else: p.add(bm_box((x0 + out_sign * (off + .005), y + h / 2, e), (.015, h / 2, .07)), mm)

def sash(p, mm, x0, z0, along_x, center, width, sill_y, height, out_sign):
    """Six-over-six sash bars on the outside of a window opening."""
    off = .0125 + .012
    for k in (1, 2):  # vertical muntins
        pos = center - width / 2 + width * k / 3
        if along_x: p.add(bm_box((pos, sill_y + height / 2, z0 + out_sign * off), (.012, height / 2, .01)), mm)
        else: p.add(bm_box((x0 + out_sign * off, sill_y + height / 2, pos), (.01, height / 2, .012)), mm)
    for k in (1, 2, 3):  # meeting rail and horizontal bars
        yy = sill_y + height * k / 4
        t = .025 if k == 2 else .012
        if along_x: p.add(bm_box((center, yy, z0 + out_sign * off), (width / 2, t, .01)), mm)
        else: p.add(bm_box((x0 + out_sign * off, yy, center), (.01, t, width / 2)), mm)

F, U, UT, RIDGE, KH = .30, 2.90, 5.20, 7.0, 2.45
def gable(p, mm, along_x, fixed, a, b, y0, rise, out, battens=None, bm=None, board=.30):
    """Triangular board gable over a wall from a to b at `fixed`, eave y0."""
    import bmesh
    from mathutils import Vector
    mid = (a + b) / 2
    def P(t, y, o):
        return (t, y, fixed + o) if along_x else (fixed + o, y, t)
    g = bmesh.new()
    vs = [g.verts.new(C.E(*P(t, y, o))) for o in (-.0125, .0125) for (t, y) in ((a, y0), (b, y0), (mid, y0 + rise))]
    g.faces.new(vs[0:3]); g.faces.new(vs[3:6][::-1])
    for i in range(3):
        j = (i + 1) % 3; g.faces.new((vs[i], vs[j], vs[3 + j], vs[3 + i]))
    bmesh.ops.recalc_face_normals(g, faces=list(g.faces))
    p.add(g, mm, uv_scale=1 / 1.6)
    if battens:
        n = max(2, int((b - a) / board))
        for k in range(1, n):
            t = a + (b - a) * k / n; top = y0 + rise * (1 - abs(t - mid) / ((b - a) / 2))
            if top - y0 > .08:
                p.add(bm_box(P(t, (y0 + top) / 2, out * .025), ((.03, (top - y0) / 2, .01) if along_x else (.01, (top - y0) / 2, .03))), battens)

def house_trim(m):
    p = Part(); g = m["batten"]; s = m["batten_dark"]
    gh, uh = U - F, UT - U
    walls = [  # (x0,z0,x1,z1,y,h,out, openings[(at,width,sill,height)])
        (14, -6, 22, -6, F, gh, -1, [(16, .86, 0, 2.0), (14.9, .7, .85, 1.1), (20, .86, 0, 2.0), (21.2, .7, .85, 1.1)]),
        (14, 0, 19, 0, F, gh, 1, [(16, .82, 0, 2.0)]), (19, 0, 22, 0, F, gh, 1, []),
        (14, -6, 14, 0, F, gh, -1, [(-3.6, .7, .85, 1.1)]), (22, -6, 22, 0, F, gh, 1, []),
        (14, 3.5, 19, 3.5, F, KH, 1, [(17.6, .86, 0, 2.0), (15.3, .7, .85, 1.0)]),
        (14, 0, 14, 3.5, F, KH, -1, [(1.8, .7, .85, 1.0)]), (19, 0, 19, 3.5, F, KH, 1, []),
        (14, -6, 22, -6, U, uh, -1, [(15.4, .7, .75, 1.05), (17.0, .7, .75, 1.05), (19.6, .7, .75, 1.05), (21.2, .7, .75, 1.05)]),
        (14, 0, 22, 0, U, uh, 1, [(16, .7, .75, 1.05)]),
        (14, -6, 14, 0, U, uh, -1, [(-3, .6, .8, .9)]), (22, -6, 22, 0, U, uh, 1, [(-1.2, .6, .8, .9)]),
    ]
    for (x0, z0, x1, z1, y, h, out, ops) in walls:
        if (x0, z0, x1, z1) == (14, 0, 19, 0) or (x0, z0, x1, z1) == (19, 0, 22, 0):
            if y == F: continue  # inside the lean-to / behind the stair: no exterior face
        wall_battens(p, g, x0, z0, x1, z1, y, h, out, ops)
        along_x = abs(x1 - x0) > abs(z1 - z0)
        for (c, w, sill, hh) in ops:
            if sill > .1: sash(p, s, x0, z0, along_x, c, w, y + sill, hh, out)
    # Triangular board gables (east and west) with battens.
    for x, out in ((14, -1), (22, 1)):
        gable(p, m["grey"], False, x, -6, 0, UT, RIDGE - UT, out, g)
    # Field-stone skirt over the pier line (house and lean-to), porch piers.
    st = m["stone"]
    for (cx, cz, hx, hz) in ((18, -6.03, 4.05, .04), (18, -.0, 4.05, .04), (13.97, -3, .04, 3.05), (22.03, -3, .04, 3.05),
                             (16.5, 3.53, 2.55, .04), (13.97, 1.75, .04, 1.8), (19.03, 1.75, .04, 1.8)):
        if cz == 0: continue
        p.add(bm_box((cx, .15, cz), (hx, .15, hz)), st)
    for k in range(5):
        p.add(bm_box((14.15 + k * 1.92, .14, -7.95), (.14, .14, .14)), st)
    # Porch: rail-less edge board, ceiling boards visible under the tin.
    p.add(bm_box((18, 2.38, -7.88), (4.1, .07, .05)), g)                       # porch plate on the posts
    # Ridge cap and tin trims on the main roof; stovepipe through the lean-to.
    p.add(bm_box((18, RIDGE + .12, -3), (4.45, .05, .14)), m["tin"])
    for x in (13.75, 22.25):
        p.add(tilt_bm(bm_box((x, (UT + RIDGE) / 2 + .1, -4.7), (.03, .04, 1.95)), math.atan2(RIDGE - UT, 3.4), 'x', (x, (UT + RIDGE) / 2 + .1, -4.7)), g)
        p.add(tilt_bm(bm_box((x, (UT + RIDGE) / 2 + .1, -1.3), (.03, .04, 1.95)), -math.atan2(RIDGE - UT, 3.4), 'x', (x, (UT + RIDGE) / 2 + .1, -1.3)), g)
    # Chimney: corbelled top and a slumped, missing-stone notch suggested by a dark cap.
    p.add(bm_box((22.43, RIDGE + .5, -3), (.42, .06, .44)), st)              # cap course
    p.add(bm_box((22.43, RIDGE + .565, -3), (.2, .01, .2)), m["soot"])
    for (x1, y, z0, z1) in ((23.15, 2.3, -4.0, -2.0), (22.95, UT + .2, -3.65, -2.35)):  # sloped shoulders
        run = .35
        p.add(tilt_bm(bm_box((x1 - run / 2, y + .1, (z0 + z1) / 2), (run / 2 + .05, .06, (z1 - z0) / 2)), .6, 'z', (x1 - run, y, 0)), st)
    # The firebox has slumped open on the outside: a dark cavity in the base.
    p.add(bm_box((23.16, 1.05, -2.9), (.01, .55, .32)), m["soot"])
    # Windows boarded over from outside, like the Wolcott farmhouse's: scrap
    # boards nailed across (gaps left between them), slightly skewed.
    rng = np.random.default_rng(8)
    def boarded(along_x, x0, z0, c, w, sill_y, hh, out, n=3):
        off = .0125 + .045
        for k in range(n):
            y = sill_y + hh * (k + .5) / n; ang = rng.normal(0, .08)
            if along_x: bx = bm_box((c, y, z0 + out * off), (w / 2 + .12, .07, .012))
            else: bx = bm_box((x0 + out * off, y, c), (.012, .07, w / 2 + .12))
            p.add(tilt_bm(bx, ang, 'z' if along_x else 'x', (c if along_x else x0, y, z0 if along_x else c)), m["grey"])
    boarded(True, 0, -6, 15.4, .7, U + .75, 1.05, -1)
    boarded(False, 22, 0, -1.2, .6, U + .8, .9, 1, 2)
    boarded(False, 14, 0, 1.8, .7, F + .85, 1.0, -1)
    # Inside the east room: a plain board mantel and a soot-black firebox on the
    # chimney wall (HABS WV-0494 farmhouse room). Presentation only.
    fx = 22 - .0125 - .02
    p.add(bm_box((fx - .01, F + .5, -3), (.012, .5, .5)), m["soot"])                  # firebox
    for z in (-3.62, -2.38):
        p.add(bm_box((fx - .02, F + .62, z), (.03, .62, .1)), g)                       # pilasters
    p.add(bm_box((fx - .03, F + 1.18, -3), (.04, .07, .74)), g)                         # frieze
    p.add(bm_box((fx - .1, F + 1.27, -3), (.11, .025, .8)), g)                          # mantel shelf
    p.add(bm_box((fx - .25, F + .01, -3), (.25, .01, .6)), st)                          # hearth stones
    # Door hood over the west front door, like the Wolcott farmhouse.
    p.add(tilt_bm(bm_box((16, 2.62, -6.25), (.6, .02, .3)), .35, 'x', (16, 2.62, -6.0)), m["tin"])
    return p

def outbuildings_trim(m):
    p = Part(); g = m["batten"]
    for (x0, z0, x1, z1, h, out, ops) in ((24, 3, 27, 3, 2.2, -1, [(25.5, .78, 0, 1.95)]), (24, 6, 27, 6, 2.2, 1, []),
                                          (24, 3, 24, 6, 2.2, -1, []), (27, 3, 27, 6, 2.2, 1, []),
                                          (8.5, 2, 8.5, 5, 2.2, 1, [(3.5, .8, 0, 1.98)]), (6, 2, 6, 5, 2.0, -1, []),
                                          (6, 2, 8.5, 2, 2.0, -1, []), (6, 5, 8.5, 5, 2.0, 1, [])):
        wall_battens(p, g, x0, z0, x1, z1, 0, h, out, ops)
    # Barn: wide boards, battens, and the big sliding door hanging open on its track.
    for (x0, z0, x1, z1, out, ops) in ((32, -8, 41, -8, -1, [(36.5, 3.0, 0, 3.2)]), (32, -8, 32, 4, -1, [(-5.5, .9, 0, 2.0), (1.5, .6, 1.6, .6)]),
                                       (41, -8, 41, 4, 1, [(-2, .6, 1.6, .6)]), (32, 4, 41, 4, 1, [(36.5, 1.2, 2.9, 1.0)])):
        wall_battens(p, g, x0, z0, x1, z1, 0, 4.2, out, ops, .36)
    for z, out in ((-8, -1), (4, 1)):
        gable(p, m["grey"], True, z, 32, 41, 4.2, 4.0, out, g, board=.36)
    for z, out in ((3, -1), (6, 1)):
        gable(p, m["grey"], True, z, 24, 27, 2.2, 1.2, out, g)
    p.add(bm_box((36.5, 3.25, -8.08), (3.4, .04, .03)), m["rust"])            # door track
    p.add(bm_box((39.55, 1.6, -8.12), (1.55, 1.6, .03)), m["grey"])            # slid-open leaf
    for k in range(4):
        p.add(bm_box((38.1 + k * .95, 1.6, -8.155), (.03, 1.6, .01)), g)
    p.add(tilt_bm(bm_box((39.55, 1.6, -8.155), (1.5, .04, .01)), .82, 'z', (39.55, 1.6, -8.155)), g)  # Z brace
    # Barn ridge cap and a hay hood over the loft door.
    p.add(bm_box((36.5, 8.32, -2), (.15, .05, 6.75)), m["tin"])
    p.add(tilt_bm(bm_box((36.5, 4.25, 4.35), (.9, .02, .45)), -.4, 'x', (36.5, 4.25, 4.1)), m["tin"])
    # Cellar headwall: rough stone arch face and a timber lintel; grass lip on the bank.
    p.add(bm_box((18, 2.05, 11.95), (.6, .1, .08)), m["grey"])
    for k in range(9):
        a = math.pi * k / 8
        p.add(bm_box((18 + .62 * math.cos(a), 2.0 + .35 * math.sin(a), 11.94), (.12, .09, .06)), m["stone"])
    # Smokehouse ridge cap and a rusted vent; shed ridge.
    p.add(bm_box((25.5, 3.48, 4.5), (.12, .04, 1.85)), m["tin"])
    p.add(bm_cyl((25.5, 3.7, 4.5), .07, .4, 8), m["rust"])
    return p

M2 = {}
def cellar_trim(m):
    """Root cellar: a segmental limewashed barrel vault under the timber
    ceiling (springing 1.95 m above the floor, crown just under the timbers),
    iron hooks in the crown, damp streaks; the vault runs north-south."""
    p = Part(); x0, x1, z0, z1 = 16.5, 20.5, 14.5, 18.5
    spring, crown = .50, .93
    half = (x1 - x0) / 2; sag = crown - spring
    R = (half * half + sag * sag) / (2 * sag); cy = crown - R
    def vault(u, v):
        x = x0 + (x1 - x0) * u; dx = x - (x0 + half)
        return (x, cy + math.sqrt(max(R * R - dx * dx, 0)), z0 + (z1 - z0) * v)
    import bmesh
    vb = bm_grid(16, 4, vault)
    vb.normal_update()
    if sum(f.normal.z for f in vb.faces) > 0:  # face down, toward the bulb
        bmesh.ops.reverse_faces(vb, faces=list(vb.faces))
    p.add(vb, m["lime"], uv_scale=.5)
    for (xa, xb) in ((x0, x0 + .02), (x1 - .02, x1)):  # spring course
        p.add(bm_box(((xa + xb) / 2, spring - .05, (z0 + z1) / 2), (.03, .05, (z1 - z0) / 2)), m["lime"])
    for k, z in enumerate((15.6, 16.6, 17.6)):  # hooks in the crown
        x = 18.5 + (.3 if k % 2 else -.3)
        p.add(bm_tube([(x, crown - .01, z), (x, crown - .18, z), (x + .05, crown - .24, z), (x + .09, crown - .17, z)], .008, 5), m["rust"])
    return p

def palette():
    ph = C.polyhaven
    return {
        "leaves": mat("forest_floor", (1, 1, 1), .95, image=ph("dry_decay_leaves", "diff", "1k", 1024), normal=ph("dry_decay_leaves", "nor_gl", "1k", 512)),
        "gravel": mat("gravel", (1, 1, 1), .9, image=C.tint_image(ph("gravel_floor_02", "diff", "1k", 1024), "gravel_graded", (.5, .5, .48)), normal=ph("gravel_floor_02", "nor_gl", "1k", 512)),
        "water": mat("creek", (.015, .02, .02), .08, .0),
        "straw": mat("dead_grass", (.13, .11, .065), .95),
        "bark": mat("bark", (1, 1, 1), .95, image=ph("bark_brown_02", "diff", "1k", 512)),
        "crown_a": mat("crown_olive", (.075, .075, .025), .9),
        "crown_b": mat("crown_rust", (.15, .055, .018), .9),
        "crown_c": mat("crown_brown", (.10, .065, .025), .9),
        "hemlock": mat("hemlock", (.03, .06, .04), .9),
        "batten": mat("batten_grey", (1, 1, 1), .9, image=ph("wood_planks_grey", "diff", "1k", 512)),
        "sashwood": mat("sash", (.20, .19, .17), .8),
        "wood": mat("old_wood", (1, 1, 1), .85, image=ph("old_planks_02", "diff", "1k", 512)),
        "grey": mat("grey_boards", (1, 1, 1), .9, image=ph("wood_planks_grey", "diff", "1k", 512), normal=ph("wood_planks_grey", "nor_gl", "1k", 512)),
        "stone": mat("stone", (1, 1, 1), .95, image=ph("old_stone_wall", "diff", "1k", 512)),
        "tin": mat("tin", (1, 1, 1), .7, .4, image=ph("rusty_corrugated_iron", "diff", "1k", 512)),
        "rust": mat("rust", (1, 1, 1), .8, .3, image=ph("rusty_metal_02", "diff", "1k", 512)),
        "soot": mat("soot", (.02, .02, .02), .9),
        "lime": mat("limewash", (.78, .76, .70), .95, image=C.tint_image(ph("old_stone_wall", "diff", "1k", 512), "limewash_tex", (.5, .5, .48), (.45, .44, .41))),
        "rot": mat("board_rot", (1, 1, 1), .95, image=C.tint_image(ph("wood_planks_grey", "diff", "1k", 512), "planks_rot", (.68, .62, .54))),
        "batten_dark": mat("batten_dark", (.62, .6, .56), .9, image=ph("wood_planks_grey", "diff", "1k", 512)),
    }

if __name__ == "__main__":
    os.makedirs(C.OUT, exist_ok=True)
    for name, fn in (("terrain", terrain), ("woods", woods), ("house_trim", house_trim), ("outbuildings_trim", outbuildings_trim), ("cellar_trim", cellar_trim)):
        if ONLY and name not in ONLY: continue
        C.reset(); C.MATS.clear(); M2.clear(); M2.update(palette())
        part = fn(M2)
        assert len(part.mats) <= 7, (name, len(part.mats))  # hollow_art.h loader: SWAT_ROOM101_MATERIALS-1
        C.export("hollow_" + name, part)
