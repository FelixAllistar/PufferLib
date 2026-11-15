# Briar Court surface decal atlas (presentation only).
#
#   python3 ocean/swat/tools/blender/motel_decals.py [OUT_DIR]
#   blender -b --factory-startup --python-exit-code 1 \
#     -P ocean/swat/tools/blender/motel_decals.py -- [OUT_DIR]
#
# OUT_DIR defaults to ocean/swat/assets/environment/motel_decals. Pure numpy
# (Blender's bundled Python works too); fixed seeds and a built-in PNG writer,
# so reruns reproduce the files byte for byte. Everything is original and CC0.
#
# Outputs:
#   decals_color.png  2048x1536 RGBA8, sRGB albedo + straight coverage alpha.
#                     Fully transparent texels carry the tile's mean colour so
#                     trilinear mips do not darken edges.
#   decals_nr.png     1024x768 RGBA8 (half resolution, same UVs): RGB = OpenGL
#                     tangent-space normal, A = roughness (0..1).
#   atlas.txt         tile name, cell rectangle (256 px cells on an 8x6 grid)
#                     and the default physical size in metres; read at runtime.
# Tiles: water streaks, roof runoff, rust runs, AC drip, door-frame and handle
# grime, wall-base grime, downspout splash, walkway scuffs, tyre marks, oil
# drips, carpet stains/burns/traffic wear, bathroom grout grime, corner mould and
# three original graffiti tags (invented words "VEXO", "RUMB", and a glyph).
import os, sys, zlib, struct, hashlib
import numpy as np

CELL = 256
COLS, ROWS = 8, 6
W, H = COLS * CELL, ROWS * CELL

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
here = os.path.dirname(os.path.abspath(__file__))
out = argv[0] if argv else os.path.join(here, "..", "..", "assets", "environment", "motel_decals")
os.makedirs(out, exist_ok=True)

# name, cell x, cell y, cells w, cells h, metres w, metres h
TILES = [
    ("streak_a",     0, 0, 1, 2, 0.45, 0.90),
    ("streak_b",     1, 0, 1, 2, 0.50, 1.00),
    ("rust_a",       2, 0, 1, 2, 0.22, 0.60),
    ("rust_b",       3, 0, 1, 2, 0.30, 0.80),
    ("ac_drip",      4, 0, 1, 2, 0.45, 1.10),
    ("frame_grime",  5, 0, 1, 2, 0.22, 2.05),
    ("roof_runoff",  6, 0, 2, 2, 2.00, 1.10),
    ("grime_base",   0, 2, 4, 1, 2.40, 0.45),
    ("scuff",        4, 2, 2, 1, 1.20, 0.60),
    ("tyre",         6, 2, 2, 1, 3.00, 0.60),
    ("handle_grime", 0, 3, 1, 1, 0.30, 0.30),
    ("burn",         1, 3, 1, 1, 0.16, 0.16),
    ("stain_a",      2, 3, 1, 1, 0.60, 0.60),
    ("stain_b",      3, 3, 1, 1, 0.50, 0.50),
    ("oil",          4, 3, 1, 1, 1.10, 1.10),
    ("mould",        5, 3, 1, 1, 0.50, 0.50),
    ("tag_vexo",     6, 3, 2, 1, 1.60, 0.80),
    ("tag_rumb",     0, 4, 2, 1, 1.40, 0.70),
    ("tag_glyph",    2, 4, 2, 1, 0.90, 0.45),
    ("mould_big",    4, 4, 2, 2, 0.90, 0.90),
    ("splash",       6, 4, 2, 1, 1.00, 0.50),
    ("carpet_wear",  0, 5, 2, 1, 1.60, 0.80),
    ("burn_b",       2, 5, 1, 1, 0.12, 0.12),
    ("stain_c",      3, 5, 1, 1, 0.40, 0.40),
    ("grout_grime",  6, 5, 2, 1, 1.20, 0.60),
]

