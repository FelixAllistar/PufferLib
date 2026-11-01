"""Thumbnails for "Who is ACTUALLY the strongest trainer in Gen 1?" (2026-11-01), Pokemon Stadium style.
Game sprites from the player's own ROM import (Gen1Recomp's extracted assets), colorized; the arena, lights and type
are drawn here (no generated imagery). Writes several variants side by side so they can be compared at YouTube size.
Usage: uv run --with pillow python thumbnail.py SPRITES_DIR OUT_DIR
"""
import math
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

SPR = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)
W, H = 1280, 720
IMPACT = '/mnt/c/Windows/Fonts/impact.ttf'
BLACK = '/mnt/c/Windows/Fonts/ariblk.ttf'

# 4-shade Game Boy sprite -> colors (lightest is transparent)
# darkest, dark, light, lightest (the lightest only where it is inside the sprite, e.g. Oak's lab coat)
PALETTES = {
    'blue': [(20, 30, 70), (40, 110, 240), (140, 200, 255), (255, 255, 255)],
    'oak': [(40, 30, 30), (170, 120, 80), (240, 210, 170), (255, 255, 255)],
    'char': [(90, 20, 0), (250, 110, 20), (255, 200, 80), (255, 250, 220)],
    'kazam': [(80, 50, 0), (240, 180, 20), (255, 230, 120), (255, 255, 230)],
    'lorelei': [(60, 10, 50), (220, 70, 150), (255, 190, 225), (255, 255, 255)],
}


def sprite(path, palette, scale):
    im = Image.open(path).convert('L')
    src = im.load()
    levels = sorted({src[x, y] for y in range(im.height) for x in range(im.width)})
    light = levels[-1]
    # background = the lightest shade connected to the border (white inside the sprite stays white)
    bg, stack = set(), [(x, y) for x in range(im.width) for y in (0, im.height - 1)] + \
        [(x, y) for y in range(im.height) for x in (0, im.width - 1)]
    while stack:
        x, y = stack.pop()
        if (x, y) in bg or not (0 <= x < im.width and 0 <= y < im.height) or src[x, y] != light:
            continue
        bg.add((x, y))
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    out = Image.new('RGBA', im.size, (0, 0, 0, 0))
    pal = PALETTES[palette]
    for y in range(im.height):
        for x in range(im.width):
            if (x, y) not in bg:
                v = levels.index(src[x, y])
                out.putpixel((x, y), pal[min(v, 3) if len(levels) == 4 else min(v, 2)] + (255,))
    return out.resize((im.width * scale, im.height * scale), Image.NEAREST)


def outlined(img, size=8):
    """A thick dark outline around a sprite so it pops off the background."""
    pad = size * 3
    canvas = Image.new('RGBA', (img.width + 2 * pad, img.height + 2 * pad), (0, 0, 0, 0))
    canvas.paste(img, (pad, pad), img)
    a = canvas.split()[3]
    glow = a.filter(ImageFilter.MaxFilter(size * 2 + 1)).filter(ImageFilter.GaussianBlur(size))
    ring = a.filter(ImageFilter.MaxFilter(size + 1 if size % 2 == 0 else size))
    base = Image.new('RGBA', canvas.size, (255, 240, 160, 0))
    base.putalpha(glow.point(lambda v: min(255, v * 2)))
    edge = Image.new('RGBA', canvas.size, (255, 255, 255, 255))
    edge.putalpha(ring)
    base.alpha_composite(edge)
    base.alpha_composite(canvas)
    return base


def arena():
    """Pokemon Stadium look: night sky, tiered stands full of camera flashes, spotlights, the round battle floor
    seen at an angle with a Poke Ball emblem in the middle."""
    im = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=(int(20 + 40 * t), int(16 + 30 * t), int(60 + 70 * t)))
    # stands: three tiers, darker bands with crowd dots
    for i, (y0, col) in enumerate(((250, (34, 30, 80)), (320, (44, 38, 96)), (390, (56, 48, 112)))):
        d.ellipse([-300, y0, W + 300, y0 + 700], fill=col)
        for x in range(0, W, 7):
            for r in range(0, 60, 6):
                if (x * 31 + r * 17 + i * 7) % 11 < 3:
                    c = [(240, 80, 80), (250, 220, 90), (120, 200, 255), (255, 255, 255)][(x + r) % 4]
                    d.point((x, y0 + 30 + r + int(((x - W / 2) / W) ** 2 * 120)), fill=c)
    glow = Image.new('L', (W, H), 0)
    g = ImageDraw.Draw(glow)
    for cx in (160, 640, 1120):  # spotlight cones
        g.polygon([(cx - 25, -10), (cx + 25, -10), (cx + 240, H), (cx - 240, H)], fill=70)
    glow = glow.filter(ImageFilter.GaussianBlur(28))
    im = Image.composite(Image.new('RGB', (W, H), (220, 235, 255)), im, glow)
    d = ImageDraw.Draw(im)
    # floor: blue-gray field with a white ring, Poke Ball emblem flattened by perspective
    d.ellipse([-80, 440, W + 80, 900], fill=(70, 90, 140), outline=(255, 255, 255), width=10)
    d.ellipse([90, 480, W - 90, 860], fill=(90, 120, 180))
    cx, cy, rx, ry = W // 2, 640, 300, 110
    d.pieslice([cx - rx, cy - ry, cx + rx, cy + ry], 180, 360, fill=(220, 40, 50))
    d.pieslice([cx - rx, cy - ry, cx + rx, cy + ry], 0, 180, fill=(240, 240, 240))
    d.rectangle([cx - rx, cy - 9, cx + rx, cy + 9], fill=(20, 20, 30))
    d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], outline=(20, 20, 30), width=12)
    d.ellipse([cx - 70, cy - 28, cx + 70, cy + 28], fill=(20, 20, 30))
    d.ellipse([cx - 46, cy - 17, cx + 46, cy + 17], fill=(240, 240, 240))
    # top lens flares
    flare = Image.new('L', (W, H), 0)
    f = ImageDraw.Draw(flare)
    for cx in (160, 640, 1120):
        f.ellipse([cx - 40, -40, cx + 40, 40], fill=255)
    flare = flare.filter(ImageFilter.GaussianBlur(18))
    return Image.composite(Image.new('RGB', (W, H), (255, 255, 255)), im, flare)


