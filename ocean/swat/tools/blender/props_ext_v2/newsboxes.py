# BC2_NEWSBOX01: a row of three street newspaper vending boxes (blue coin
# box, tall three-tier free rack, red coin box). Generic: no titles or
# logos; window papers are blank tone blocks. Reference: Wikimedia Commons
# "Group of Newspaper Vending Machines.jpg" by Twzink (CC0), see LOG.md.
# Front = +X, origin at the centre of the row on the ground.
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(doors=1, blue_tilt=0.10, blue=[0.20, 0.26, 0.45], red=[0.60, 0.16, 0.14], red2=[0.52, 0.17, 0.15], gap=0.18, dirt=0.6, grain=1.0, seed=5)

def paper(name, x, y, z, w, h, m):
    p = box(name, (x, y, z), (0.004, w, h), m, (0.74, 0.73, 0.69)); box_uv(p, 0.3)
    for k in range(3):
        b = box(name + "b%d" % k, (x + 0.003, y + w * (0.18 * (k - 1)), z + h * 0.18), (0.003, w * 0.22, h * 0.28), m, (0.35, 0.33, 0.30)); box_uv(b, 0.3)
    t = box(name + "t", (x + 0.003, y, z + h * 0.40), (0.003, w * 0.8, h * 0.08), m, (0.12, 0.12, 0.12)); box_uv(t, 0.3)

def coin_box(y, col, pm, dm, gm, rnd, name, h=0.98, tilt=0.12):
    w, d = 0.44, 0.40
    body = extrude_profile(name, [(-d / 2, 0.06), (d / 2, 0.06), (d / 2 + 0.0, h * 0.55), (d / 2 - tilt, h), (-d / 2, h)], w, pm, col, bevel=0.012, segs=1, y0=y - w / 2)
    box_uv(body, 0.6); paint(body, col, noise=0.08, seed=rnd.randint(0, 99), grad=(0.0, 0.5, P["dirt"]))
    feet = box(name + "base", (0, y, 0.03), (d + 0.02, w + 0.02, 0.06), dm, (0.15, 0.15, 0.15)); box_uv(feet, 0.4)
    # sloped window with paper behind
    z0, z1 = h * 0.57, h * 0.95
    ang = math.atan2(tilt, h - h * 0.55)
    n = Vector((math.cos(ang), 0, math.sin(ang)))
    c = Vector((d / 2 - tilt * 0.5 + 0.004, y, (z0 + z1) / 2)) + n * 0.003
    win = quad(name + "win", [c + Vector((0, -w * 0.38, 0)) - Vector((-math.sin(ang), 0, math.cos(ang))) * (z1 - z0) / 2 * 1.02,
                              c + Vector((0, w * 0.38, 0)) - Vector((-math.sin(ang), 0, math.cos(ang))) * (z1 - z0) / 2 * 1.02,
                              c + Vector((0, w * 0.38, 0)) + Vector((-math.sin(ang), 0, math.cos(ang))) * (z1 - z0) / 2 * 1.02,
                              c + Vector((0, -w * 0.38, 0)) + Vector((-math.sin(ang), 0, math.cos(ang))) * (z1 - z0) / 2 * 1.02][::-1], gm, (0.70, 0.70, 0.66))
    box_uv(win, 0.3)
    # coin mechanism on top, handle
    mech = box(name + "mech", (0.0, y + 0.08, h + 0.12), (0.16, 0.16, 0.24), pm, col, bevel=0.01); box_uv(mech, 0.4)
    slot = box(name + "slot", (0.082, y + 0.08, h + 0.18), (0.004, 0.09, 0.07), dm, (0.85, 0.85, 0.82)); box_uv(slot, 0.3)
    hd = box(name + "handle", (d / 2 + 0.03, y + 0.08, h * 0.52), (0.06, 0.12, 0.03), dm, (0.2, 0.2, 0.2)); box_uv(hd, 0.3)
    lab = box(name + "lab", (d / 2 + 0.012, y, h * 0.3), (0.004, w * 0.6, 0.14), dm, (0.85, 0.85, 0.80)); box_uv(lab, 0.3)
    if P.get("doors", 0):
        pan = box(name + "door", (d / 2 + 0.006, y, h * 0.27), (0.008, w * 0.82, h * 0.40), pm, [c * 0.78 for c in col], bevel=0.006); box_uv(pan, 0.4)

def build(p):
    global P; P = p
    rnd = random.Random(p["seed"])
    grain = image("rusty_painted_metal", 512, gray=True, mean=0.85, gain=p.get("grain", 0.55))
    pm = material("paint", grain, rough=0.55)
    dm = material("dark", grain, rough=0.6)
    gm = material("paper", None, rough=0.3)
    g = p["gap"]
    coin_box(-0.44 - 0.42 - g, p["blue"], pm, dm, gm, rnd, "blue", h=0.92, tilt=p.get("blue_tilt", 0.0))
    # tall three-tier rack
    w, d, h = 0.52, 0.42, 1.38
    rack = box("rack", (0, 0, h / 2 + 0.04), (d, w, h), pm, p["red"], bevel=0.02); box_uv(rack, 0.6)
    paint(rack, p["red"], noise=0.08, seed=3, grad=(0.0, 0.5, p["dirt"]))
    for k in range(3):
        z = 0.30 + 0.45 * k
        door = box("door%d" % k, (d / 2 + 0.01, 0, z), (0.02, w - 0.06, 0.38), pm, [c * 0.9 for c in p["red"]], bevel=0.008); box_uv(door, 0.4)
        win = quad("rwin%d" % k, [(d / 2 + 0.022, 0.20, z - 0.14), (d / 2 + 0.022, -0.20, z - 0.14), (d / 2 + 0.022, -0.20, z + 0.14), (d / 2 + 0.022, 0.20, z + 0.14)], gm, (0.55, 0.50, 0.48))
        box_uv(win, 0.3)
        if k > 0: paper("rp%d" % k, d / 2 + 0.026, 0.06, z - 0.02, 0.22, 0.24, gm)
    base = box("rbase", (0, 0, 0.03), (d + 0.04, w + 0.04, 0.06), dm, (0.45, 0.10, 0.10)); box_uv(base, 0.4)
    if p.get("pedestal", 1):
        py = 0.26 + g + 0.24
        pb = box("pedbase", (0, py, 0.02), (0.30, 0.46, 0.04), dm, (0.12, 0.12, 0.12)); box_uv(pb, 0.4)
        pp = box("pedpost", (0, py, 0.45), (0.08, 0.10, 0.84), dm, (0.13, 0.13, 0.13)); box_uv(pp, 0.4)
        ph = box("pedhead", (0, py, 1.05), (0.20, 0.50, 0.40), dm, (0.13, 0.13, 0.13), bevel=0.02); box_uv(ph, 0.4)
        paper("pedp", 0.103, py - 0.05, 1.05, 0.30, 0.28, gm)
        g = g + 0.5
    coin_box(0.44 + 0.42 + g * 0.7, p["red2"], pm, dm, gm, rnd, "red", h=0.98, tilt=0.0)
    for y in (-0.44 - 0.42 - p["gap"], 0.44 + 0.42 + g * 0.7):
        paper("pp%d" % int(y * 10), 0.205, y, 0.72, 0.30, 0.30, gm)

def view(p):
    return dict(cam_loc=(2.55, 0.1, 1.6), target=(0, 0.15, 0.62), focal=22, sun=(50, 200), res=(960, 640))

finish("BC2_NEWSBOX01", build, D, view)