# ---------------------------------------------------------------- noise helpers
def upsample(g, h, w):
    """Bilinear resize of a small grid (periodic) to h x w."""
    gh, gw = g.shape
    y = (np.arange(h) + 0.5) * gh / h - 0.5
    x = (np.arange(w) + 0.5) * gw / w - 0.5
    y0 = np.floor(y).astype(int); x0 = np.floor(x).astype(int)
    fy = (y - y0)[:, None]; fx = (x - x0)[None, :]
    fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
    y0 %= gh; x0 %= gw; y1 = (y0 + 1) % gh; x1 = (x0 + 1) % gw
    a = g[y0][:, x0]; b = g[y0][:, x1]; c = g[y1][:, x0]; d = g[y1][:, x1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

def fbm(rng, h, w, base=4, octaves=5, gain=0.5, aspect=(1, 1)):
    total = np.zeros((h, w)); amp = 1.0; norm = 0.0
    for o in range(octaves):
        n = base * (2 ** o)
        g = rng.random((max(2, n * aspect[0]), max(2, n * aspect[1])))
        total += amp * upsample(g, h, w); norm += amp; amp *= gain
    return total / norm

def smooth(a, b, x):
    """GLSL smoothstep(edge0, edge1, x); edge0 > edge1 gives a falling edge."""
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)

def grid(h, w):
    y = (np.arange(h) + 0.5)[:, None] / h
    x = (np.arange(w) + 0.5)[None, :] / w
    return np.broadcast_to(x, (h, w)), np.broadcast_to(y, (h, w))

def edge_fade(h, w, margin=0.06):
    x, y = grid(h, w)
    return smooth(0, margin, x) * smooth(0, margin, 1 - x) * smooth(0, margin, y) * smooth(0, margin, 1 - y)

def seg_dist(x, y, a, b):
    ax, ay = a; bx, by = b
    dx, dy = bx - ax, by - ay
    t = np.clip(((x - ax) * dx + (y - ay) * dy) / max(dx * dx + dy * dy, 1e-12), 0, 1)
    return np.hypot(x - ax - t * dx, y - ay - t * dy)

def poly_dist(x, y, pts):
    d = np.full(x.shape, 1e9)
    for i in range(len(pts) - 1):
        d = np.minimum(d, seg_dist(x, y, pts[i], pts[i + 1]))
    return d

# Each tile function returns (rgb 0..1 sRGB [h,w,3], alpha [h,w], height [h,w], roughness [h,w]).
def drips(rng, h, w, count, length=(0.4, 1.0), width=(0.03, 0.08), top=0.0, wobble=0.012):
    """Downward running fingers (v=0 at the top), returns coverage 0..1."""
    x, y = grid(h, w)
    cov = np.zeros((h, w))
    for i in range(count):
        cx = rng.uniform(0.12, 0.88); ln = rng.uniform(*length); wd = rng.uniform(*width)
        phase = rng.uniform(0, 6.28); freq = rng.uniform(0.6, 1.8)
        cxy = cx + wobble * np.sin(y * freq * 6.28 + phase) + 0.02 * (y - top)
        taper = wd * (1.0 - 0.75 * np.clip((y - top) / ln, 0, 1))
        d = np.abs(x - cxy)
        f = smooth(-0.01, 0.01, taper - d) * (y >= top) * smooth(0, 0.12, top + ln - y)
        cov = np.maximum(cov, f * rng.uniform(0.55, 1.0))
    return cov

