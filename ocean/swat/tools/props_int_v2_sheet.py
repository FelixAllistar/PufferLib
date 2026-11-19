#!/usr/bin/env python3
"""Side-by-side review sheets for the interior props v2 hill-climb.

  props_int_v2_sheet.py OUT.jpg LABEL=IMAGE [LABEL=IMAGE ...] [--h 360] [--crop L,T,R,B for the next image]
  LABEL=@text draws a text panel instead (for references that are linked, not redistributed).

Every panel is scaled to the same height and labelled; the JPEG quality is
lowered until the sheet is under 400 KB. Used for both the per-iteration
"reference | render" pairs and the committed contact sheets
("reference | first render | final in-game")."""
import sys
from PIL import Image, ImageDraw

def main(argv):
    out = argv[0]; h = 360; panels = []; crop = None
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--h": h = int(argv[i + 1]); i += 2; continue
        if a == "--crop": crop = tuple(float(v) for v in argv[i + 1].split(",")); i += 2; continue
        label, path = a.split("=", 1)
        if path.startswith("@"):
            # Text placeholder for a reference that may not be redistributed.
            im = Image.new("RGB", (int(h * 1.2), h), (40, 40, 40)); dd = ImageDraw.Draw(im)
            words = path[1:].split(" "); line = ""; y = 10
            for w in words:
                if len(line) + len(w) > 50: dd.text((8, y), line, fill=(220, 220, 220)); y += 14; line = ""
                line += w + " "
            dd.text((8, y), line, fill=(220, 220, 220))
            panels.append((label, im)); i += 1; continue
        im = Image.open(path).convert("RGB")
        if crop:
            w0, h0 = im.size
            im = im.crop((int(crop[0] * w0), int(crop[1] * h0), int(crop[2] * w0), int(crop[3] * h0))); crop = None
        im = im.resize((max(1, round(im.width * h / im.height)), h), Image.LANCZOS)
        panels.append((label, im)); i += 1
    W = sum(p.width for _, p in panels) + 6 * (len(panels) - 1)
    sheet = Image.new("RGB", (W, h + 22), (24, 24, 24))
    d = ImageDraw.Draw(sheet); x = 0
    for label, im in panels:
        sheet.paste(im, (x, 22)); d.text((x + 4, 5), label, fill=(240, 240, 240)); x += im.width + 6
    q = 90
    while True:
        sheet.save(out, quality=q, optimize=True)
        import os
        if os.path.getsize(out) <= 400 * 1024 or q <= 40: break
        q -= 8
    print(out, sheet.size, q)

if __name__ == "__main__":
    main(sys.argv[1:])
