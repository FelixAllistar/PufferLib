#!/usr/bin/env python3
"""The Hollow yard ground (woods v2): ground decal tiles and their placement.

    python3 hollow_ground.py DECAL_DIR           (numpy + Pillow; run after hollow_textures.py)

hollow_textures.py writes the 4 x 3 cell decal atlas (stains, drag marks,
newsprint). This script widens it to 8 x 6 cells of 256 px (2048 x 1536,
the surface-decal budget) without touching those cells, adds ground tiles
cut from CC0 Poly Haven scans, appends them to atlas.txt and rewrites the
generated ground section of placement.txt:

  path          worn foot path, packed bare dirt (tiles along its length)
  ruts          two tyre ruts with standing water (tiles along its length)
  puddle_a/b    muddy water, low roughness (reflects the dusk sky)
  leaf_drift    leaves banked against a wall or fence (wall at the tile top)
  mud_patch, wet_patch, litter_patch, grass_patch, gravel_spill, worn_blob
                large-scale variation of the uniform yard texture

Placement (deterministic, seeded): worn paths between the house, barn,
smokehouse, cellar, woodpile, tool shed and the lane; ruts from the lane to
the bus and the junk cars, with puddles; leaf drifts along the house, barn,
smokehouse and shed walls and the dog-pen fence; scattered patches.
Presentation only (surface_decals.h): nothing touches collision or navigation.
Sources: Poly Haven (CC0) brown_mud_dry, muddy_tracks, brown_mud_02,
dry_decay_leaves, gravel_floor_02, leafy_grass; see hollow_woods/README.md.
"""
import os, sys, math, urllib.request
import numpy as np
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else "hollow_decals"
CACHE = os.environ.get("SWAT_PH_CACHE", os.path.expanduser("~/.cache/swat_polyhaven"))
P = 256
COLS, ROWS = 8, 6
MARK = "# --- generated: yard ground (hollow_ground.py) ---"

def scan(asset, kind="diff"):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, f"{asset}_{kind}_1k.jpg")
    if not os.path.exists(path):
        url = f"https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/{asset}/{asset}_{kind}_1k.jpg"
        req = urllib.request.Request(url, headers={"User-Agent": "swat-art-build/1.0"})
        open(path, "wb").write(urllib.request.urlopen(req, timeout=120).read())
    return np.asarray(Image.open(path).convert("RGB"), np.float32)

def crop(img, w, h, rng, scale=1.0):
    """A w x h px window of a 1K scan; `scale` px of scan per output px."""
    sw, sh = int(w * scale), int(h * scale)
    x = int(rng.integers(0, img.shape[1] - sw)); y = int(rng.integers(0, img.shape[0] - sh))
    sub = Image.fromarray(img[y:y + sh, x:x + sw].astype(np.uint8)).resize((w, h), Image.LANCZOS)
    return np.asarray(sub, np.float32)