def t_streak(rng, h, w, variant):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 3, 5, aspect=(2, 1))
    sheet = smooth(-0.06, 0.12, 0.16 - np.abs(x - 0.5) * (0.30 + 0.25 * y)) * smooth(0, 0.5, 0.95 - y)
    cov = np.maximum(sheet * 0.65, drips(rng, h, w, 7 if variant else 5, (0.45, 0.95), (0.02, 0.06)))
    cov *= 0.55 + 0.6 * n
    cov *= smooth(0, 0.05, y)
    tide = 0.25 * smooth(0.05, 0, np.abs(cov - 0.35))  # mineral tide lines
    a = np.clip(cov * 0.85 + tide * 0.3, 0, 1) * edge_fade(h, w, 0.04)
    base = np.array([0.30, 0.27, 0.22]) if variant == 0 else np.array([0.26, 0.25, 0.22])
    rgb = base[None, None, :] * (0.85 + 0.3 * n[..., None]) + tide[..., None] * np.array([0.35, 0.33, 0.28])
    return rgb, a, cov * 0.2, 0.62 + 0.25 * n

def t_roof_runoff(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 4, 5, aspect=(1, 2))
    band = smooth(0.35, 0, y) * 0.55
    cov = np.maximum(band, drips(rng, h, w, 26, (0.35, 0.95), (0.008, 0.03), wobble=0.004))
    # horizontally tileable: blend edges with a periodic copy
    cov *= 0.55 + 0.6 * n
    side = smooth(0, 0.04, x) * smooth(0, 0.04, 1 - x)
    a = np.clip(cov * 0.8, 0, 1) * smooth(0, 0.02, y) * smooth(0, 0.05, 1 - y) * (0.25 + 0.75 * side)
    rgb = np.array([0.24, 0.22, 0.19])[None, None, :] * (0.8 + 0.4 * n[..., None])
    return rgb, a, cov * 0.15, 0.7 + 0.2 * n

def t_rust(rng, h, w, variant):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 4, 5, aspect=(2, 1))
    head = smooth(0.12, 0.0, np.hypot((x - 0.5) * 1.2, (y - 0.04) * 2.5)) * 0.9
    cov = np.maximum(head, drips(rng, h, w, 4 if variant == 0 else 6, (0.5, 0.95), (0.03, 0.09), wobble=0.006))
    cov *= 0.6 + 0.5 * n
    a = np.clip(cov, 0, 1) * edge_fade(h, w, 0.04)
    dark = smooth(0.6, 0.2, y)
    c1 = np.array([0.45, 0.20, 0.07]); c2 = np.array([0.62, 0.36, 0.16])
    rgb = c1 * dark[..., None] + c2 * (1 - dark[..., None])
    rgb = rgb * (0.8 + 0.35 * n[..., None])
    return rgb, a * 0.95, cov * 0.25, 0.8 + 0.15 * n

def t_ac_drip(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 3, 5)
    wet = smooth(0.30, 0.0, np.abs(x - 0.5) * (1.0 + 1.5 * (1 - y))) * smooth(0.98, 0.3, y) * smooth(0, 0.1, y)
    cov = np.maximum(wet * 0.7, drips(rng, h, w, 6, (0.6, 1.0), (0.03, 0.07)))
    rings = 0.5 + 0.5 * np.sin(y * 70 + n * 8)
    cov *= 0.6 + 0.4 * n
    a = np.clip(cov * (0.75 + 0.25 * rings), 0, 1) * edge_fade(h, w, 0.04)
    rgb = np.array([0.20, 0.21, 0.19])[None, None, :] * (0.8 + 0.3 * n[..., None]) + 0.12 * (rings * cov)[..., None] * np.array([0.9, 0.85, 0.7])
    return rgb, a, cov * 0.1, 0.45 + 0.3 * (1 - cov)  # damp algae film is smoother

def t_frame_grime(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 3, 6, aspect=(4, 1))
    edge = smooth(0.7, 0.0, x)                    # dark at the frame edge (u=0)
    hand = smooth(0.30, 0.0, np.abs(y - 0.52)) * smooth(0.9, 0.0, x)  # handle height
    foot = smooth(0.80, 1.0, y) * 0.8              # kicked base
    cov = np.clip((edge * 0.45 + hand * 0.55 + foot * 0.5) * (0.5 + 0.7 * n), 0, 1)
    a = cov * edge_fade(h, w, 0.03)
    rgb = np.array([0.18, 0.16, 0.13])[None, None, :] * (0.8 + 0.4 * n[..., None])
    return rgb, a * 0.85, cov * 0.05, 0.75 + 0.15 * n

