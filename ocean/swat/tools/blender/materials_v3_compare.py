#!/usr/bin/env python3
"""Side-by-side sheets for the materials_v3 hill climb (needs Pillow).

    python3 materials_v3_compare.py OUT.jpg [--height H] [--quality Q] LABEL=IMAGE ...

Each image is scaled to the same height and placed left to right with its
label underneath. Used for every iteration (reference | render) and for the
committed per-item contact sheets (reference | first render | final in-game).
"""
import sys
from PIL import Image, ImageDraw

args = sys.argv[1:]
out = args.pop(0)
height, quality = 360, 85
if '--height' in args:
    i = args.index('--height'); height = int(args[i + 1]); del args[i:i + 2]
if '--quality' in args:
    i = args.index('--quality'); quality = int(args[i + 1]); del args[i:i + 2]
tiles = []
for a in args:
    label, path = a.split('=', 1)
    im = Image.open(path).convert('RGB')
    im = im.resize((max(1, round(im.width * height / im.height)), height), Image.LANCZOS)
    tiles.append((label, im))
W = sum(t[1].width for t in tiles) + 6 * (len(tiles) - 1)
sheet = Image.new('RGB', (W, height + 22), (24, 24, 24))
d = ImageDraw.Draw(sheet); x = 0
for label, im in tiles:
    sheet.paste(im, (x, 0)); d.text((x + 6, height + 5), label, fill=(235, 235, 235)); x += im.width + 6
sheet.save(out, quality=quality, optimize=True)
print(out, sheet.size)
