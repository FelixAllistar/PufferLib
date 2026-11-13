#!/usr/bin/env python3
# Counts level-of-detail pops in a camera path rendered frame by frame:
#   CRET_PATH_STILLS=1 ./build/cretaceous/cretaceous capture DIR --path lod_forest,lod_meadow --fps 60 --size 640 360
#   python3 -I ocean/cretaceous/tools/lodpop.py DIR/lod_forest [DIR2/lod_forest]
# The lod_* paths move the camera at a steady speed along a straight line, so
# the difference between two frames changes smoothly from frame to frame. A
# plant or prop that changes its mesh, an impostor that swaps in, or ground
# cover that appears changes its part of the picture in one frame: a spike in
# that tile's difference over both its neighbours in time. Per 16x16 tile and
# frame, the mean absolute luma difference to the previous frame (0-255) is
# D[t]; a pop is a tile where D[t] exceeds both D[t-1] and D[t+1] by POP_RATIO
# times and by at least POP_MIN. Tiles that are more than a fifth sky (blue,
# or bright and grey: cloud) in either frame are left out: the clouds march a
# pixel per 4x4 block each frame and change from frame to frame by design,
# which is not a level-of-detail change. The first SKIP frames are left out
# too: exposure and the sim are still settling after the capture's warm-up.
# Printed: tiles that popped, frames with any
# pop, frames with 8 or more popped tiles (a visible event rather than one
# leaf), and the summed excess (how much changed in pops). With a second DIR
# it prints both runs side by side; --list also prints, per run, every frame
# with a pop, its popped tiles and their box in pixels. Needs numpy and Pillow.
import glob
import sys

import numpy as np
from PIL import Image

TILE = 16
POP_RATIO = 2.0
POP_MIN = 4.0
SKIP = 20


def load(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    sky = ((b > g + 8) & (b > r + 25)) | ((np.minimum(np.minimum(r, g), b) > 170) & (np.maximum(np.maximum(r, g), b) - np.minimum(np.minimum(r, g), b) < 40))
    return r * 0.2126 + g * 0.7152 + b * 0.0722, sky


def tiles(a, h, w):
    return a[:h, :w].reshape(h // TILE, TILE, w // TILE, TILE).mean(axis=(1, 3))


def tile_diffs(d):
    files = sorted(glob.glob(d + "/f*.png"))
    if len(files) < 4:
        raise SystemExit(f"{d}: needs every frame (CRET_PATH_STILLS=1)")
    (prev, psky), rows, masks = load(files[0]), [], []
    for f in files[1:]:
        cur, sky = load(f)
        h, w = cur.shape[0] // TILE * TILE, cur.shape[1] // TILE * TILE
        rows.append(tiles(np.abs(cur - prev), h, w))
        masks.append(np.maximum(tiles(sky.astype(np.float32), h, w), tiles(psky.astype(np.float32), h, w)) > 0.2)
        prev, psky = cur, sky
    return np.stack(rows), np.stack(masks)


def hits(DM):
    D, M = DM
    mid, nb = D[1:-1], np.maximum(D[:-2], D[2:])
    sky = M[:-2] | M[1:-1] | M[2:]
    sky[:SKIP] = True
    return (mid > nb * POP_RATIO) & (mid > nb + POP_MIN) & ~sky, mid, nb


def pops(D):
    hit, mid, nb = hits(D)
    per_frame = hit.reshape(hit.shape[0], -1).sum(axis=1)
    excess = np.where(hit, mid - nb, 0).sum()
    return {"tiles": int(hit.sum()), "frames": int((per_frame > 0).sum()), "events": int((per_frame >= 8).sum()),
            "excess": float(excess), "worst": int(per_frame.max()), "worst_at": int(per_frame.argmax()) + 2, "n": D[0].shape[0] + 1}


def listing(d, D):
    hit = hits(D)[0]
    for t in range(hit.shape[0]):
        ys, xs = np.nonzero(hit[t])
        if len(ys):
            print(f"{d} frame {t + 2}: {len(ys)} tiles, x {xs.min() * TILE}-{(xs.max() + 1) * TILE} y {ys.min() * TILE}-{(ys.max() + 1) * TILE}")


def main():
    dirs = [a for a in sys.argv[1:] if not a.startswith("--")]
    diffs = [tile_diffs(d) for d in dirs]
    if "--list" in sys.argv:
        for d, D in zip(dirs, diffs):
            listing(d, D)
    runs = [pops(D) for D in diffs]
    keys = ["n", "tiles", "frames", "events", "excess", "worst", "worst_at"]
    print("%-10s" % "" + "".join("%14s" % ("run %d" % (i + 1)) for i in range(len(runs))))
    for k in keys:
        print("%-10s" % k + "".join("%14.0f" % r[k] for r in runs))


if __name__ == "__main__":
    main()