def t_handle(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 4, 6)
    r = np.hypot((x - 0.5) * 1.3, y - 0.5)
    cov = smooth(0.48, 0.05, r) * (0.45 + 0.8 * n)
    smear = smooth(0.18, 0.0, np.abs(y - 0.5 - 0.15 * (x - 0.5))) * smooth(0.5, 0.1, np.abs(x - 0.5))
    cov = np.clip(cov + 0.3 * smear * n, 0, 1)
    rgb = np.array([0.15, 0.13, 0.11])[None, None, :] * (0.85 + 0.3 * n[..., None])
    return rgb, cov * 0.8 * edge_fade(h, w), cov * 0.02, 0.55 + 0.2 * n  # hand oils polish

def t_grime_base(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 4, 6, aspect=(1, 4))
    splash = fbm(rng, h, w, 16, 3, aspect=(1, 4))
    cov = smooth(0.15, 1.0, y) ** 1.4 * (0.6 + 0.6 * n) + smooth(0.62, 0.8, splash) * smooth(0.3, 0.9, y) * 0.5
    cov = np.clip(cov, 0, 1)
    side = smooth(0, 0.08, x) * smooth(0, 0.08, 1 - x)
    a = cov * side * smooth(0, 0.01, 1 - y)
    rgb = np.array([0.27, 0.23, 0.18])[None, None, :] * (0.8 + 0.4 * n[..., None])
    return rgb, a * 0.9, cov * 0.15, 0.85 + 0.1 * n

def t_splash(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 4, 6, aspect=(1, 2))
    spots = smooth(0.66, 0.78, fbm(rng, h, w, 24, 2, aspect=(1, 2)))
    fan = smooth(0.55, 0.0, np.hypot((x - 0.5) * 1.1, (1 - y) * 1.6))
    cov = np.clip(fan * (0.9 + 0.6 * n) + spots * fan, 0, 1)
    rgb = np.array([0.33, 0.26, 0.18])[None, None, :] * (0.8 + 0.4 * n[..., None])
    return rgb, cov * edge_fade(h, w) * 0.9, cov * 0.15, 0.88 + 0.08 * n

def t_scuff(rng, h, w):
    x, y = grid(h, w)
    cov = np.zeros((h, w)); n = fbm(rng, h, w, 6, 5, aspect=(1, 2))
    for i in range(22):
        cx, cy = rng.uniform(0.1, 0.9), rng.uniform(0.15, 0.85)
        ang = rng.uniform(-0.6, 0.6); ln = rng.uniform(0.04, 0.16); wd = rng.uniform(0.006, 0.02)
        dx, dy = np.cos(ang) * ln * 0.5, np.sin(ang) * ln
        d = seg_dist(x, y, (cx - dx, cy - dy), (cx + dx, cy + dy))
        cov = np.maximum(cov, smooth(wd, 0, d) * rng.uniform(0.4, 1.0))
    cov = np.clip(cov * (0.6 + 0.6 * n) + 0.18 * smooth(0.5, 0.9, n), 0, 1)
    rgb = np.array([0.10, 0.10, 0.10])[None, None, :] * (0.8 + 0.4 * n[..., None])
    return rgb, cov * edge_fade(h, w) * 0.8, -cov * 0.05, 0.55 + 0.3 * (1 - cov)

