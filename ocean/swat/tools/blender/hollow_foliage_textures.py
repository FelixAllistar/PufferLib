# The Hollow woods: foliage card atlas (own work, procedural, deterministic).
#
#   python3 hollow_foliage_textures.py OUT_DIR          (needs numpy + Pillow)
#
# Writes OUT_DIR/foliage_atlas.png: a 2048 x 2048 RGBA atlas of 4 x 4 cells
# (512 px). Every cell is a card drawn base-at-bottom-centre, growing up, the
# way hollow_trees.py maps it onto a branch: alpha is coverage, RGB is albedo
# (sRGB), bled into the transparent texels so mipmaps keep the twig colour.
# The engine alpha-tests the cards (lighting.c foliage flag, 253/255 albedo
# alpha) and widens the coverage with distance so fine twigs do not vanish.
#
# Cells, one row per line (row = y / 512 from the top, columns left to right):
#   row 0: twig_bare_a  twig_bare_b  twig_oak_leaves  twig_poplar
#   row 1: hemlock_a    hemlock_b    pine_a           pine_b
#   row 2: laurel_a     laurel_b     fern_brown       goldenrod
#   row 3: dead_grass   twig_sycamore branch_big      branch_big_b
# hollow_trees.py has the same table (ATLAS).
import sys, os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1] if len(sys.argv) > 1 else "."
CELL = 512
SS = 3                      # supersampling for anti-aliased coverage
N = CELL * SS

def new_cell():
    return Image.new("RGBA", (N, N), (0, 0, 0, 0))

def jitter_rgb(rng, c, amt=10):
    # Value jitter with only a trace of hue: natural material, not confetti.
    v = rng.normal(0, amt); h = rng.normal(0, amt * .15, 3)
    return tuple(int(max(0, min(255, c[i] + v + h[i]))) for i in range(3))

class Pen:
    def __init__(self, img):
        self.d = ImageDraw.Draw(img)
    def seg(self, a, b, w, col):
        w = max(2 * SS, int(round(w)))   # no twig thinner than ~2 px at full res
        self.d.line([a, b], fill=col, width=w)
        if w > 3:  # round joints
            r = w / 2
            self.d.ellipse([b[0] - r, b[1] - r, b[0] + r, b[1] + r], fill=col)
    def ellipse(self, c, rx, ry, ang, col):
        pts = []
        for k in range(14):
            t = 2 * math.pi * k / 14
            x, y = rx * math.cos(t), ry * math.sin(t)
            pts.append((c[0] + x * math.cos(ang) - y * math.sin(ang), c[1] + x * math.sin(ang) + y * math.cos(ang)))
        self.d.polygon(pts, fill=col)

# --------------------------------------------------------------- deciduous twigs
def twig_spray(rng, pen, x, y, ang, length, width, depth, col, leaf=None, leaf_p=0.0, spread=(.35, .65), droop=0.0, out=None, apical=False):
    """Recursive sympodial twig spray. ang 0 = up (screen -y)."""
    steps = 3
    px, py = x, y
    for s in range(steps):
        ang += rng.normal(0, .12) + droop * (s + 1) / steps * .15
        L = length / steps
        nx, ny = px + L * math.sin(ang), py - L * math.cos(ang)
        w = width * (1 - .25 * (s + 1) / steps)
        pen.seg((px, py), (nx, ny), w, col)
        px, py = nx, ny
    if depth == 0 or length < 10 * SS:
        if leaf and rng.random() < leaf_p:
            leaf(rng, pen, px, py, ang)
        if out is not None: out.append((px, py))
        return
    n = (3 if rng.random() < .75 else 2) if apical else (2 if rng.random() < .7 else 3)
    for k in range(n):
        side = -1 if k % 2 == 0 else 1
        if n == 3 and k == 2: side = 0
        a = ang + side * rng.uniform(*spread) + rng.normal(0, .08)
        lr = (rng.uniform(.45, .62) if side else rng.uniform(.8, .92)) if apical else (rng.uniform(.62, .82) if side else rng.uniform(.75, .9))
        twig_spray(rng, pen, px, py, a, length * lr, width * .68, depth - 1, jitter_rgb(rng, col, 6), leaf, leaf_p, spread, droop, out, apical)
    # small lateral spurs along the segment
    if depth >= 2 and rng.random() < .6:
        t = rng.uniform(.3, .7)
        sx, sy = x + (px - x) * t, y + (py - y) * t
        a = ang + rng.choice([-1, 1]) * rng.uniform(.6, 1.0)
        twig_spray(rng, pen, sx, sy, a, length * .4, width * .5, depth - 2, col, leaf, leaf_p, spread, droop, out)