def text(im, xy, s, font, size, fill, stroke, width, anchor='mm', shadow=True):
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(font, size)
    if shadow:
        d.text((xy[0] + 8, xy[1] + 10), s, font=f, fill=(0, 0, 0), anchor=anchor, stroke_width=width,
               stroke_fill=(0, 0, 0))
    d.text(xy, s, font=f, fill=fill, anchor=anchor, stroke_width=width, stroke_fill=stroke)


def cross(im, box):
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = box
    for w, c in ((34, (0, 0, 0)), (22, (230, 30, 30))):
        d.line([(x0, y0), (x1, y1)], fill=c, width=w)
        d.line([(x0, y1), (x1, y0)], fill=c, width=w)


YELLOW, BLUE_OUT = (255, 214, 0), (24, 56, 160)
oak = outlined(sprite(SPR / 'trainers/prof.oak.png', 'oak', 8), 6)
big_oak = outlined(sprite(SPR / 'trainers/prof.oak.png', 'oak', 10), 7)
blue = outlined(sprite(SPR / 'trainers/rival3.png', 'blue', 9), 6)
char = outlined(sprite(SPR / 'front/charizard.png', 'char', 9), 6)
kazam = outlined(sprite(SPR / 'front/alakazam.png', 'kazam', 7), 6)
lorelei = outlined(sprite(SPR / 'trainers/lorelei.png', 'lorelei', 8), 6)

variants = {}

# A: WHO'S #1? - Oak crossed out on the left, Blue with Charizard on the right
im = arena()
im.paste(big_oak, (20, 200), big_oak)
cross(im, (60, 250, 470, 690))
im.paste(char, (720, 200), char)
im.paste(blue, (880, 230), blue)
text(im, (640, 110), "WHO'S #1?", IMPACT, 190, YELLOW, BLUE_OUT, 12)
variants['A'] = im

# B: NOT OAK. - one huge line, Oak crossed
im = arena()
im.paste(big_oak, (365, 190), big_oak)
cross(im, (400, 240, 880, 700))
text(im, (640, 120), 'NOT OAK.', IMPACT, 230, (255, 255, 255), (0, 0, 0), 14)
variants['B'] = im

# C: ACTUAL #1 - Blue big, Charizard and Alakazam, gold "#1"
im = arena()
im.paste(kazam, (80, 300), kazam)
im.paste(char, (300, 210), char)
im.paste(blue, (740, 230), blue)
text(im, (1110, 150), '#1', IMPACT, 260, YELLOW, BLUE_OUT, 14)
text(im, (420, 105), 'ACTUALLY', IMPACT, 150, (255, 255, 255), (0, 0, 0), 12)
variants['C'] = im

# D: OAK vs BLUE, Stadium versus screen
im = arena()
tint = Image.new('RGBA', (W, H), (0, 0, 0, 0))
td = ImageDraw.Draw(tint)
td.polygon([(0, 0), (700, 0), (580, H), (0, H)], fill=(200, 30, 40, 150))
td.polygon([(700, 0), (W, 0), (W, H), (580, H)], fill=(30, 80, 220, 150))
td.line([(700, 0), (580, H)], fill=(255, 255, 255, 255), width=14)
im = im.convert('RGBA')
im.alpha_composite(tint)
im = im.convert('RGB')
im.paste(oak, (70, 240), oak)
im.paste(blue, (840, 220), blue)
text(im, (640, 380), 'VS', IMPACT, 220, YELLOW, (0, 0, 0), 14)
text(im, (300, 110), '#1?', IMPACT, 170, (255, 255, 255), (0, 0, 0), 12)
text(im, (980, 110), '#1?', IMPACT, 170, (255, 255, 255), (0, 0, 0), 12)
variants['D'] = im

for k, im in variants.items():
    im.save(OUT / f'thumb_{k}.png')
# how they read at YouTube's home-feed size (~360x202), side by side
small = [im.resize((360, 202), Image.LANCZOS) for im in variants.values()]
sheet = Image.new('RGB', (360 * 2 + 20, 202 * 2 + 20), (15, 15, 15))
for i, s in enumerate(small):
    sheet.paste(s, ((i % 2) * 380, (i // 2) * 222))
sheet.save(OUT / 'thumbs_small.png')
print('thumbnails ->', OUT)
