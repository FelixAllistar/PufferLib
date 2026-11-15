#!/usr/bin/env python3
"""Look regression check: renders a fixed set of review stills and compares them with approved references.

    python3 -I ocean/cretaceous/tools/lookcheck.py run --ref REFDIR --out NEWDIR [--bin BUILD/cretaceous]
    python3 -I ocean/cretaceous/tools/lookcheck.py render NEWDIR
    python3 -I ocean/cretaceous/tools/lookcheck.py compare REFDIR NEWDIR [--sheet sheet.jpg] [--md report.md]
    python3 -I ocean/cretaceous/tools/lookcheck.py approve NEWDIR REFDIR

Run from the repository root (the game loads its shaders from there). `run` renders every shot in
SHOTS into NEWDIR, then compares: with an empty REFDIR it only renders and says how to approve.

Captures are deterministic: on one machine and driver, two renders of the same commit are bit for bit
the same (checked on WSL's d3d12 driver), so every changed pixel is a real change. References belong to
one machine and driver; keep them outside the repository (they are large and per-GPU).

Per shot it reports: changed (share of pixels whose largest channel moved more than 6/255),
SSIM of the luma (8x8 windows), the shift in mean brightness, warmth (red minus blue) and saturation,
and where the change is (top, middle, bottom third of the frame). The sheet shows reference, new and
an amplified difference for every changed shot, worst first. Needs numpy and Pillow.
"""
import argparse
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

# The built-in review shots (cretaceous.c) plus fixed cameras (CRET_SHOT_CAM: name:x:z:above:yaw:pitch:fov):
# the redwood grove, a forest sky gap, the gorge from its floor and from above.
SHOTS = ("dino_tp,dino_herd,dino_trex,ridge,river,coast,canopy,aerial,sunset,dusk,morning,night,rain,"
         "ground,mouth,creek_steep,redwood,skyh,ga0,gup")
CAMS = ("redwood:4610:1765:3.0:0.72:-0.03:65;skyh:2896:2432:4:0.6:0.12:70;"
        "ga0:2620:1344:2.5:0:0.08:70;gup:2620:1344:45:0.5:-0.7:70")
CHANGED = 6          # a pixel changed when a channel moved more than this (0-255)
SAME_SHARE = 0.001   # a shot counts as unchanged below this share of changed pixels


def render(out, binary, shots, size):
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("CRET_SHOT_CAM", CAMS)
    cmd = [binary, "capture", out, "--shot", shots, "--size", str(size[0]), str(size[1])]
    print("render:", " ".join(cmd))
    r = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0:
        sys.stdout.write(r.stdout[-3000:])
        sys.exit(f"render failed ({r.returncode})")


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float32)


