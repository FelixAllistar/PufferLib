"""Shared drawing for the Gen 1 video (2026-11-01): Pokemon Stadium style arena, ROM sprites colored with the
pokered-gbc palettes the Gen1Recomp port ships (Gen 1 species assignments, per-trainer palettes), glow outlines and
outlined Impact type. Used by thumbnail.py and scenes.py. No generated imagery.
"""
import functools
import os
import pathlib
import re

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HOME = pathlib.Path.home()
SPR = pathlib.Path(os.environ.get('GEN1_SPRITES', HOME / '.local/share/love/pokemon-love2d/red/assets/generated/battle'))
PAL_LUA = pathlib.Path(os.environ.get('GEN1_PALETTES', HOME / 'puffertank/gen1recomp/data/palettes_gbc.lua'))
IMPACT = '/mnt/c/Windows/Fonts/impact.ttf'
BLACK = '/mnt/c/Windows/Fonts/ariblk.ttf'
YELLOW, BLUE_OUT, WHITE, INK = (255, 214, 0), (24, 56, 160), (255, 255, 255), (0, 0, 0)
RED, GREEN, GRAY = (235, 60, 60), (80, 220, 120), (170, 180, 200)

# trainer sprite file and palette name per trainer class (trainers.json class)
TRAINER = {'RIVAL3': ('rival3', 'GARY3'), 'RIVAL2': ('rival2', 'GARY2'), 'RIVAL1': ('rival1', 'GARY1'),
           'PROF_OAK': ('prof.oak', 'OAK'), 'LT_SURGE': ('lt.surge', 'LT_SURGE'), 'FISHER': ('fisher', 'FISHERMAN'),
           'JR_TRAINER_M': ('jr.trainerm', 'JR_TRAINER_M'), 'JR_TRAINER_F': ('jr.trainerf', 'JR_TRAINER_F'),
           'PSYCHIC_TR': ('psychic', 'PSYCHIC')}
FILE_FIX = {'MR_MIME': 'mr.mime', 'NIDORAN_F': 'nidoranf', 'NIDORAN_M': 'nidoranm', 'FARFETCH_D': 'farfetchd'}


@functools.cache
def palettes():
    """{'palettes': {NAME: [4 RGB, lightest first]}, 'pokemon': {SPECIES: NAME}} from the port's Lua table."""
    src = PAL_LUA.read_text()
    a, b = src.index('  palettes = {'), src.index('  pokemon = {')
    pals = {}
    for m in re.finditer(r'\n    ([A-Z_0-9]+) = \{((?:\s*\{[^}]*\},?)+)\s*\}', src[a:b]):
        pals[m.group(1)] = [tuple(map(int, c.split(','))) for c in re.findall(r'\{\s*([\d,\s]+?)\s*\}', m.group(2))]
    end = src.index('\n  }', b)
    mons = dict(re.findall(r'\n    ([A-Z_0-9]+) = "([A-Z_0-9]+)"', src[b:end]))
    return {'palettes': pals, 'pokemon': mons}


def sprite(kind, name, scale, palette=None):
    """kind 'front' (species, e.g. CHARIZARD) or 'trainer' (trainers.json class). Background (the lightest shade
    touching the border) is transparent; lightest shade inside the sprite stays white."""
    p = palettes()
    if kind == 'trainer':
        fname, pal = TRAINER.get(name, (name.lower().replace('_', ''), name))
        if pal not in p['palettes']:
            pal = pal.replace('_', '')
        path = SPR / 'trainers' / f'{fname}.png'
    else:
        path = SPR / 'front' / f"{FILE_FIX.get(name, name.lower())}.png"
        pal = p['pokemon'].get(name, 'GREYMON')
    colors = p['palettes'].get(palette or pal) or p['palettes']['GREYMON']
    im = Image.open(path).convert('L')
    src = im.load()
    levels = sorted({src[x, y] for y in range(im.height) for x in range(im.width)})
    light = levels[-1]
    bg, stack = set(), [(x, y) for x in range(im.width) for y in (0, im.height - 1)] + \
        [(x, y) for y in range(im.height) for x in (0, im.width - 1)]
    while stack:
        x, y = stack.pop()
        if (x, y) in bg or not (0 <= x < im.width and 0 <= y < im.height) or src[x, y] != light:
            continue
        bg.add((x, y))
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    out = Image.new('RGBA', im.size, (0, 0, 0, 0))
    # darkest gray level -> palette color 3 (black) ... lightest -> color 0 (white)
    for y in range(im.height):
        for x in range(im.width):
            if (x, y) not in bg:
                v = levels.index(src[x, y])
                k = 3 - round(v * 3 / max(1, len(levels) - 1))
                out.putpixel((x, y), colors[k] + (255,))
    bbox = out.getbbox()
    out = out.crop(bbox) if bbox else out
    return out.resize((out.width * scale, out.height * scale), Image.NEAREST)


def outlined(img, size=6, ring=(255, 255, 255), glow=(255, 240, 160)):
    """White ring plus a soft glow around a sprite so it pops off the arena."""
    pad = size * 3
    canvas = Image.new('RGBA', (img.width + 2 * pad, img.height + 2 * pad), (0, 0, 0, 0))
    canvas.paste(img, (pad, pad), img)
    a = canvas.split()[3]
    g = a.filter(ImageFilter.MaxFilter(size * 2 + 1)).filter(ImageFilter.GaussianBlur(size))
    r = a.filter(ImageFilter.MaxFilter(size + 1 if size % 2 == 0 else size))
    base = Image.new('RGBA', canvas.size, glow + (0,))
    base.putalpha(g.point(lambda v: min(255, v * 2)))
    edge = Image.new('RGBA', canvas.size, ring + (255,))
    edge.putalpha(r)
    base.alpha_composite(edge)
    base.alpha_composite(canvas)
    return base


