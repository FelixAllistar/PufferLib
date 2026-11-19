#!/usr/bin/env python3
"""Side-by-side reference | render(s) for the hill-climb loop, and contact
sheets.  usage: pe2_compare.py OUT.jpg H "label:img" ["label:img" ...]
Needs Pillow."""
import sys
from PIL import Image, ImageDraw
out, H = sys.argv[1], int(sys.argv[2])
tiles = []
for arg in sys.argv[3:]:
    label, path = arg.split(":", 1)
    im = Image.open(path).convert("RGB")
    im = im.resize((max(1, round(im.width * H / im.height)), H), Image.LANCZOS)
    d = ImageDraw.Draw(im); d.rectangle((0, 0, 8 + 7 * len(label), 16), fill=(0, 0, 0)); d.text((4, 2), label, fill=(255, 255, 255))
    tiles.append(im)
W = sum(t.width for t in tiles) + 4 * (len(tiles) - 1)
sheet = Image.new("RGB", (W, H), (40, 40, 40)); x = 0
for t in tiles: sheet.paste(t, (x, 0)); x += t.width + 4
q = 88
while True:
    sheet.save(out, quality=q)
    import os
    if os.path.getsize(out) <= 400_000 or q < 40: break
    q -= 8
print(out, sheet.size)