def t_tyre(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 6, 5, aspect=(1, 4))
    curve = 0.5 + 0.12 * np.sin(x * 3.1 + 0.4) - 0.06
    tread = 0.6 + 0.4 * np.sin(x * 260)
    cov = np.zeros((h, w))
    for off in (-0.2, 0.2):
        d = np.abs(y - curve - off)
        cov = np.maximum(cov, smooth(0.11, 0.06, d) * (0.6 + 0.4 * tread))
    cov *= smooth(0, 0.25, x) * smooth(0, 0.15, 1 - x) * (0.55 + 0.6 * n)
    rgb = np.array([0.05, 0.05, 0.05])[None, None, :] * (0.8 + 0.4 * n[..., None])
    return rgb, np.clip(cov, 0, 1) * 0.85, -cov * 0.03, 0.45 + 0.35 * (1 - cov)

def t_oil(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 3, 6)
    r = np.hypot(x - 0.5, (y - 0.5) * 1.1) + (n - 0.5) * 0.25
    core = smooth(0.30, 0.10, r)
    halo = smooth(0.45, 0.25, r) * 0.45
    drops = smooth(0.70, 0.80, fbm(rng, h, w, 20, 2)) * smooth(0.5, 0.2, r)
    cov = np.clip(np.maximum(core, halo) + drops * 0.6, 0, 1)
    rgb = np.array([0.035, 0.032, 0.03])[None, None, :] + 0.05 * halo[..., None] * np.array([0.6, 0.5, 0.3])
    return rgb, cov * edge_fade(h, w) * 0.9, np.zeros((h, w)), 0.12 + 0.6 * (1 - core)

def t_burn(rng, h, w, small):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 6, 5)
    cov = np.zeros((h, w)); hole = np.zeros((h, w))
    spots = [(0.5, 0.5, 0.11)] if small else [(rng.uniform(0.3, 0.7), rng.uniform(0.3, 0.7), rng.uniform(0.06, 0.12)) for _ in range(3)]
    for cx, cy, rad in spots:
        r = np.hypot(x - cx, y - cy) + (n - 0.5) * 0.05
        cov = np.maximum(cov, smooth(rad * 2.2, rad * 0.6, r))
        hole = np.maximum(hole, smooth(rad, rad * 0.4, r))
    rgb = np.array([0.30, 0.20, 0.10])[None, None, :] * (1 - hole[..., None]) + np.array([0.04, 0.03, 0.025]) * hole[..., None]
    return rgb, np.clip(cov, 0, 1) * edge_fade(h, w), -hole * 0.6, 0.9 + 0.05 * n

def t_stain(rng, h, w, tint):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 3, 6)
    r = np.hypot(x - 0.5, y - 0.5) + (n - 0.5) * 0.3
    body = smooth(0.38, 0.20, r)
    ring = smooth(0.035, 0.0, np.abs(r - 0.32)) * 0.7
    cov = np.clip(body * 0.55 + ring, 0, 1) * (0.7 + 0.4 * n)
    rgb = np.array(tint)[None, None, :] * (0.8 + 0.3 * n[..., None]) * (1 - 0.3 * ring[..., None])
    return rgb, np.clip(cov, 0, 1) * edge_fade(h, w), np.zeros((h, w)), 0.85 + 0.1 * n

def t_carpet_wear(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 4, 6, aspect=(1, 2))
    path = smooth(0.42, 0.10, np.abs(y - 0.5 - 0.06 * np.sin(x * 5)))
    cov = path * (0.45 + 0.6 * n) * smooth(0, 0.2, x) * smooth(0, 0.2, 1 - x)
    rgb = np.array([0.22, 0.17, 0.12])[None, None, :] * (0.85 + 0.3 * n[..., None])
    return rgb, np.clip(cov, 0, 1) * 0.6, -cov * 0.08, 0.92 + 0.05 * n

