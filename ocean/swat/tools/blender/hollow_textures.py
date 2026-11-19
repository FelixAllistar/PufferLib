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
    paper = tuple(int(v) for v in (np.array([226, 220, 200]) * (0.86 + 0.1 * rng.random())))
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
    for _ in range(3):  # old, dark, mostly brown stains: implied, not graphic
        a, col = stain(rng, n, 5, (70, 32, 26), 0.85, 0.2)
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


if __name__ == "__main__":
    flyer_wall(); mattress(); tarp(); stain_sheet()
    print("wrote", sorted(os.listdir(OUT)))
