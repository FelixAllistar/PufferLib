# Briar Court interior props v2: snake plant (Sansevieria) in a terracotta
# pot on a dark saucer (reception corner). Leaves are solid double-sided
# strips (no alpha cards); leaf and clay maps are baked from seeded numpy
# noise (no downloads).
# Origin: floor, centre of the back edge of the 0.5 m footprint; front +Z.
#   blender -b --factory-startup --python-exit-code 1 \
#       -P ocean/swat/tools/blender/props_int_v2_potted_plant.py -- OUT_DIR [--render PNG] [--iter N]
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

reset()
rng = np.random.default_rng(1974)
C = (0, 0, .25)                      # pot centre (x, -, z)
POT_R, POT_H = .17, .32

def tex_leaf(seed, n=256):
    """U across the blade (0..1), V along it. Yellow margins, mottled
    dark/grey-green cross bands."""
    r = np.random.default_rng(seed)
    v, u = np.mgrid[0:n, 0:n] / n
    band = periodic_noise(r, n, 3, octaves=3)
    wav = (np.sin(v * 90 + 5 * periodic_noise(r, n, 3, octaves=2) + u * 4) * .5 + .5) ** 1.5
    mott = periodic_noise(r, n, 12, octaves=3)
    light = srgb((112, 140, 84)); dark = srgb((22, 50, 20))
    m = np.clip(.35 * band + .45 * wav + .45 * (mott > .5) + .05, 0, 1)
    rgb = light * (1 - m)[..., None] + dark * m[..., None]
    edge = np.minimum(u, 1 - u)
    rgb = np.where((edge < .09)[..., None], srgb((214, 200, 70)) * (.9 + .1 * mott[..., None]), rgb)
    return rgb

def tex_clay(seed, n=256):
    r = np.random.default_rng(seed)
    v, u = np.mgrid[0:n, 0:n] / n
    nz = periodic_noise(r, n, 8)
    rgb = srgb((168, 92, 62)) * (.85 + .25 * nz)[..., None]
    # Pale mineral bloom low on the pot and a darker watering stain.
    rgb = rgb * (1 - .25 * np.clip(periodic_noise(r, n, 2) - .5, 0, 1))[..., None]
    rgb += srgb((40, 34, 28)) * np.clip((v - .7) * 2, 0, 1)[..., None] * periodic_noise(r, n, 4)[..., None]
    return rgb

leaf_img = to_image("snake_leaf", tex_leaf(3))
clay_img = to_image("terracotta", tex_clay(5))
m_leaf = mat("plant_leaf", image=leaf_img, rough=.5)
m_clay = mat("plant_clay", image=clay_img, rough=.85)
m_saucer = mat("plant_saucer", (.06, .06, .065), rough=.5)
m_soil = mat("plant_soil", (.10, .07, .05), rough=1.0)

p = Part()
# Saucer.
p.add(bm_lathe([(.0, .0), (.17, .0), (.19, .035), (.185, .038), (.0, .03)], c=(C[0], 0, C[2]), seg=28), m_saucer)
# Bulbous pot with a rolled lip and throwing rings.
prof = [(.10, .01), (.15, .04), (.178, .10), (.183, .13), (.185, .135), (.185, .16), (.18, .19), (.1815, .195), (.175, .22), (.162, .25), (.163, .255), (.15, .27), (.135, .295), (.14, .305), (.142, .315), (.13, .318), (.125, .30)]
pot = bm_lathe(prof, c=(C[0], 0, C[2]), seg=32, cap_bottom=True)
p.add(pot, m_clay, uv_scale=1.0)
p.add(bm_cyl((C[0], .292, C[2]), .126, .01, seg=24), m_soil)

idx_leaf = p.mat_index(m_leaf)
NU, NV = 2, 12

def leaf(base, height, width, yaw, lean, twist, curl, roll=0.0):
    """Two-sided sword blade written straight into the part with u/v UVs."""
    def fn(u, t):
        w = width * math.sin(math.pi * min(t / .9, 1) * .5) ** .4 * (1 - t ** 3) ** .7
        w *= min(1.0, .45 + t * 3)
        s = (u - .5) * w
        fold = -abs(s) * .35
        a = twist * t
        a += roll
        lx = s * math.cos(a) - fold * math.sin(a)
        lz = s * math.sin(a) + fold * math.cos(a)
        out = lean * height * t * t + curl * t
        y = height * t * (1 - .25 * lean * t)          # arching blades lose height
        cy, sy = math.cos(yaw), math.sin(yaw)
        x = lx * cy + (lz + out) * sy
        z = -lx * sy + (lz + out) * cy
        return E(base[0] + x, base[1] + y, base[2] + z)
    grid = [[fn(i / NU, j / NV) for j in range(NV + 1)] for i in range(NU + 1)]
    for side in (0, 1):
        vs = [[p.bm.verts.new(grid[i][j]) for j in range(NV + 1)] for i in range(NU + 1)]
        for i in range(NU):
            for j in range(NV):
                quad = [(i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)]
                if side: quad.reverse()
                f = p.bm.faces.new([vs[a][b] for a, b in quad]); f.material_index = idx_leaf; f.smooth = True
                for l, (a, b) in zip(f.loops, quad):
                    l[p.uv].uv = (a / NU, b / NV)

for i in range(23):
    a = rng.uniform(0, 2 * math.pi)
    rad = rng.uniform(0, .07)
    base = (C[0] + rad * math.cos(a), .27, C[2] + rad * math.sin(a))
    tall = i < 6
    h = rng.uniform(.70, .88) if tall else rng.uniform(.38, .72)
    if i >= 19: h = rng.uniform(.22, .34)
    yaw = math.atan2(math.cos(a), math.sin(a)) + rng.uniform(-.5, .5)
    # Blades fan mostly sideways (as in the reference), little toward/away
    # from the wall: pull each yaw toward the nearest of +-X.
    side = math.pi / 2 if math.sin(yaw) >= 0 else -math.pi / 2
    yaw = side + .55 * (math.atan2(math.sin(yaw - side), math.cos(yaw - side)))
    lean = rng.uniform(.05, .15) if tall else rng.uniform(.25, .45)
    roll = (math.pi / 2 - .35) * (1 if side > 0 else -1) * min(1, lean * 2.5) + rng.uniform(-.3, .3)
    leaf(base, h, rng.uniform(.09, .115), yaw, lean, rng.uniform(-.5, .5), rng.uniform(0, .03), roll)

export("potted_plant", p)
# Reference: "A healthy Snake Plant (Sansevieria) grows in a clay pot" (Bigul
# Malayi, WordPress Photo Directory, CC0): frontal, camera at ~0.7 m.
REF_VIEW = {"cam": (0, .72, 2.1), "target": (0, .55, .25), "fov": 34, "res": (720, 960),
            "room": {"box": (-1.5, 1.5, 0, 3, 2.6), "walls": ["back"], "wall": (.16, .16, .62), "floor": (.55, .52, .45)},
            "lights": [((.3, 2.0, 1.8), 240, (1, .97, .92), 1.0), ((-1.2, 1.0, 1.6), 60, (1, 1, 1), 1.5)]}
preview(REF_VIEW)