def t_grout(rng, h, w):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 4, 6, aspect=(1, 2))
    tile = 0.15  # 0.15 m tiles across a 1.2 m wide decal
    gx = np.abs(((x * 1.2) % tile) / tile - 0.5); gy = np.abs(((y * 0.6) % tile) / tile - 0.5)
    lines = np.maximum(smooth(0.44, 0.5, gx), smooth(0.44, 0.5, gy))
    cov = np.clip(lines * (0.4 + 0.8 * n) + 0.25 * smooth(0.55, 0.85, n), 0, 1)
    rgb = np.array([0.24, 0.22, 0.16])[None, None, :] * (0.8 + 0.4 * n[..., None])
    return rgb, cov * edge_fade(h, w, 0.12) * 0.85, -lines * 0.05, 0.8 + 0.1 * n

def t_mould(rng, h, w, big):
    x, y = grid(h, w)
    n = fbm(rng, h, w, 5, 6)
    speck = smooth(0.62, 0.74, fbm(rng, h, w, 28, 3))
    # Corner growth: densest at (u=0,v=1) corner (wall/floor-wall junction).
    r = np.hypot(x * (0.8 if big else 1.0), (1 - y)) + (n - 0.5) * 0.4
    cov = np.clip(smooth(0.95, 0.2, r) * (0.4 + 0.8 * n) + speck * smooth(1.1, 0.4, r) * 0.8, 0, 1)
    rgb = np.array([0.10, 0.11, 0.08])[None, None, :] * (0.8 + 0.5 * n[..., None]) + speck[..., None] * np.array([0.02, 0.03, 0.01])
    return rgb, cov * edge_fade(h, w, 0.03) * 0.9, cov * 0.08, 0.95 + 0.04 * n

# ------------------------------------------------------------ original graffiti
# Simple letter skeletons in a unit box (x right, y down), drawn as thick strokes.
LETTERS = {
    "V": [[(0.05, 0.05), (0.5, 0.95), (0.95, 0.05)]],
    "E": [[(0.9, 0.08), (0.12, 0.08), (0.12, 0.92), (0.9, 0.92)], [(0.12, 0.5), (0.7, 0.5)]],
    "X": [[(0.08, 0.06), (0.92, 0.94)], [(0.92, 0.06), (0.08, 0.94)]],
    "O": [[(0.5, 0.06), (0.86, 0.22), (0.92, 0.6), (0.7, 0.92), (0.32, 0.92), (0.08, 0.6), (0.14, 0.22), (0.5, 0.06)]],
    "R": [[(0.12, 0.94), (0.12, 0.06), (0.7, 0.08), (0.85, 0.28), (0.66, 0.5), (0.12, 0.5)], [(0.45, 0.5), (0.92, 0.94)]],
    "U": [[(0.1, 0.05), (0.12, 0.7), (0.32, 0.93), (0.68, 0.93), (0.88, 0.7), (0.9, 0.05)]],
    "M": [[(0.06, 0.94), (0.12, 0.06), (0.5, 0.6), (0.88, 0.06), (0.94, 0.94)]],
    "B": [[(0.12, 0.94), (0.12, 0.06), (0.66, 0.08), (0.8, 0.26), (0.6, 0.48), (0.12, 0.48)],
          [(0.6, 0.48), (0.88, 0.68), (0.72, 0.92), (0.12, 0.94)]],
}