def box(x, k):
    """Mean over k x k windows (valid region) by summed-area table."""
    c = np.pad(x, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


def ssim(a, b, k=8):
    a, b = a.astype(np.float64), b.astype(np.float64)   # float32 sums lose the variance on large frames
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    ma, mb = box(a, k), box(b, k)
    va, vb, cv = box(a * a, k) - ma * ma, box(b * b, k) - mb * mb, box(a * b, k) - ma * mb
    return float((((2 * ma * mb + c1) * (2 * cv + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())


def luma(x):
    return x @ np.array([0.2126, 0.7152, 0.0722], np.float32)


def sat(x):
    return (x.max(2) - x.min(2)).mean()


def compare_one(ra, nb):
    d = np.abs(ra - nb).max(2)
    ch = d > CHANGED
    h = ch.shape[0]
    thirds = [ch[i * h // 3:(i + 1) * h // 3].mean() for i in range(3)]
    return {
        "changed": float(ch.mean()),
        "ssim": ssim(luma(ra), luma(nb)),
        "bright": float(luma(nb).mean() - luma(ra).mean()),
        "warm": float((nb[..., 0] - nb[..., 2]).mean() - (ra[..., 0] - ra[..., 2]).mean()),
        "sat": float(sat(nb) - sat(ra)),
        "where": "/".join(f"{t * 100:.0f}" for t in thirds),
        "diff": d,
    }


def compare(ref, new, sheet, md):
    names = sorted(f for f in os.listdir(new) if f.endswith(".png") and os.path.exists(os.path.join(ref, f)))
    missing = sorted(f for f in os.listdir(ref) if f.endswith(".png") and not os.path.exists(os.path.join(new, f)))
    added = sorted(f for f in os.listdir(new) if f.endswith(".png") and not os.path.exists(os.path.join(ref, f)))
    rows = []
    for f in names:
        ra, nb = load(os.path.join(ref, f)), load(os.path.join(new, f))
        if ra.shape != nb.shape:
            rows.append((f, None, ra, nb))
            continue
        rows.append((f, compare_one(ra, nb), ra, nb))
    rows.sort(key=lambda r: -1 if r[1] is None else -(1 - r[1]["ssim"]))
    lines = ["| shot | changed % | SSIM | brightness | warmth | saturation | changed % top/mid/bottom |",
             "|---|---|---|---|---|---|---|"]
    nchanged = 0
    for f, m, _, _ in rows:
        if m is None:
            lines.append(f"| {f[:-4]} | size differs | | | | | |")
            nchanged += 1
            continue
        if m["changed"] >= SAME_SHARE:
            nchanged += 1
        lines.append(f"| {f[:-4]} | {m['changed'] * 100:.2f} | {m['ssim']:.4f} | {m['bright']:+.1f} | {m['warm']:+.1f} | "
                     f"{m['sat']:+.1f} | {m['where']} |")
    text = "\n".join(lines) + "\n"
    text += f"\n{nchanged} of {len(rows)} shots changed (more than {SAME_SHARE * 100:.1f}% of pixels by more than {CHANGED}/255)."
    if missing:
        text += "\nNot rendered (in the reference only): " + ", ".join(m[:-4] for m in missing)
    if added:
        text += "\nNo reference yet: " + ", ".join(m[:-4] for m in added)
    print(text)
    if md:
        with open(md, "w") as fp:
            fp.write(text + "\n")
    if sheet:
        bad = [r for r in rows if r[1] is None or r[1]["changed"] >= SAME_SHARE]
        if bad:
            tw = 480
            th = int(round(bad[0][2].shape[0] * tw / bad[0][2].shape[1]))
            img = Image.new("RGB", (tw * 3, (th + 22) * len(bad)), (16, 16, 16))
            dr = ImageDraw.Draw(img)
            for i, (f, m, ra, nb) in enumerate(bad):
                y = i * (th + 22)
                heat = np.zeros_like(nb)
                if m is not None:
                    heat[..., 0] = np.clip(m["diff"] * 6, 0, 255)
                    heat[..., 1] = np.clip(m["diff"] * 2, 0, 255)
                    heat = heat * 0.75 + luma(nb)[..., None] * 0.25
                for k, im in enumerate((ra, nb, heat)):
                    t = Image.fromarray(np.clip(im, 0, 255).astype(np.uint8)).resize((tw, th), Image.LANCZOS)
                    img.paste(t, (k * tw, y + 22))
                label = f[:-4] if m is None else f"{f[:-4]}  changed {m['changed'] * 100:.1f}%  SSIM {m['ssim']:.3f}  " \
                                                   f"bright {m['bright']:+.1f}  warm {m['warm']:+.1f}"
                dr.text((6, y + 5), label + "    (reference | new | difference x6)", fill=(235, 235, 235))
            img.save(sheet, quality=88)
            print("sheet:", sheet)
    return nchanged


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    for name in ("run", "render"):
        p = sp.add_parser(name)
        if name == "run":
            p.add_argument("--ref", required=True)
            p.add_argument("--out", required=True)
            p.add_argument("--sheet")
            p.add_argument("--md")
        else:
            p.add_argument("out")
        p.add_argument("--bin", default="build/cretaceous/cretaceous")
        p.add_argument("--shots", default=SHOTS)
        p.add_argument("--size", nargs=2, type=int, default=(640, 360))
    p = sp.add_parser("compare")
    p.add_argument("ref")
    p.add_argument("new")
    p.add_argument("--sheet")
    p.add_argument("--md")
    p = sp.add_parser("approve")
    p.add_argument("new")
    p.add_argument("ref")
    a = ap.parse_args()

    if a.cmd == "render":
        render(a.out, a.bin, a.shots, a.size)
    elif a.cmd == "run":
        render(a.out, a.bin, a.shots, a.size)
        if not os.path.isdir(a.ref) or not any(f.endswith(".png") for f in os.listdir(a.ref)):
            print(f"no references in {a.ref}: approve these with\n  lookcheck.py approve {a.out} {a.ref}")
            return
        sheet = a.sheet or os.path.join(a.out, "lookcheck_sheet.jpg")
        compare(a.ref, a.out, sheet, a.md)
    elif a.cmd == "compare":
        compare(a.ref, a.new, a.sheet, a.md)
    elif a.cmd == "approve":
        os.makedirs(a.ref, exist_ok=True)
        n = 0
        for f in os.listdir(a.new):
            if f.endswith(".png"):
                shutil.copy2(os.path.join(a.new, f), os.path.join(a.ref, f))
                n += 1
        print(f"approved {n} stills into {a.ref}")


if __name__ == "__main__":
    main()
