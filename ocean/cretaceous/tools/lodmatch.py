#!/usr/bin/env python3
# Measures how well a tree's LODs agree in tone, from the match pages that
# `capture DIR --gallery` writes with CRET_GALLERY_MATCH (devmodes.c):
#   python3 -I ocean/cretaceous/tools/lodmatch.py DIR [DIR2]
# For every tree and distance it masks the crown (pixels that differ from the
# empty plain shot from the same spot, above 30% of the tree's height, where
# the crown is and its ground shadow is not) and prints, per LOD, the crown's
# pixel count, mean sRGB colour, mean luma (Rec. 709 on sRGB values, 0-255)
# and that luma relative to the full mesh. With a second DIR it prints the
# change of each value between the two runs (before and after a change).
# Needs Pillow.
import math
import sys
from PIL import Image, ImageChops, ImageStat


def load_index(d):
    rows = {}
    with open(d + "/match_index.txt") as f:
        for line in f:
            name, dist, h = line.split()
            rows[name] = (float(dist), float(h))
    return rows


def crown_stats(d, name, dist, h):
    img = Image.open(f"{d}/{name}").convert("RGB")
    bare = Image.open(f"{d}/match_bare_{dist:.0f}m.png").convert("RGB")
    W, H = img.size
    # The capture's camera: 2 m above the ground, 60 degrees vertical, aimed 5.5 m up.
    pitch, t = math.atan2(5.5 - 2, dist), math.tan(math.radians(30))
    row = lambda y: H / 2 - H / 2 * math.tan(math.atan2(y - 2, dist) - pitch) / t
    cut = int(max(0, min(H, row(h * 0.3))))
    diff = ImageChops.difference(img, bare).convert("L").point(lambda v: 255 if v > 10 else 0)
    mask = Image.new("L", (W, H), 0)
    mask.paste(diff.crop((0, 0, W, cut)), (0, 0))
    n = ImageStat.Stat(mask).sum[0] / 255
    if n < 20:
        return None
    r, g, b = ImageStat.Stat(img, mask).mean
    return n, (r, g, b), 0.2126 * r + 0.7152 * g + 0.0722 * b


def measure(d):
    idx = load_index(d)
    out = {}
    for name, (dist, h) in sorted(idx.items()):
        if name.startswith("match_bare"):
            continue
        tree, dm, lod = name[len("match_"):-len(".png")].rsplit("_", 2)
        s = crown_stats(d, name, dist, h)
        if s:
            out[(tree, int(dist), lod)] = s
    return out


def report(m, base=None):
    trees = sorted({k[0] for k in m})
    for tree in trees:
        for dist in sorted({k[1] for k in m if k[0] == tree}):
            full = m.get((tree, dist, "full"))
            for lod in ("full", "reduced", "impostor"):
                s = m.get((tree, dist, lod))
                if not s:
                    continue
                n, (r, g, b), y = s
                rel = y / full[2] if full else float("nan")
                line = f"{tree:12s} {dist:4d} m  {lod:9s} px {n:7.0f}  rgb {r:5.1f} {g:5.1f} {b:5.1f}  luma {y:5.1f}  vs full {rel:5.3f}"
                if base and (tree, dist, lod) in base:
                    y0 = base[(tree, dist, lod)][2]
                    f0 = base.get((tree, dist, "full"))
                    rel0 = y0 / f0[2] if f0 else float("nan")
                    line += f"  | before luma {y0:5.1f} vs full {rel0:5.3f}"
                print(line)
        print()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: lodmatch.py DIR [BEFORE_DIR]")
    now = measure(sys.argv[1])
    report(now, measure(sys.argv[2]) if len(sys.argv) > 2 else None)