def tag(rng, h, w, word, fill, outline, slant=0.18):
    x, y = grid(h, w)
    X = x * w / h  # metric-ish units: height = 1
    n = fbm(rng, h, w, 6, 4)
    step = min(0.62, (w / h - 0.25) / len(word))
    width_total = len(word) * step
    start = (w / h - width_total) * 0.5 + 0.02
    d = np.full((h, w), 1e9)
    for i, ch in enumerate(word):
        ox = start + i * step + rng.uniform(-0.02, 0.02); oy = 0.14 + rng.uniform(-0.05, 0.05); sc = (0.68 + rng.uniform(-0.04, 0.04)) * step / 0.62 / 0.8
        for stroke in LETTERS[ch]:
            pts = [(ox + px * 0.55 * sc + slant * (1 - py) * sc * 0.5, oy + py * sc) for px, py in stroke]
            d = np.minimum(d, poly_dist(X, y, pts))
    inner = smooth(0.062, 0.050, d)
    outer = smooth(0.098, 0.085, d)
    spray = smooth(0.16, 0.09, d) * 0.25 * (0.5 + n)
    # paint runs under thick strokes
    run = np.zeros((h, w))
    for i in range(7):
        cx = rng.uniform(start + 0.1, start + width_total - 0.1); top = rng.uniform(0.5, 0.8); ln = rng.uniform(0.05, 0.18)
        run = np.maximum(run, smooth(0.008, 0.002, np.abs(X - cx)) * (y > top) * smooth(top + ln, top + ln - 0.03, y))
    run *= smooth(0.3, 0.0, d)
    a = np.clip(np.maximum.reduce([inner, outer, spray, run * 0.9]), 0, 1) * (0.85 + 0.15 * n)
    col = np.array(outline)[None, None, :] * np.ones((h, w, 1))
    col = col * (1 - inner[..., None]) + np.array(fill)[None, None, :] * inner[..., None]
    col = col * (0.92 + 0.12 * n[..., None])
    return col, a * edge_fade(h, w, 0.02), inner * 0.02, 0.5 + 0.1 * n

def t_glyph(rng, h, w):
    x, y = grid(h, w)
    X = x * w / h
    n = fbm(rng, h, w, 6, 4)
    cx = w / h * 0.5
    # an original "crowned spiral": a spiral with three points above it
    t = np.linspace(0, 4.4 * np.pi, 160)
    rr = 0.04 + 0.03 * t / np.pi
    spiral = [(cx + r * np.cos(a) * 1.2, 0.6 + r * np.sin(a)) for r, a in zip(rr * 0.9, t)]
    d = poly_dist(X, y, spiral)
    crown = [(cx - 0.42, 0.32), (cx - 0.3, 0.06), (cx - 0.12, 0.28), (cx, 0.04), (cx + 0.12, 0.28), (cx + 0.3, 0.06), (cx + 0.42, 0.32)]
    d = np.minimum(d, poly_dist(X, y, crown))
    a = np.clip(np.maximum(smooth(0.045, 0.03, d), smooth(0.11, 0.05, d) * 0.2 * (0.5 + n)), 0, 1)
    col = np.array([0.95, 0.93, 0.88])[None, None, :] * np.ones((h, w, 1)) * (0.9 + 0.1 * n[..., None])
    return col, a * edge_fade(h, w, 0.02), np.zeros((h, w)), 0.5 + 0.1 * n

def build(name, rng, h, w):
    if name == "streak_a": return t_streak(rng, h, w, 0)
    if name == "streak_b": return t_streak(rng, h, w, 1)
    if name == "roof_runoff": return t_roof_runoff(rng, h, w)
    if name == "rust_a": return t_rust(rng, h, w, 0)
    if name == "rust_b": return t_rust(rng, h, w, 1)
    if name == "ac_drip": return t_ac_drip(rng, h, w)
    if name == "frame_grime": return t_frame_grime(rng, h, w)
    if name == "handle_grime": return t_handle(rng, h, w)
    if name == "grime_base": return t_grime_base(rng, h, w)
    if name == "splash": return t_splash(rng, h, w)
    if name == "scuff": return t_scuff(rng, h, w)
    if name == "tyre": return t_tyre(rng, h, w)
    if name == "oil": return t_oil(rng, h, w)
    if name == "burn": return t_burn(rng, h, w, False)
    if name == "burn_b": return t_burn(rng, h, w, True)
    if name == "stain_a": return t_stain(rng, h, w, [0.36, 0.26, 0.14])   # coffee / soda
    if name == "stain_b": return t_stain(rng, h, w, [0.30, 0.12, 0.10])   # wine
    if name == "stain_c": return t_stain(rng, h, w, [0.45, 0.42, 0.30])   # dried water / bleach-ish
    if name == "carpet_wear": return t_carpet_wear(rng, h, w)
    if name == "grout_grime": return t_grout(rng, h, w)
    if name == "mould": return t_mould(rng, h, w, False)
    if name == "mould_big": return t_mould(rng, h, w, True)
    if name == "tag_vexo": return tag(rng, h, w, "VEXO", [0.86, 0.32, 0.12], [0.08, 0.08, 0.10])
    if name == "tag_rumb": return tag(rng, h, w, "RUMB", [0.20, 0.55, 0.78], [0.95, 0.94, 0.90], slant=0.25)
    if name == "tag_glyph": return t_glyph(rng, h, w)
    raise KeyError(name)

