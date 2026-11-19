# BC2_PAYPHONE01: pedestal payphone with a rounded painted hood, dark post,
# black phone unit with steel faceplate, armoured cord. Generic: no text or
# logos (the label areas are blank colour). Reference: Wikimedia Commons
# "Payphone Main St Moab UT 2025-05-16 11-54-11 1.jpg" by G. Edward Johnson
# (CC BY 4.0), see LOG.md. Front = +X, origin at the base centre.
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(hood=[0.76, 0.57, 0.16], unit_w=0.30, post_w=0.22, hood_w=0.64, hood_h=0.98, hood_d=0.44, hood_z=1.0, post_h=1.0, wall=0.035,
         bevel=0.09, steel=[0.72, 0.72, 0.70], unit=[0.10, 0.10, 0.11], stickers=18, seed=11)

def build(p):
    rnd = random.Random(p["seed"])
    grain = image("dirty_concrete", 512, gray=True, mean=0.86, gain=0.35)
    paint_m = material("paint", grain, rough=0.6)
    steel = material("steel", grain, rough=0.35, metal=0.6)
    dark = material("dark", grain, rough=0.6)
    hw, hh, hd, hz = p["hood_w"], p["hood_h"], p["hood_d"], p["hood_z"]
    base = box("base", (0, 0, 0.025), (0.42, 0.40, 0.05), dark, (0.55, 0.54, 0.52), bevel=0.01); box_uv(base, 0.5)
    post = box("post", (-0.06, 0, p["post_h"] / 2 + 0.05), (0.14, p.get("post_w", 0.17), p["post_h"]), dark, (0.16, 0.16, 0.17), bevel=0.008); box_uv(post, 0.5)
    pan = box("postpanel", (0.012, 0, 0.60), (0.006, p.get("post_w", 0.17) - 0.05, 0.86), paint_m, (0.75, 0.72, 0.62)); box_uv(pan, 0.5)
    hood = box("hood", (0, 0, hz + hh / 2), (hd, hw, hh), paint_m, p["hood"], bevel=p["bevel"], segs=3)
    w = p["wall"]
    cut = box("_cut", (w + 0.1, 0, hz + hh / 2), (hd + 0.2, hw - 2 * w, hh - 2 * w), None)
    boolean(hood, cut); box_uv(hood, 0.6)
    paint(hood, p["hood"], noise=0.08, seed=2, grad=(hz, hz + 0.3, 0.85))
    # phone unit
    ux = -hd / 2 + w + 0.09
    unit = box("unit", (ux, -0.03, hz + 0.47), (0.18, p.get("unit_w", 0.24), 0.70), dark, p["unit"], bevel=0.01); box_uv(unit, 0.3)
    fp = box("face", (ux + 0.095, -0.03, hz + 0.62), (0.012, p.get("unit_w", 0.24) - 0.04, 0.40), steel, p["steel"]); box_uv(fp, 0.3)
    cb = box("coinbox", (ux + 0.095, -0.03, hz + 0.27), (0.012, p.get("unit_w", 0.24) - 0.08, 0.18), steel, p["steel"]); box_uv(cb, 0.3)
    lab = box("label", (ux + 0.103, -0.03, hz + 0.74), (0.004, 0.17, 0.05), dark, (0.22, 0.42, 0.68)); box_uv(lab, 0.3)
    for r in range(4):
        for c in range(3):
            k = box("key%d%d" % (r, c), (ux + 0.105, -0.07 + 0.03 * c, hz + 0.64 - 0.032 * r), (0.01, 0.02, 0.02), steel, (0.85, 0.85, 0.85)); box_uv(k, 0.3)
    # handset hanging on the left of the faceplate
    hx = ux + 0.13; hy = 0.06
    hs = tube("handset", [(hx, hy, hz + 0.50), (hx + 0.01, hy, hz + 0.58), (hx + 0.01, hy, hz + 0.68), (hx, hy, hz + 0.74)], 0.022, 8, dark, (0.06, 0.06, 0.06))
    box_uv(hs, 0.3)
    for z in (hz + 0.50, hz + 0.74):
        cup = box("cup", (hx + 0.012, hy, z), (0.05, 0.05, 0.06), dark, (0.06, 0.06, 0.06), bevel=0.015); box_uv(cup, 0.3)
    cord = tube("cord", [(hx, hy, hz + 0.47), (hx + 0.06, hy - 0.02, hz + 0.25), (hx + 0.05, hy - 0.12, hz + 0.08), (ux + 0.06, -0.15, hz + 0.25)], 0.008, 5, steel, (0.6, 0.6, 0.6))
    box_uv(cord, 0.3)
    # shelf / ledge at the bottom of the hood opening, and stickers
    led = box("ledge", (hd / 2 - 0.12, 0, hz + w + 0.02), (0.24, hw - 2 * w, 0.04), paint_m, p["hood"], bevel=0.01); box_uv(led, 0.5)
    hdr = box("header", (hd / 2 - 0.06, 0, hz + hh - w - 0.08), (0.10, hw - 2 * w, 0.14), paint_m, (0.86, 0.84, 0.76)); box_uv(hdr, 0.5)
    plac = box("placard", (hd / 2 - 0.005, -0.04, hz + hh - w - 0.08), (0.004, 0.30, 0.09), dark, (0.95, 0.95, 0.93)); box_uv(plac, 0.3)
    for i in range(p["stickers"]):
        side = rnd.choice((-1, 1)); z = hz + rnd.uniform(0.15, hh - 0.2); x = rnd.uniform(-hd / 2 + 0.06, hd / 2 - 0.06)
        inner = i % 3 == 0
        if inner:
            sz = rnd.uniform(0.10, 0.18); col = rnd.choice([(0.92, 0.92, 0.88), (0.3, 0.5, 0.75)])
            st = box("sti%d" % i, (x + 0.03, side * (hw / 2 - w - 0.002), z), (sz, 0.003, sz * 1.3), dark, col); box_uv(st, 0.3); continue
        sz = rnd.uniform(0.05, 0.11); col = rnd.choice([(0.92, 0.92, 0.88), (0.2, 0.35, 0.6), (0.75, 0.2, 0.15), (0.12, 0.12, 0.12), (0.4, 0.6, 0.3)])
        st = box("st%d" % i, (x, side * (hw / 2 + 0.002), z), (sz, 0.003, sz * rnd.uniform(0.6, 1.4)), dark, col); box_uv(st, 0.3)

def view(p):
    return dict(cam_loc=(1.30, -0.75, 1.75), target=(0, 0, 1.2), focal=22, sun=(55, 210), res=(720, 960))

finish("BC2_PAYPHONE01", build, D, view)