def oak_leaf(col_base):
    def f(rng, pen, x, y, ang):
        # A marcescent oak leaf hanging from the tip: lobed, curled, brown.
        a = math.pi + rng.normal(0, .5)          # hangs down
        L = rng.uniform(26, 40) * SS; W = L * .32
        cx, cy = x + .5 * L * math.sin(a), y - .5 * L * math.cos(a)
        col = jitter_rgb(rng, col_base, 14)
        pen.ellipse((cx, cy), W, L / 2, a, col)
        for k in range(3):  # lobes
            t = (k - 1) * .3 * L
            for s in (-1, 1):
                lx = cx + t * math.sin(a) + s * W * .9 * math.cos(a); ly = cy - t * math.cos(a) + s * W * .9 * math.sin(a)
                pen.ellipse((lx, ly), W * .45, W * .55, a, col)
    return f

def poplar_leaf(rng, pen, x, y, ang):
    a = math.pi + rng.normal(0, .6); L = rng.uniform(22, 30) * SS
    cx, cy = x + .5 * L * math.sin(a), y - .5 * L * math.cos(a)
    pen.ellipse((cx, cy), L * .45, L * .42, a, jitter_rgb(rng, (128, 102, 52), 14))

def draw_twigs(rng, kind):
    img = new_cell(); pen = Pen(img)
    bark = {"bare_a": (112, 106, 98), "bare_b": (104, 100, 94), "oak": (104, 96, 86), "poplar": (112, 108, 100), "syc": (168, 164, 150)}[kind]
    leaf = {"oak": oak_leaf((104, 70, 42)), "poplar": poplar_leaf}.get(kind)
    leaf_p = {"oak": .16, "poplar": .07}.get(kind, 0)
    depth = 6 if kind != "poplar" else 5
    spread = (.30, .5) if kind == "poplar" else (.32, .6)
    # Two or three sprays from the base so the card is filled, not one stick.
    # One leading shoot with a sympodial spray: long and directional, so a
    # crown of cards reads as fine twigs, not as round puffs.
    for k in range(2):
        x = N / 2 + rng.normal(0, N * .03); a = rng.normal(0, .12) + (k - .5) * .25
        twig_spray(rng, pen, x, N * .995, a, N * rng.uniform(.2, .24), 7 * SS, depth, bark, leaf, leaf_p, spread, apical=True)
    return img

def draw_big_branch(rng, variant):
    """LOD1 card: a whole secondary limb with its twig crown."""
    img = new_cell(); pen = Pen(img)
    col = (76, 70, 64) if variant == 0 else (84, 78, 70)
    twig_spray(rng, pen, N / 2, N * .995, rng.normal(0, .1), N * .24, 13 * SS, 7, col, oak_leaf((100, 68, 42)) if variant else None,
               .12 if variant else 0, (.3, .6))
    return img

# --------------------------------------------------------------- conifers
def needle_shoot(rng, pen, x, y, ang, L, needle, col, density=1.0, flat=True):
    """A shoot with two rows of needles (hemlock: short, flat, 2-ranked)."""
    steps = max(3, int(L / (3 * SS)))
    for s in range(steps):
        t = s / steps
        px, py = x + t * L * math.sin(ang), y - t * L * math.cos(ang)
        nl = needle * (1 - .5 * t)
        for side in (-1, 1):
            if rng.random() > density: continue
            a = ang + side * (math.pi / 2 - .35 + rng.normal(0, .15))
            c = jitter_rgb(rng, col, 10)
            pen.seg((px, py), (px + nl * math.sin(a), py - nl * math.cos(a)), 2.2 * SS, c)

def hemlock_spray(rng, variant):
    img = new_cell(); pen = Pen(img)
    dark = (34, 50, 36); mid = (46, 64, 44)
    # Main axis arches outward then droops at the tip (cards hang from a branch).
    x, y = N / 2, N * .995; ang = rng.normal(0, .08); L = N * .9
    pts = []
    steps = 14
    for s in range(steps):
        ang += .045 * (1 if variant else -1) + rng.normal(0, .03)
        nx, ny = x + L / steps * math.sin(ang), y - L / steps * math.cos(ang)
        pen.seg((x, y), (nx, ny), (6 - 4 * s / steps) * SS, (60, 48, 40)); pts.append((x, y, ang, s / steps)); x, y = nx, ny
    # Side shoots: 2-ranked, getting shorter to the tip, each feathered with needles.
    for (px, py, a, t) in pts[1:]:
        for side in (-1, 1):
            if rng.random() < .15: continue
            sa = a + side * rng.uniform(.75, 1.1)
            sl = N * (.30 - .22 * t) * rng.uniform(.75, 1.1)
            needle_shoot(rng, pen, px, py, sa, sl, 9 * SS, dark if rng.random() < .6 else mid, .95)
            # second-order shoots
            for q in range(2):
                tt = rng.uniform(.3, .8); qx, qy = px + tt * sl * math.sin(sa), py - tt * sl * math.cos(sa)
                qa = sa + rng.choice([-1, 1]) * rng.uniform(.6, 1.0)
                needle_shoot(rng, pen, qx, qy, qa, sl * .45, 7 * SS, dark, .9)
    return img