def normal_from_height(hgt, strength):
    gy, gx = np.gradient(hgt)
    nx = -gx * strength; ny = gy * strength  # OpenGL: +Y up in texture space (v down in image)
    nz = np.ones_like(hgt)
    inv = 1 / np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx * inv, ny * inv, nz * inv], -1)

def png(path, rgba):
    h, w, _ = rgba.shape
    raw = b"".join(b"\x00" + rgba[r].tobytes() for r in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f: f.write(data)
    return hashlib.sha256(data).hexdigest()

color = np.zeros((H, W, 4), np.uint8)
nr = np.zeros((H // 2, W // 2, 4), np.uint8)
nr[..., 0] = 128; nr[..., 1] = 128; nr[..., 2] = 255; nr[..., 3] = 230
for index, (name, cx, cy, cw, ch, mw, mh) in enumerate(TILES):
    rng = np.random.default_rng(9100 + index)
    h, w = ch * CELL, cw * CELL
    rgb, a, hgt, rough = build(name, rng, h, w)
    a = np.clip(a, 0, 1)
    rgb = np.clip(rgb, 0, 1)
    mean = (rgb * a[..., None]).sum((0, 1)) / max(a.sum(), 1e-6)
    # Quantize alpha to 1/255 first so bleed colour is used exactly where a==0.
    a8 = np.round(a * 255).astype(np.uint8)
    rgb = np.where((a8 == 0)[..., None], mean[None, None, :], rgb)
    tile = np.concatenate([np.round(rgb * 255).astype(np.uint8), a8[..., None]], -1)
    color[cy * CELL:(cy + ch) * CELL, cx * CELL:(cx + cw) * CELL] = tile
    # half-resolution normal/roughness
    hh = hgt.reshape(h // 2, 2, w // 2, 2).mean((1, 3))
    rr = np.clip(rough, 0, 1).reshape(h // 2, 2, w // 2, 2).mean((1, 3))
    nrm = normal_from_height(hh, 40.0)
    half = np.concatenate([np.round((nrm * 0.5 + 0.5) * 255).astype(np.uint8), np.round(rr * 255).astype(np.uint8)[..., None]], -1)
    nr[cy * CELL // 2:(cy + ch) * CELL // 2, cx * CELL // 2:(cx + cw) * CELL // 2] = half

h1 = png(os.path.join(out, "decals_color.png"), color)
h2 = png(os.path.join(out, "decals_nr.png"), nr)
with open(os.path.join(out, "atlas.txt"), "w") as f:
    f.write("# Generated by tools/blender/motel_decals.py. Do not edit.\n")
    f.write("# atlas COLOR_FILE NR_FILE COLUMNS ROWS\n")
    f.write("atlas decals_color.png decals_nr.png %d %d\n" % (COLS, ROWS))
    f.write("# tile NAME CELL_X CELL_Y CELLS_W CELLS_H METRES_W METRES_H\n")
    for name, cx, cy, cw, ch, mw, mh in TILES:
        f.write("tile %s %d %d %d %d %.2f %.2f\n" % (name, cx, cy, cw, ch, mw, mh))
    f.write("# sha256 decals_color.png %s\n# sha256 decals_nr.png %s\n" % (h1, h2))
print("wrote", out, h1[:12], h2[:12])
