#!/usr/bin/python3
"""Procedural textures for The Hollow (HOLLOW.md), deterministic (fixed seeds).

    python3 hollow_textures.py OUT_DIR

Writes small PNGs that hollow_props.py / hollow_scene.py embed in GLBs:
  flyer_wall.png   a board wall of fictional missing-person flyers and
                   newspaper clippings. No real names, faces or cases: the
                   "photos" are blank grey silhouettes, the text is generic.
  mattress.png     faded ticking with old, dark, stylised stains.
  tarp.png         weathered blue poly tarp with dark streaks.
  stain_floor.png  RGBA decal sheet: dried stains and a drag smear.
Stains are simple soft shapes made here: no photographic sources.
"""
import os, sys, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else "hollow_textures"
os.makedirs(OUT, exist_ok=True)


def font(size):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default(size=size)


def serif(size):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf"):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return font(size)


def noise(rng, n, scale):
    small = rng.random((max(2, n // scale), max(2, n // scale)))
    img = Image.fromarray((small * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC)
    return np.asarray(img, dtype=np.float32) / 255.0


def flyer(rng, w, h, kind):
    paper = tuple(int(v) for v in (np.array([196, 188, 166]) * (0.80 + 0.1 * rng.random())))
    im = Image.new("RGB", (w, h), paper); d = ImageDraw.Draw(im)
    if kind == "missing":
        d.text((w // 2, int(h * .09)), "MISSING", fill=(25, 20, 18), font=font(int(w * .19)), anchor="mm")
        # Photo box: a blank grey head-and-shoulders silhouette, never a face.
        bx0, by0, bx1, by1 = int(w * .2), int(h * .18), int(w * .8), int(h * .58)
        d.rectangle((bx0, by0, bx1, by1), fill=(150, 148, 142))
        cx = (bx0 + bx1) // 2
        d.ellipse((cx - w * .1, by0 + h * .06, cx + w * .1, by0 + h * .2), fill=(92, 90, 86))
        d.pieslice((cx - w * .22, by0 + h * .21, cx + w * .22, by1 + h * .14), 180, 360, fill=(92, 90, 86))
        d.text((w // 2, int(h * .66)), "HAVE YOU SEEN HER?", fill=(30, 26, 24), font=font(int(w * .075)), anchor="mm")
        for k in range(5):  # body text as grey bars
            y = int(h * (.73 + .045 * k)); x1 = int(w * (.85 - .25 * rng.random()))
            d.rectangle((int(w * .15), y, x1, y + int(h * .018)), fill=(95, 90, 85))
        d.text((w // 2, int(h * .96)), "CALL 304-555-0100", fill=(30, 26, 24), font=font(int(w * .06)), anchor="mm")
    else:
        heads = ["WOMAN STILL MISSING", "SEARCH CALLED OFF", "NO LEADS IN CASE", "FAMILY ASKS FOR HELP", "HIKER NOT FOUND"]
        d.text((int(w * .05), int(h * .03)), heads[int(rng.integers(len(heads)))], fill=(20, 18, 16), font=serif(int(w * .07)))
        cols = 3 if w > h * .8 else 2
        for c in range(cols):
            x0 = int(w * (.05 + c * .92 / cols)); x1 = x0 + int(w * .92 / cols) - 8
            for k in range(int(h * .8 / 9)):
                y = int(h * .15) + k * 9
                if y > h - 10: break
                d.line((x0, y, x1 - int(rng.random() * 12), y), fill=(110, 105, 98), width=3)
    im = im.rotate(float(rng.normal(0, 2.5)), expand=False, fillcolor=paper)
    # Age: yellowing, water lines, dirt.
    a = np.asarray(im, dtype=np.float32)
    n = noise(rng, max(w, h), 24)[:h, :w]
    a *= (0.82 + 0.22 * n[..., None]) * np.array([1.0, .96, .86])
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


def flyer_wall():
    rng = np.random.default_rng(1938)
    W, H = 1024, 1024
    # Board background: vertical grey boards papered with old newspaper.
    bg = np.zeros((H, W, 3), np.float32)
    for x0 in range(0, W, 128):
        tone = 0.30 + 0.06 * rng.random()
        bg[:, x0:x0 + 128] = np.array([tone, tone * .97, tone * .9]) * 255
        bg[:, x0:x0 + 3] *= .45
    bg *= (0.75 + 0.35 * noise(rng, W, 16))[..., None]
    im = Image.fromarray(np.clip(bg, 0, 255).astype(np.uint8))
    # Pinned papers, overlapping, slightly rotated; a red string between some.
    pins = []
    items = [("missing", 200, 280)] * 7 + [("clip", 230, 170)] * 6 + [("clip", 160, 220)] * 4
    order = rng.permutation(len(items))
    for k in order:
        kind, w, h = items[k]
        f = flyer(rng, w, h, kind)
        x = int(rng.integers(10, W - w - 10)); y = int(rng.integers(30, H - h - 30))
        shadow = Image.new("L", (w, h), 0)
        im.paste((10, 10, 10), (x + 6, y + 8), Image.new("L", (w, h), 130))
        im.paste(f, (x, y))
        pins.append((x + w // 2, y + 12))
    d = ImageDraw.Draw(im)
    link = [pins[i] for i in rng.permutation(len(pins))[:9]]
    for a, b in zip(link, link[1:]):
        d.line((a, b), fill=(120, 22, 18), width=3)
    for p in pins:
        d.ellipse((p[0] - 5, p[1] - 5, p[0] + 5, p[1] + 5), fill=(170, 160, 40))
    im = im.filter(ImageFilter.GaussianBlur(0.6))
    im.save(os.path.join(OUT, "flyer_wall.png"))


def linoleum():
    """Worn printed linoleum rug: faded tile pattern, a border, wear paths."""
    rng = np.random.default_rng(1952)
    n = 512; im = Image.new("RGB", (n, n), (120, 104, 78)); d = ImageDraw.Draw(im)
    for i in range(8):
        for j in range(8):
            x, y = 32 + i * 56, 32 + j * 56
            c = (98, 84, 62) if (i + j) % 2 else (136, 118, 88)
            d.rectangle((x, y, x + 54, y + 54), fill=c)
            d.ellipse((x + 12, y + 12, x + 42, y + 42), outline=(84, 58, 42), width=4)
    d.rectangle((8, 8, n - 9, n - 9), outline=(70, 40, 30), width=14)
    a = np.asarray(im, np.float32)
    yy, xx = np.mgrid[0:n, 0:n] / n
    wear = np.exp(-((xx - .5) / .18) ** 2) * .5 + .5 * noise(rng, n, 10)
    a = a * (1 - .35 * wear[..., None]) + np.array([70, 58, 44]) * .35 * wear[..., None]
    Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).save(os.path.join(OUT, "linoleum.png"))


def stain(rng, n, blobs, color, alpha_max, spread=0.12):
    """Soft, irregular, dried-looking stain mask (n x n) and colour."""
    yy, xx = np.mgrid[0:n, 0:n] / n
    m = np.zeros((n, n), np.float32)
    for _ in range(blobs):
        cx, cy = 0.5 + rng.normal(0, spread), 0.5 + rng.normal(0, spread)
        r = 0.05 + 0.13 * rng.random()
        m = np.maximum(m, np.clip(1 - ((xx - cx) ** 2 + (yy - cy) ** 2) / r ** 2, 0, 1))
    m *= 0.55 + 0.6 * noise(rng, n, 10)
    m = np.clip((m - 0.18) * 2.2, 0, 1)
    edge = np.clip(m * 4, 0, 1) - np.clip(m * 4 - 1, 0, 1)  # darker dried rim
    return m * alpha_max, np.array(color, np.float32) * (1 - .35 * edge[..., None])


def mattress():
    rng = np.random.default_rng(77)
    n = 512
    base = np.ones((n, n, 3), np.float32) * np.array([196, 188, 166])
    stripes = (np.sin(np.arange(n) / n * 2 * math.pi * 26) > .6)[None, :, None]
    base = np.where(stripes, base * np.array([.72, .74, .82]), base)
    base *= (0.8 + 0.25 * noise(rng, n, 20))[..., None]
    for _ in range(2):  # old, dark, mostly brown stains: implied, not graphic
        a, col = stain(rng, n, 4, (58, 36, 28), 0.75, 0.16)
        base = base * (1 - a[..., None]) + col * a[..., None]
    a, col = stain(rng, n, 4, (120, 100, 60), 0.4, 0.25)  # water rings
    base = base * (1 - a[..., None]) + col * a[..., None]
    Image.fromarray(np.clip(base, 0, 255).astype(np.uint8)).save(os.path.join(OUT, "mattress.png"))


def tarp():
    rng = np.random.default_rng(5)
    n = 512
    base = np.ones((n, n, 3), np.float32) * np.array([38, 70, 112])
    weave = ((np.arange(n)[:, None] // 3 + np.arange(n)[None, :] // 3) % 2) * 6
    base += weave[..., None]
    base *= (0.75 + 0.35 * noise(rng, n, 12))[..., None]
    a, col = stain(rng, n, 6, (40, 22, 18), 0.75, 0.18)
    base = base * (1 - a[..., None]) + col * a[..., None]
    a, col = stain(rng, n, 5, (110, 96, 70), 0.5, 0.3)  # dried mud
    base = base * (1 - a[..., None]) + col * a[..., None]
    Image.fromarray(np.clip(base, 0, 255).astype(np.uint8)).save(os.path.join(OUT, "tarp.png"))


def stain_sheet():
    """RGBA 1024x512: [0] round dried pool, [1] scuffed smear, [2] two-track drag mark (tileable along v)."""
    rng = np.random.default_rng(13)
    W, H = 1024, 512
    out = np.zeros((H, W, 4), np.float32)
    a, col = stain(rng, 512, 7, (52, 24, 20), 0.9, 0.12)
    out[:, :512, :3] = col; out[:, :512, 3] = a
    # Drag mark: two scuffed furrows with dark smears, along the tile's v axis.
    n = 512; yy, xx = np.mgrid[0:n, 0:n] / n
    wob = 0.03 * np.sin(yy * 2 * math.pi * 2 + 1.3)
    furrow = np.exp(-((xx - 0.38 - wob) / 0.05) ** 2) + np.exp(-((xx - 0.62 - wob) / 0.05) ** 2)
    smear = np.exp(-((xx - 0.5 - wob) / 0.14) ** 2) * (noise(rng, n, 14) > .55)
    m = np.clip(furrow * (0.55 + 0.45 * noise(rng, n, 8)) + 0.5 * smear, 0, 1) * 0.85
    c = np.dstack([np.full((n, n), 60.), np.full((n, n), 44.), np.full((n, n), 34.)])
    c = c * (1 - 0.5 * smear[..., None]) + np.array([58, 26, 20]) * 0.5 * smear[..., None]
    out[:, 512:, :3] = c; out[:, 512:, 3] = m
    rgba = np.dstack([np.clip(out[..., :3], 0, 255), np.clip(out[..., 3] * 255, 0, 255)]).astype(np.uint8)
    Image.fromarray(rgba, "RGBA").save(os.path.join(OUT, "stain_sheet.png"))


def decal_atlas(out_dir):
    """Surface-decal atlas for surface_decals.h (4x2 cells of 256 px):
    stain_pool, stain_smear, drips, boots / mud_splash, grime_base(2 wide) / drag (col 3, 2 tall)."""
    rng = np.random.default_rng(2026)
    os.makedirs(out_dir, exist_ok=True)
    C, R, P = 4, 3, 256
    rgba = np.zeros((R * P, C * P, 4), np.float32)
    def put(cx, cy, rgb, a):
        h, w = a.shape
        rgba[cy * P:cy * P + h, cx * P:cx * P + w, :3] = rgb
        rgba[cy * P:cy * P + h, cx * P:cx * P + w, 3] = a
    a, col = stain(rng, P, 7, (46, 22, 18), 0.92, 0.11); put(0, 0, col, a)           # stain_pool (dried, dark)
    a, col = stain(rng, P, 5, (52, 30, 22), 0.75, 0.2)
    yy, xx = np.mgrid[0:P, 0:P] / P
    a *= np.clip(1.4 - np.abs(yy - .5) * 3.2, 0, 1); put(1, 0, col, a)                 # stain_smear
    m = np.zeros((P, P), np.float32)
    for _ in range(26):
        cx, cy, r = rng.uniform(.15, .85), rng.uniform(.1, .9), rng.uniform(.01, .04)
        m = np.maximum(m, np.clip(1 - ((xx - cx) ** 2 + (yy - cy) ** 2) / r ** 2, 0, 1))
    put(2, 0, np.broadcast_to(np.array([50, 24, 20], np.float32), (P, P, 3)), np.clip(m * 1.6, 0, .9))   # drips
    m = np.zeros((P, P), np.float32)                                                   # boots: muddy prints
    for k in range(4):
        cx = .35 + .3 * (k % 2); cy = .15 + .22 * k
        m = np.maximum(m, np.clip(1 - ((xx - cx) / .07) ** 2 - ((yy - cy) / .1) ** 2, 0, 1) * (noise(rng, P, 8) > .35))
    put(0, 1, np.broadcast_to(np.array([44, 34, 26], np.float32), (P, P, 3)), np.clip(m * 1.3, 0, .8))
    a, col = stain(rng, P, 6, (58, 46, 34), 0.6, 0.25); put(1, 1, col, a)               # mud_splash
    g = np.clip((1 - yy) ** 1.6 * (0.55 + 0.6 * noise(rng, P, 10)), 0, 1)
    gw = np.concatenate([g, g[:, ::-1]], axis=1)[:, :P] if False else g
    put(2, 1, np.broadcast_to(np.array([40, 36, 30], np.float32), (P, P, 3)), gw * .8)  # grime_base
    # drag: two furrows and a smear, tileable along v (cell col 3, rows 0..1).
    H = 2 * P; yy2, xx2 = np.mgrid[0:H, 0:P] / np.array([H, P])[:, None, None]
    wob = .03 * np.sin(yy2 * 2 * math.pi * 2 + 1.3)
    furrow = np.exp(-((xx2 - .36 - wob) / .05) ** 2) + np.exp(-((xx2 - .64 - wob) / .05) ** 2)
    smear = np.exp(-((xx2 - .5 - wob) / .12) ** 2) * (np.asarray(Image.fromarray((rng.random((16, 8)) * 255).astype(np.uint8)).resize((P, H), Image.BICUBIC)) / 255. > .5)
    mm = np.clip(furrow * .95 + .75 * smear, 0, .95)
    colr = np.dstack([np.full((H, P), 120.), np.full((H, P), 100.), np.full((H, P), 76.)]) * (1 - .7 * smear[..., None]) + np.array([48, 22, 18]) * .7 * smear[..., None]
    rgba[:H, 3 * P:4 * P, :3] = colr; rgba[:H, 3 * P:4 * P, 3] = mm
    # Old newspaper pasted on the inside of the boards (a real boxed-house
    # practice): yellowed newsprint, headline bars, columns, torn edges.
    for k in range(2):
        im = Image.new("RGB", (P, P), (182, 170, 138)); d = ImageDraw.Draw(im)
        d.rectangle((10, 12, P - 14, 34), fill=(70, 64, 54))
        for c in range(3):
            x0 = 10 + c * 80
            for j in range(22):
                y = 46 + j * 9
                if rng.random() < .08: continue
                d.line((x0, y, x0 + 70 - int(rng.random() * 10), y), fill=(118, 108, 90), width=3)
        if k: d.rectangle((96, 60, 170, 120), fill=(120, 112, 96))
        n = noise(rng, P, 12)
        a = np.asarray(im, np.float32) * (0.72 + 0.35 * n[..., None]) * np.array([1, .97, .9])
        torn = np.clip((noise(rng, P, 6) - .18) * 4, 0, 1)
        yy, xx = np.mgrid[0:P, 0:P] / P
        edge = np.clip(np.minimum.reduce([xx, 1 - xx, yy, 1 - yy]) * 14 - torn * .6, 0, 1)
        stain_a, stain_c = stain(rng, P, 3, (110, 84, 52), .55, .3)   # water stains
        a = a * (1 - stain_a[..., None]) + stain_c * stain_a[..., None]
        put(k, 2, a, edge * .92)
    # Transparent texels carry the cell's mean colour (no dark mip fringes).
    out = rgba.copy()
    for cy in range(R):
        for cx in range(C):
            cell = out[cy * P:(cy + 1) * P, cx * P:(cx + 1) * P]
            w = cell[..., 3:4]; mean = (cell[..., :3] * w).sum((0, 1)) / max(w.sum(), 1e-3)
            cell[..., :3] = cell[..., :3] * (w > .01) + mean * (w <= .01)
    img = np.dstack([np.clip(out[..., :3], 0, 255), np.clip(out[..., 3] * 255, 0, 255)]).astype(np.uint8)
    Image.fromarray(img, "RGBA").save(os.path.join(out_dir, "decals_color.png"))
    nr = np.zeros((R * P // 2, C * P // 2, 4), np.uint8); nr[..., 0] = 128; nr[..., 1] = 128; nr[..., 2] = 255; nr[..., 3] = 215
    nr[:P // 2, :3 * P // 2, 3] = 120  # stains and drips keep a faint dried sheen
    Image.fromarray(nr, "RGBA").save(os.path.join(out_dir, "decals_nr.png"))
    with open(os.path.join(out_dir, "atlas.txt"), "w") as f:
        f.write("# Generated by tools/blender/hollow_textures.py. Do not edit.\n")
        f.write("atlas decals_color.png decals_nr.png 4 3\n")
        for row in (("stain_pool", 0, 0, 1, 1, .9, .9), ("stain_smear", 1, 0, 1, 1, .8, .8), ("drips", 2, 0, 1, 1, .6, .6),
                    ("boots", 0, 1, 1, 1, .7, .7), ("mud_splash", 1, 1, 1, 1, .8, .8), ("grime_base", 2, 1, 1, 1, 1.0, .45),
                    ("drag", 3, 0, 1, 2, .75, 2.6), ("newsprint_a", 0, 2, 1, 1, .62, .62), ("newsprint_b", 1, 2, 1, 1, .62, .62)):
            f.write("tile %s %d %d %d %d %.2f %.2f\n" % row)


def newspaper_placement(out_dir):
    """Rewrite the generated section of placement.txt: newsprint sheets on the
    interior faces of the house's board walls, skipping openings, the stair
    closet, the fireplace and the flyer wall."""
    rng = np.random.default_rng(1938)
    F, U = .30, 2.90
    # (axis, fixed, a, b, y0, height, normal sign, skip[(lo, hi)])
    faces = [
        ("z", -6, 14, 18, F, 2.45, 1, [(15.5, 16.5), (14.5, 15.3)]), ("z", -6, 18, 22, F, 2.45, 1, [(19.5, 20.5), (20.8, 21.6)]),
        ("z", 0, 14, 18, F, 2.45, -1, [(15.5, 16.5)]), ("x", 14, -6, 0, F, 2.45, 1, [(-4.0, -3.2)]),
        ("x", 22, -6, -1.3, F, 2.45, -1, [(-3.9, -2.1)]), ("x", 18, -6, 0, F, 2.45, -1, [(-5.1, -4.1)]),
        ("x", 18, -6, -1.3, F, 2.45, 1, [(-5.1, -4.1)]),
        ("x", 14, 0, 3.5, F, 2.2, 1, [(1.4, 2.2)]), ("z", 3.5, 14, 19, F, 2.2, -1, [(17.1, 18.1), (14.9, 15.7)]), ("x", 19, 0, 3.5, F, 2.2, -1, []),
        ("z", -6, 14, 18, U, 2.15, 1, [(15.0, 15.8), (16.6, 17.4)]), ("z", -6, 18, 22, U, 2.15, 1, [(19.2, 20.0), (20.8, 21.6)]),
        ("z", 0, 14, 18, U, 2.15, -1, [(15.6, 16.4)]), ("x", 14, -6, 0, U, 2.15, 1, [(-3.35, -2.65)]),
        ("x", 18, -6, 0, U, 2.15, -1, [(-3.5, -2.5)]), ("x", 18, -6, 0, U, 2.15, 1, [(-3.5, -2.5)]),
        ("x", 22, -3.0, 0, U, 2.15, -1, [(-1.55, -.85)]), ("z", 0, 18, 22, U, 2.15, -1, []),
    ]
    lines = []
    for (axis, fixed, a, b, y0, h, sgn, skip) in faces:
        off = fixed + sgn * .0125
        t = a + .35
        while t < b - .3:
            for row in range(4):
                y = y0 + .42 + row * .55 + rng.normal(0, .04)
                if y > y0 + h - .28 or rng.random() < .1: continue    # torn away / never pasted
                if any(lo - .33 < t < hi + .33 for lo, hi in skip): continue
                tile = "newsprint_a" if rng.random() < .55 else "newsprint_b"
                x, z = (t, off) if axis == "z" else (off, t)
                nx, nz = (0, sgn) if axis == "z" else (sgn, 0)
                lines.append("decal %s %.3f %.3f %.3f %d 0 %d rot=%.0f alpha=%.2f layer=0" % (
                    tile, x, y, z, nx, nz, rng.normal(0, 4), rng.uniform(.65, .92)))
            t += .58 + rng.normal(0, .03)
    path = os.path.join(out_dir, "placement.txt"); marker = "# --- generated: newspaper on the house's inside walls (hollow_textures.py) ---"
    text = open(path).read().split(marker)[0].rstrip() + "\n\n" + marker + "\n" + "\n".join(lines) + "\n"
    open(path, "w").write(text)
    print("newspaper decals", len(lines))


if __name__ == "__main__":
    flyer_wall(); mattress(); tarp(); stain_sheet(); linoleum()
    if len(sys.argv) > 2: decal_atlas(sys.argv[2]); newspaper_placement(sys.argv[2])
    print("wrote", sorted(os.listdir(OUT)))