def pine_plume(rng, variant):
    img = new_cell(); pen = Pen(img)
    twig = (70, 58, 46); needle = (52, 74, 58) if variant == 0 else (46, 66, 54)
    def branch(x, y, ang, L, depth, w):
        steps = 4
        for s in range(steps):
            ang += rng.normal(0, .1)
            nx, ny = x + L / steps * math.sin(ang), y - L / steps * math.cos(ang)
            pen.seg((x, y), (nx, ny), w, twig); x, y = nx, ny
            if s >= 1:  # needle fascicles along the outer twig
                tuft(x, y, ang, 34 * SS * rng.uniform(.8, 1.1), 14)
        if depth:
            for side in (-1, 1):
                branch(x, y, ang + side * rng.uniform(.4, .8), L * .6, depth - 1, w * .7)
        tuft(x, y, ang, 44 * SS, 30)
    def tuft(x, y, ang, L, n):
        for k in range(n):
            a = ang + rng.normal(0, .65)
            l = L * rng.uniform(.6, 1.0)
            pen.seg((x, y), (x + l * math.sin(a), y - l * math.cos(a)), 1.7 * SS, jitter_rgb(rng, needle, 9))
    branch(N / 2, N * .995, rng.normal(0, .1), N * .34, 3, 6 * SS)
    for k in range(2): branch(N / 2 + rng.normal(0, N * .05), N * .8, rng.choice([-1, 1]) * rng.uniform(.5, .9), N * .22, 2, 4 * SS)
    return img

# --------------------------------------------------------------- understory
def laurel(rng, variant):
    """Rhododendron / mountain laurel: crooked stems ending in whorls of
    long leathery leaves that hang and curl in the cold."""
    img = new_cell(); pen = Pen(img)
    stem = (64, 54, 44); leaf = (40, 58, 36) if variant == 0 else (48, 62, 38)
    def stemf(x, y, ang, L, depth, w):
        steps = 3
        for s in range(steps):
            ang += rng.normal(0, .25)
            nx, ny = x + L / steps * math.sin(ang), y - L / steps * math.cos(ang)
            pen.seg((x, y), (nx, ny), w, stem); x, y = nx, ny
        if depth:
            for side in (-1, 1):
                stemf(x, y, ang + side * rng.uniform(.35, .8), L * .72, depth - 1, w * .7)
        else:
            k = rng.integers(5, 9)
            for i in range(k):  # leaf whorl
                a = ang + 2 * math.pi * i / k + rng.normal(0, .2)
                a = a + (math.pi - a % (2 * math.pi)) * .35   # droop
                Lf = rng.uniform(40, 62) * SS * (.55 if variant else 1.0)
                cx, cy = x + .5 * Lf * math.sin(a), y - .5 * Lf * math.cos(a)
                pen.ellipse((cx, cy), Lf * (.14 if not variant else .22), Lf / 2, a, jitter_rgb(rng, leaf, 9))
    for k in range(3):
        stemf(N / 2 + rng.normal(0, N * .08), N * .995, rng.normal(0, .35), N * .26, 2, 7 * SS)
    return img

def fern_brown(rng):
    img = new_cell(); pen = Pen(img)
    for k in range(7):
        ang = rng.normal(0, .55); x, y = N / 2 + rng.normal(0, N * .04), N * .995
        L = N * rng.uniform(.55, .8); steps = 18; broken = rng.random() < .35
        col = jitter_rgb(rng, (112, 76, 44), 12)
        for s in range(steps):
            ang += (.05 + .06 * s / steps) * (1 if ang > 0 else -1)   # arching, collapsing
            if broken and s == steps // 2: ang += 1.2 * (1 if ang > 0 else -1)
            nx, ny = x + L / steps * math.sin(ang), y - L / steps * math.cos(ang)
            pen.seg((x, y), (nx, ny), 2.5 * SS, col)
            pl = N * .07 * (1 - abs(s / steps - .4))
            for side in (-1, 1):
                if s > 2 and rng.random() < .85:
                    a = ang + side * 1.25
                    c = jitter_rgb(rng, col, 10)
                    pen.ellipse((x + .5 * pl * math.sin(a), y - .5 * pl * math.cos(a)), 3 * SS, pl / 2, a, c)
            x, y = nx, ny
    return img