def mon(name, scale=5, size=5):
    return outlined(sprite('front', name, scale), size)


def trainer(cls, scale=6, size=6):
    return outlined(sprite('trainer', cls, scale), size)


def arena(W=1920, H=1080, floor=True, dim=0.0):
    """Night sky, three tiers of stands with camera flashes, spotlights, the round floor with a Poke Ball emblem.
    dim darkens the result (0..1) so text and panels read on top of it."""
    sx, sy = W / 1280, H / 720
    im = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=(int(20 + 40 * t), int(16 + 30 * t), int(60 + 70 * t)))
    for i, (y0, col) in enumerate(((250, (34, 30, 80)), (320, (44, 38, 96)), (390, (56, 48, 112)))):
        d.ellipse([-300 * sx, y0 * sy, W + 300 * sx, (y0 + 700) * sy], fill=col)
        for x in range(0, W, 6):
            for r in range(0, int(60 * sy), 5):
                if (x * 31 + r * 17 + i * 7) % 11 < 3:
                    c = [(240, 80, 80), (250, 220, 90), (120, 200, 255), (255, 255, 255)][(x + r) % 4]
                    yy = y0 * sy + 30 * sy + r + int(((x - W / 2) / W) ** 2 * 120 * sy)
                    d.rectangle([x, yy, x + 1, yy + 1], fill=c)
    glow = Image.new('L', (W, H), 0)
    g = ImageDraw.Draw(glow)
    for cx in (W * 0.125, W * 0.5, W * 0.875):
        g.polygon([(cx - 25 * sx, -10), (cx + 25 * sx, -10), (cx + 240 * sx, H), (cx - 240 * sx, H)], fill=70)
    glow = glow.filter(ImageFilter.GaussianBlur(28 * sx))
    im = Image.composite(Image.new('RGB', (W, H), (220, 235, 255)), im, glow)
    d = ImageDraw.Draw(im)
    if floor:
        d.ellipse([-80 * sx, 440 * sy, W + 80 * sx, 900 * sy], fill=(70, 90, 140), outline=WHITE, width=int(10 * sx))
        d.ellipse([90 * sx, 480 * sy, W - 90 * sx, 860 * sy], fill=(90, 120, 180))
        cx, cy, rx, ry = W // 2, 640 * sy, 300 * sx, 110 * sy
        d.pieslice([cx - rx, cy - ry, cx + rx, cy + ry], 180, 360, fill=(220, 40, 50))
        d.pieslice([cx - rx, cy - ry, cx + rx, cy + ry], 0, 180, fill=(240, 240, 240))
        d.rectangle([cx - rx, cy - 9 * sy, cx + rx, cy + 9 * sy], fill=(20, 20, 30))
        d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], outline=(20, 20, 30), width=int(12 * sx))
        d.ellipse([cx - 70 * sx, cy - 28 * sy, cx + 70 * sx, cy + 28 * sy], fill=(20, 20, 30))
        d.ellipse([cx - 46 * sx, cy - 17 * sy, cx + 46 * sx, cy + 17 * sy], fill=(240, 240, 240))
    flare = Image.new('L', (W, H), 0)
    f = ImageDraw.Draw(flare)
    for cx in (W * 0.125, W * 0.5, W * 0.875):
        f.ellipse([cx - 40 * sx, -40 * sx, cx + 40 * sx, 40 * sx], fill=255)
    flare = flare.filter(ImageFilter.GaussianBlur(18 * sx))
    im = Image.composite(Image.new('RGB', (W, H), WHITE), im, flare)
    if dim:
        im = Image.blend(im, Image.new('RGB', (W, H), (8, 10, 30)), dim)
    return im.convert('RGBA')


def text(im, xy, s, size, fill=WHITE, stroke=INK, width=None, anchor='mm', font=IMPACT, shadow=True):
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(font, size)
    width = max(2, size // 14) if width is None else width
    if shadow:
        o = max(3, size // 22)
        d.text((xy[0] + o, xy[1] + o + 2), s, font=f, fill=INK, anchor=anchor, stroke_width=width, stroke_fill=INK)
    d.text(xy, s, font=f, fill=fill, anchor=anchor, stroke_width=width, stroke_fill=stroke)


def cross(im, box, w=34):
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = box
    for ww, c in ((w, INK), (int(w * 0.65), (230, 30, 30))):
        d.line([(x0, y0), (x1, y1)], fill=c, width=ww)
        d.line([(x0, y1), (x1, y0)], fill=c, width=ww)


def panel(im, box, fill=(12, 16, 44, 215), outline=WHITE, width=6, radius=28):
    """Stadium menu box: dark translucent rounded panel with a white border."""
    layer = Image.new('RGBA', im.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).rounded_rectangle(box, radius, fill=fill, outline=outline, width=width)
    im.alpha_composite(layer)


def paste_c(im, img, cx, bottom):
    """Paste centered on cx with its bottom edge at `bottom`."""
    im.alpha_composite(img, (int(cx - img.width / 2), int(bottom - img.height)))