def vnoise(rng, w, h, cells, periodic_v=True):
    """Smooth value noise, periodic along v when asked (tiles along a path)."""
    gx, gy = cells
    g = rng.random((gy + 1, gx + 1))
    if periodic_v: g[-1] = g[0]
    im = Image.fromarray((g * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    return np.asarray(im, np.float32) / 255

def grade(rgb, sat, val, tint=(1, 1, 1)):
    lum = rgb @ np.array([.3, .59, .11], np.float32)
    return np.clip((lum[..., None] + (rgb - lum[..., None]) * sat) * val * np.array(tint, np.float32), 0, 255)

def normal_rough(nrm, rough, w, h):
    """Half-res normal (RGB) + roughness (A) cell."""
    n = Image.fromarray(nrm.astype(np.uint8)).resize((w // 2, h // 2), Image.LANCZOS)
    rough = np.broadcast_to(np.asarray(rough, np.float32), (h, w))
    r = Image.fromarray((np.clip(rough, 0, 1) * 255).astype(np.uint8)).resize((w // 2, h // 2), Image.LANCZOS)
    return np.dstack([np.asarray(n, np.float32), np.asarray(r, np.float32)])

def tiles():
    rng = np.random.default_rng(1935)
    dirt, dirt_n = scan("brown_mud_dry"), scan("brown_mud_dry", "nor_gl")
    tracks, tracks_n = scan("muddy_tracks"), scan("muddy_tracks", "nor_gl")
    mud, mud_n = scan("brown_mud_02"), scan("brown_mud_02", "nor_gl")
    leaves, leaves_n = scan("dry_decay_leaves"), scan("dry_decay_leaves", "nor_gl")
    gravel, gravel_n = scan("gravel_floor_02"), scan("gravel_floor_02", "nor_gl")
    grass, grass_n = scan("leafy_grass"), scan("leafy_grass", "nor_gl")
    out = {}   # name -> (col, row, wcells, hcells, width_m, height_m, rgba, nr)
    yy = lambda h, w: np.mgrid[0:h, 0:w][0] / h
    xx = lambda h, w: np.mgrid[0:h, 0:w][1] / w
    # Worn path: compacted bare dirt, darker in the middle where feet go.
    w, h = P, 2 * P
    c = grade(crop(dirt, w, h, rng, .9), .55, 1.0, (1.0, .97, .92)); n = crop(dirt_n, w, h, rng, .9)
    x = xx(h, w); edge = vnoise(rng, w, h, (3, 6)) * .25
    a = np.clip((1 - np.abs(x - .5) * 2 - edge) * 2.6, 0, 1) ** 1.2 * .9
    out["path"] = (4, 0, 1, 2, .95, 2.4, c, a, normal_rough(n, .82 + .1 * vnoise(rng, w, h, (4, 8)), w, h))
    # Tyre ruts: two wet tracks with water in their lowest stretches.
    w, h = 2 * P, 2 * P
    c = grade(crop(tracks, w, h, rng, 1.0), .65, .6); n = crop(tracks_n, w, h, rng, 1.0)
    x = xx(h, w); wob = (vnoise(rng, w, h, (2, 4)) - .5) * .05
    track = np.exp(-((x - .27 - wob) / .085) ** 2) + np.exp(-((x - .73 - wob) / .085) ** 2)
    hump = np.exp(-((x - .5 - wob) / .16) ** 2) * .35                 # grass/mud hump between
    a = np.clip(track * 1.1 + hump, 0, 1) * np.clip((vnoise(rng, w, h, (4, 6)) * .5 + .7), 0, 1) * .95
    water = np.clip((track - .65) * 3, 0, 1) * (vnoise(rng, w, h, (3, 5)) > .58)
    c = c * (1 - .55 * water[..., None]) + np.array([22, 20, 17], np.float32) * .55 * water[..., None]
    rough = .78 - .62 * water
    out["ruts"] = (5, 0, 2, 2, 2.1, 2.6, c, a, normal_rough(n * (1 - water[..., None]) + np.array([128, 128, 255]) * water[..., None], rough, w, h))
    # Puddles: still, muddy water, a soft muddy rim.
    for k, (col, row) in enumerate(((7, 0), (7, 1))):
        w = h = P
        y, x = yy(h, w), xx(h, w)
        r = np.hypot((x - .5) / .42, (y - .5) / .36) + (vnoise(rng, w, h, (5, 5), False) - .5) * .5
        water = np.clip((1 - r) * 5, 0, 1); rim = np.clip((1.18 - r) * 4, 0, 1)
        c = grade(crop(mud, w, h, rng, .8), .6, .55) * (1 - water[..., None]) + np.array([20, 19, 17], np.float32) * water[..., None]
        n = crop(mud_n, w, h, rng, .8) * (1 - water[..., None]) + np.array([128, 128, 255]) * water[..., None]
        out[f"puddle_{'ab'[k]}"] = (col, row, 1, 1, 1.5 if k == 0 else .9, 1.1 if k == 0 else .7, c, rim * .97, normal_rough(n, .75 - .67 * water, w, h))
    # Leaf drift against a wall (wall along the tile top, v = 0).
    w, h = 2 * P, P
    c = grade(crop(leaves, w, h, rng, .7), .45, .62, (1.0, .92, .84)); n = crop(leaves_n, w, h, rng, .7)
    y = yy(h, w); lump = vnoise(rng, w, h, (6, 2), False)
    a = np.clip((1 - y) * 1.6 + (lump - .5) * .9 - .25, 0, 1) * np.clip(np.minimum(xx(h, w), 1 - xx(h, w)) * 7, 0, 1)
    out["leaf_drift"] = (4, 2, 2, 1, 2.2, 1.0, c, a * .95, normal_rough(n, .9, w, h))
    # Large-scale patches over the uniform yard texture.
    def patch(name, col, row, src, src_n, size, sat, val, rough, tint=(1, 1, 1), amax=.85, cells=5):
        w = h = P
        y, x = yy(h, w), xx(h, w)
        r = np.hypot(x - .5, y - .5) * 2 + (vnoise(rng, w, h, (cells, cells), False) - .5) * .8
        a = np.clip((1 - r) * 2.2, 0, 1) * amax
        cc = grade(crop(src, w, h, rng, 1.0), sat, val, tint); nn = crop(src_n, w, h, rng, 1.0)
        out[name] = (col, row, 1, 1, size, size, cc, a, normal_rough(nn, np.full((h, w), rough), w, h))
    patch("mud_patch", 6, 2, mud, mud_n, 1.8, .6, .52, .55)
    patch("litter_patch", 7, 2, leaves, leaves_n, 2.4, .42, .6, .9, (1.0, .92, .84))
    patch("gravel_spill", 0, 3, gravel, gravel_n, 1.8, .4, .85, .85, amax=.8)
    patch("grass_patch", 1, 3, grass, grass_n, 2.2, .15, .9, .92, (1.08, 1.0, .8), amax=.75)
    patch("worn_blob", 3, 3, dirt, dirt_n, 1.4, .55, 1.0, .8, (1.0, .97, .92))
    w = h = P   # wet patch: darkening and low roughness only (rain-soaked low spot)
    y, x = yy(h, w), xx(h, w)
    r = np.hypot(x - .5, y - .5) * 2 + (vnoise(rng, w, h, (5, 5), False) - .5) * .8
    out["wet_patch"] = (2, 3, 1, 1, 2.2, 2.2, np.full((h, w, 3), 28, np.float32), np.clip((1 - r) * 1.6, 0, 1) * .7,
                        normal_rough(np.full((h, w, 3), [128, 128, 255], np.float32), np.full((h, w), .3), w, h))
    return out

def atlas(t):
    color = np.asarray(Image.open(os.path.join(OUT, "decals_color.png")).convert("RGBA"), np.float32)
    nr = np.asarray(Image.open(os.path.join(OUT, "decals_nr.png")).convert("RGBA"), np.float32)
    C = np.zeros((ROWS * P, COLS * P, 4), np.float32); N = np.zeros((ROWS * P // 2, COLS * P // 2, 4), np.float32)
    N[..., 0] = N[..., 1] = 128; N[..., 2] = 255; N[..., 3] = 215
    C[:color.shape[0], :color.shape[1]] = color[:min(color.shape[0], 3 * P), :min(color.shape[1], 4 * P)]
    N[:nr.shape[0], :nr.shape[1]] = nr[:min(nr.shape[0], 3 * P // 2), :min(nr.shape[1], 2 * P)]
    for name, (col, row, wc, hc, wm, hm, rgb, a, nrc) in t.items():
        h, w = a.shape
        # transparent texels carry the tile's mean colour (no dark mip fringes)
        mean = (rgb * a[..., None]).sum((0, 1)) / max(a.sum(), 1e-3)
        rgb = np.where(a[..., None] > .01, rgb, mean)
        C[row * P:row * P + h, col * P:col * P + w] = np.dstack([rgb, a * 255])
        N[row * P // 2:row * P // 2 + h // 2, col * P // 2:col * P // 2 + w // 2] = nrc
    Image.fromarray(np.clip(C, 0, 255).astype(np.uint8), "RGBA").save(os.path.join(OUT, "decals_color.png"))
    Image.fromarray(np.clip(N, 0, 255).astype(np.uint8), "RGBA").save(os.path.join(OUT, "decals_nr.png"))
    lines = [l for l in open(os.path.join(OUT, "atlas.txt")).read().splitlines() if l.strip()]
    keep = [l for l in lines if not l.startswith("atlas ") and not any(l.startswith(f"tile {k} ") for k in t)]
    head = [l for l in keep if l.startswith("#")]; rest = [l for l in keep if not l.startswith("#")]
    out = head + [f"atlas decals_color.png decals_nr.png {COLS} {ROWS}"] + rest
    out += ["tile %s %d %d %d %d %.2f %.2f" % (k, v[0], v[1], v[2], v[3], v[4], v[5]) for k, v in t.items()]
    open(os.path.join(OUT, "atlas.txt"), "w").write("\n".join(out) + "\n")

# ------------------------------------------------------------------ placement
def rot_for(dx, dz):
    return math.degrees(math.atan2(-dx, -dz))

def placement():
    rng = np.random.default_rng(1941)
    L = []
    def along(tile, pts, step, w_jit=0.0, alpha=(.75, .9), layer=0, jitter=.12):
        for (ax, az), (bx, bz) in zip(pts, pts[1:]):
            dx, dz = bx - ax, bz - az; d = math.hypot(dx, dz); n = max(1, int(round(d / step)))
            for k in range(n):
                t = (k + .5) / n
                x = ax + dx * t + rng.normal(0, jitter); z = az + dz * t + rng.normal(0, jitter)
                L.append("decal %s %.2f 0 %.2f 0 1 0 rot=%.0f alpha=%.2f layer=%d%s" % (
                    tile, x, z, rot_for(dx, dz) + rng.normal(0, 4), rng.uniform(*alpha), layer,
                    " scale=%.2f" % rng.uniform(1 - w_jit, 1 + w_jit) if w_jit else ""))
    # Worn foot paths (porch steps, back door, outbuildings, woodpile, lane end).
    paths = [
        [(16.0, -8.6), (15.2, -10.5), (14.6, -12.0)],                     # west front door -> lane end
        [(20.0, -8.6), (21.0, -10.4), (23.5, -11.0), (27.0, -10.3)],      # east front door -> dog pen
        [(21.5, -8.4), (26.0, -9.0), (31.0, -9.4), (35.6, -8.8)],         # porch -> barn big door
        [(17.6, 3.8), (19.5, 4.0), (22.5, 3.5), (25.3, 2.7)],             # back door -> smokehouse
        [(25.6, 2.6), (28.5, 0.5), (31.6, -2.0), (31.7, -5.4)],           # smokehouse -> barn man door
        [(19.2, 3.9), (21.5, 6.5), (22.6, 1.0), (23.3, -1.8)],            # back -> woodpile by the chimney
        [(14.2, 2.2), (11.5, 3.4), (8.8, 3.6)],                           # kitchen side -> tool shed
        [(13.6, -6.5), (8.0, -4.6), (2.2, -2.0)],                         # house -> burn barrel / bus
    ]
    for pts in paths: along("path", pts, 2.1, .1, (.82, .95), 0)
    for (x, z) in ((16.0, -8.9), (20.0, -8.9), (17.6, 4.0), (25.5, 2.6), (31.6, -5.6), (35.8, -8.9), (8.7, 3.6), (18.0, 11.0)):
        L.append("decal worn_blob %.2f 0 %.2f 0 1 0 rot=%.0f scale=%.2f alpha=%.2f layer=0" % (x, z, rng.uniform(0, 360), rng.uniform(1.6, 2.3), rng.uniform(.75, .92)))
    # Tyre ruts from the lane to the bus and the junk cars, puddles in them.
    ruts = [[(4.6, -16.0), (2.6, -12.0), (1.2, -8.0), (.6, -5.2)],       # to the bus on blocks
            [(10.0, -13.0), (9.0, -10.6), (8.2, -9.4)],                     # to the junk car by the house
            [(11.6, -13.4), (11.3, -12.2)]]                                 # to the second junk car
    for pts in ruts: along("ruts", pts, 2.4, 0, (.85, .95), 1, .05)
    for (x, z, s) in ((2.2, -11.0, 1.0), (1.0, -7.2, .8), (9.2, -11.2, .9), (3.4, -13.6, .7), (36.8, -9.6, 1.1), (28.4, -11.0, .8)):
        L.append("decal %s %.2f 0 %.2f 0 1 0 rot=%.0f scale=%.2f alpha=.95 layer=2" % ("puddle_a" if rng.random() < .6 else "puddle_b", x, z, rng.uniform(0, 360), s))
    # Leaf drifts banked against walls and fences (tile top against the wall).
    walls = [  # (x0, z0, x1, z1, outward normal x, z)
        (14.0, -8.05, 22.0, -8.05, 0, -1), (22.0, -6.0, 22.0, 0.0, 1, 0), (14.0, -6.0, 14.0, 3.5, -1, 0), (14.0, 3.55, 19.0, 3.55, 0, 1),
        (32.0, -8.0, 32.0, 4.0, -1, 0), (41.0, -8.0, 41.0, 4.0, 1, 0), (32.0, 4.0, 41.0, 4.0, 0, 1), (32.0, -8.0, 35.0, -8.0, 0, -1),
        (24.0, 3.0, 27.0, 3.0, 0, -1), (24.0, 6.0, 27.0, 6.0, 0, 1), (6.0, 2.0, 6.0, 5.0, -1, 0), (6.0, 5.0, 8.5, 5.0, 0, 1),
        (27.0, -12.0, 30.0, -12.0, 0, -1), (30.0, -12.0, 30.0, -9.0, 1, 0),
    ]
    for (x0, z0, x1, z1, nx, nz) in walls:
        d = math.hypot(x1 - x0, z1 - z0); n = max(1, int(d / 2.4))
        for k in range(n):
            if rng.random() < .3: continue
            t = (k + rng.uniform(.3, .7)) / n
            x = x0 + (x1 - x0) * t + nx * .5; z = z0 + (z1 - z0) * t + nz * .5
            L.append("decal leaf_drift %.2f 0 %.2f 0 1 0 rot=%.0f scale=%.2f alpha=%.2f layer=0" % (
                x, z, rot_for(nx, nz) + 180, rng.uniform(.8, 1.2), rng.uniform(.75, .95)))
    # Large-scale variation of the yard (avoids buildings, lane, paths' ends).
    keep_out = [(13.4, 22.6, -8.6, 3.8), (31.4, 41.6, -8.6, 4.4), (23.8, 27.2, 2.8, 6.2), (5.8, 8.7, 1.8, 5.2),
                (16.8, 19.2, 3.4, 12.1), (-1.2, 1.7, -4.3, 6.3), (26.8, 30.2, -12.2, -8.8)]
    picks = [("litter_patch", .30), ("grass_patch", .25), ("mud_patch", .17), ("wet_patch", .14), ("gravel_spill", .06), ("worn_blob", .08)]
    names = [p[0] for p in picks]; probs = np.array([p[1] for p in picks]); probs /= probs.sum()
    n = 0
    for k in range(900):
        x = rng.uniform(-7.5, 56.5); z = rng.uniform(-23.5, 11.2)
        if any(a0 <= x <= a1 and b0 <= z <= b1 for (a0, a1, b0, b1) in keep_out): continue
        edge = min(x + 8, 57 - x, z + 24, 12 - z)
        near = min(math.hypot(x - bx, z - bz) for (bx, bz) in ((18, -3), (36.5, -2), (25.5, 4.5), (7.2, 3.5)))
        # unmown dead grass and blown litter towards the woods; mud and wear near buildings
        if edge < 7: name = "grass_patch" if rng.random() < .55 else "litter_patch"
        elif near < 9: name = ("mud_patch", "wet_patch", "worn_blob")[int(rng.integers(3))]
        else: name = names[int(rng.choice(len(names), p=probs))]
        if name == "gravel_spill" and not (z < -12 and x < 16): name = "litter_patch"
        if edge > 7 and near > 9 and rng.random() < .45: continue
        L.append("decal %s %.2f 0 %.2f 0 1 0 rot=%.0f scale=%.2f alpha=%.2f layer=0" % (
            name, x, z, rng.uniform(0, 360), rng.uniform(.6, 1.2), rng.uniform(.45, .8)))
        n += 1
        if n >= 260: break
    path = os.path.join(OUT, "placement.txt")
    text = open(path).read()
    if MARK in text:   # drop our old section, keep anything after it (e.g. the newspaper section)
        pre, post = text.split(MARK, 1)
        nxt = post.find("\n# --- generated:")
        text = pre.rstrip() + "\n" + (post[nxt:] if nxt >= 0 else "")
    # The ground goes first (painted before stains and drag marks of the same layer).
    first = text.find("\n# --- generated:")
    body = "\n".join([MARK, "# Layers: 0 ground (paths, drifts, patches), 1 tyre ruts, 2 puddles.", *L, ""])
    text = text.rstrip() + "\n\n" + body if first < 0 else text[:first].rstrip() + "\n\n" + body + text[first:]
    open(path, "w").write(text)
    print("ground decals", len(L))

if __name__ == "__main__":
    t = tiles(); atlas(t); placement()