def goldenrod(rng):
    img = new_cell(); pen = Pen(img)
    for k in range(9):
        x, y = N / 2 + rng.normal(0, N * .12), N * .995
        ang = rng.normal(0, .12); L = N * rng.uniform(.6, .95); steps = 10
        col = jitter_rgb(rng, (128, 112, 84), 10)
        bend = rng.random() < .3; at = rng.integers(4, 8)
        for s in range(steps):
            if bend and s == at: ang += rng.choice([-1, 1]) * rng.uniform(.6, 1.4)
            nx, ny = x + L / steps * math.sin(ang), y - L / steps * math.cos(ang)
            pen.seg((x, y), (nx, ny), 3 * SS, col); x, y = nx, ny
        # spent plume: a fan of small grey-buff seed heads
        for q in range(16):
            a = ang + rng.normal(0, .7); r = rng.uniform(6, 34) * SS
            pen.ellipse((x + r * math.sin(a), y - r * math.cos(a)), 3 * SS, 5 * SS, a, jitter_rgb(rng, (112, 104, 90), 8))
    return img

def dead_grass(rng):
    img = new_cell(); pen = Pen(img)
    for k in range(70):
        x = N / 2 + rng.normal(0, N * .16); y = N * .995
        ang = rng.normal(0, .4); L = N * rng.uniform(.3, .85); steps = 8
        col = jitter_rgb(rng, (118, 100, 68), 14)
        w = rng.uniform(3, 6) * SS
        for s in range(steps):
            ang += .06 * (1 if ang > 0 else -1) * (s / steps) * 3
            nx, ny = x + L / steps * math.sin(ang), y - L / steps * math.cos(ang)
            pen.seg((x, y), (nx, ny), w * (1 - s / steps * .8), col); x, y = nx, ny
    return img

# --------------------------------------------------------------- assemble
def finish(img):
    """Downsample, bleed colour into transparent texels, sRGB RGBA uint8."""
    small = img.resize((CELL, CELL), Image.LANCZOS)
    a = np.asarray(small, dtype=np.float32) / 255.0
    rgb, al = a[:, :, :3], a[:, :, 3:4]
    # Un-premultiplied colour, then bleed by repeated blurs of premultiplied data.
    pm = rgb * al; acc = al.copy()
    out = np.where(al > 1e-3, rgb, 0)
    have = (al > 1e-3)[:, :, 0]
    cur_pm, cur_a = pm, acc
    for it in range(6):
        cur_pm = blur(cur_pm)
        cur_a = blur(cur_a)
        fill = cur_pm / np.maximum(cur_a, 1e-4)
        m = (~have) & (cur_a[:, :, 0] > 1e-4)
        out[m] = fill[m]; have = have | m
    out[~have] = np.array([.30, .28, .26])
    return np.dstack([out, al])

def blur(x):
    k = np.array([1, 4, 6, 4, 1], np.float32); k /= k.sum()
    y = x
    for ax in (0, 1):
        y = sum(np.roll(y, i - 2, axis=ax) * k[i] for i in range(5))
    return y

def main():
    os.makedirs(OUT, exist_ok=True)
    cells = {
        (0, 0): lambda r: draw_twigs(r, "bare_a"), (1, 0): lambda r: draw_twigs(r, "bare_b"),
        (2, 0): lambda r: draw_twigs(r, "oak"), (3, 0): lambda r: draw_twigs(r, "poplar"),
        (0, 1): lambda r: hemlock_spray(r, 0), (1, 1): lambda r: hemlock_spray(r, 1),
        (2, 1): lambda r: pine_plume(r, 0), (3, 1): lambda r: pine_plume(r, 1),
        (0, 2): lambda r: laurel(r, 0), (1, 2): lambda r: laurel(r, 1),
        (2, 2): fern_brown, (3, 2): goldenrod,
        (0, 3): dead_grass, (1, 3): lambda r: draw_twigs(r, "syc"),
        (2, 3): lambda r: draw_big_branch(r, 0), (3, 3): lambda r: draw_big_branch(r, 1),
    }
    # Keys are (column, row).
    atlas = np.zeros((4 * CELL, 4 * CELL, 4), np.float32)
    for (col, row), fn in sorted(cells.items()):
        rng = np.random.default_rng(1000 + row * 4 + col)
        cell = finish(fn(rng))
        atlas[row * CELL:(row + 1) * CELL, col * CELL:(col + 1) * CELL] = cell
        print("cell", row, col, "coverage %.3f" % cell[:, :, 3].mean())
    im = Image.fromarray((np.clip(atlas, 0, 1) * 255 + .5).astype(np.uint8), "RGBA")
    im.save(os.path.join(OUT, "foliage_atlas.png"), optimize=True)

if __name__ == "__main__":
    main()
