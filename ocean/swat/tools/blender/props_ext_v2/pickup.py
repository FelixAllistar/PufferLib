# BC2_PICKUP01: generic late-1960s half-ton pickup, short bed, faded light
# blue, dirty. No badges or lettering. Reference: Wikimedia Commons
# "Chevrolet c10 pickup argentina.jpg" (public domain), see LOG.md.
#   blender -b --factory-startup -P pickup.py -- [--render out.jpg] [--export] [--set k=v]
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(
    paint=[0.50, 0.65, 0.73], grain=0.22, dirt=0.70, belt=1.10, roof=1.86, roof_x=(-0.46, 0.38),
    bottom=0.50, cab_back=-0.50, shield_base=0.80, body_w=1.98, cab_w=1.74, wheel_r=0.40, wb=(-1.42, 1.50),
    arch_r=0.50, bumper=[0.86, 0.84, 0.76], front=2.40, rear=-2.42, hood_drop=0.06,
)

def build(p):
    grain = image("dirty_concrete", 512, gray=True, mean=0.88, gain=p.get("grain", 0.45))
    rust = image("rust_coarse_01", 512)
    paint_m = material("paint", grain, rough=0.62)
    chrome = material("chrome", grain, rough=0.35, metal=0.5)
    rubber = material("rubber", grain, rough=0.92)
    glass = material("glass", None, rough=0.08)
    dark = material("trim", grain, rough=0.7)
    pc = p["paint"]; dirt = p["dirt"]
    F, R, B, H = p["front"], p["rear"], p["bottom"], p["belt"]
    # lower body (hood, fenders, doors, bed sides) as one side profile
    body = extrude_profile("body", [(R, B + 0.05), (R + 0.04, B), (F - 0.08, B), (F, B + 0.12), (F + 0.02, H - p["hood_drop"]),
                                    (F - 0.15, H - p["hood_drop"] + 0.02), (p["shield_base"], H + 0.04), (p["cab_back"], H + 0.04),
                                    (p["cab_back"], H), (R, H)], p["body_w"], paint_m, pc, bevel=p.get("bevel", 0.045), segs=3)
    # bed well
    well = box("_well", ((R + p["cab_back"]) / 2 - 0.02, 0, H + 0.2), (p["cab_back"] - R - 0.16, p["body_w"] - 0.12, 2 * (H - 0.62)), None)
    boolean(body, well)
    for x in p["wb"]:
        cut = cylinder("_arch", p["arch_r"], p["arch_r"], p["body_w"] + 0.2, 28, loc=(x, 0, p["wheel_r"] + 0.02), rot=(math.pi / 2, 0, 0), smooth=False)
        boolean(body, cut)
    box_uv(body, 1.5)
    paint(body, pc, noise=0.05, seed=1, grad=(0.3, 1.0, dirt))
    # cab
    rx0, rx1 = p["roof_x"]
    cab = extrude_profile("cab", [(p["cab_back"], H), (p["shield_base"] + 0.05, H), (rx1, p["roof"] - 0.03), (rx1 - 0.05, p["roof"]),
                                  (rx0 + 0.04, p["roof"]), (rx0, p["roof"] - 0.04)], p["cab_w"], paint_m, pc, bevel=0.05, segs=3)
    box_uv(cab, 1.5); paint(cab, pc, noise=0.04, seed=2, grad=(0.3, 1.9, 0.85))
    # glass: side windows and windshield, rear window
    def quad(name, pts):
        bm = bmesh.new(); vs = [bm.verts.new(v) for v in pts]; bm.faces.new(vs); return new_obj(name, bm, glass, (0.10, 0.11, 0.12))
    yw = p["cab_w"] / 2 + 0.006
    for s in (-1, 1):
        a, b = H + 0.045, p["roof"] - 0.06
        pts = [(rx0 + 0.03, s * yw, a), (p["shield_base"] - 0.02, s * yw, a), (rx1 - 0.0, s * yw, b), (rx0 + 0.05, s * yw, b)]
        quad("win%d" % s, pts if s > 0 else list(reversed(pts)))
        vx = p["shield_base"] - 0.30
        pil = box("vent%d" % s, (vx, s * (yw + 0.004), (a + b) / 2 - 0.02), (0.025, 0.008, b - a), paint_m, pc); box_uv(pil, 1)
    t = 0.02
    sx0, sz0, sx1, sz1 = p["shield_base"] + 0.03, H + 0.09, rx1 + 0.0, p["roof"] - 0.06
    n = Vector((sz1 - sz0, 0, sx0 - sx1)).normalized() * 0.012
    quad("shield", [Vector((sx0, -yw + 0.08, sz0)) + n, Vector((sx0, yw - 0.08, sz0)) + n, Vector((sx1, yw - 0.1, sz1)) + n, Vector((sx1, -yw + 0.1, sz1)) + n])
    quad("rear", [(rx0 - 0.012, yw - 0.25, H + 0.12), (rx0 - 0.012, -yw + 0.25, H + 0.12), (rx0 - 0.006, -yw + 0.3, p["roof"] - 0.12), (rx0 - 0.006, yw - 0.3, p["roof"] - 0.12)])
    # grille panel, headlights, bumpers
    gw = p["body_w"] - 0.2
    sur = box("gsurround", (F + 0.012, 0, B + 0.34), (0.03, gw + 0.04, 0.36), paint_m, pc, bevel=0.012); box_uv(sur, 1)
    g = box("grille", (F + 0.022, 0, B + 0.34), (0.03, gw - 0.40, 0.26), dark, (0.18, 0.19, 0.19), bevel=0.01)
    box_uv(g, 0.5)
    for k in range(3):
        bar = box("gbar%d" % k, (F + 0.04, 0, B + 0.26 + 0.08 * k), (0.02, gw - 0.5, 0.022), chrome, (0.85, 0.85, 0.84)); box_uv(bar, 0.5)
    for s in (-1, 1):
        hl = cylinder("hl%d" % s, 0.085, 0.085, 0.05, 16, chrome, (0.92, 0.92, 0.90), loc=(F + 0.035, s * (gw / 2 - 0.12), B + 0.36), rot=(0, math.pi / 2, 0))
        box_uv(hl, 0.5)
    fb = box("fbumper", (F + 0.07, 0, B - 0.02), (0.13, p["body_w"] + 0.04, 0.24), paint_m, p["bumper"], bevel=0.04, segs=2); box_uv(fb, 0.6)
    paint(fb, p["bumper"], noise=0.06, seed=3, grad=(0.25, 0.6, 0.8))
    rb = box("rbumper", (R - 0.06, 0, B - 0.02), (0.1, p["body_w"] - 0.06, 0.16), chrome, (0.80, 0.80, 0.79), bevel=0.02); box_uv(rb, 0.6)
    # tailgate top cap and door seam / handle
    for s in (-1, 1):
        h = box("handle%d" % s, (p["cab_back"] + 0.12, s * (p["body_w"] / 2 + 0.01), H - 0.12), (0.12, 0.02, 0.03), chrome, (0.85, 0.85, 0.85)); box_uv(h, 0.5)
        seam = box("seam%d" % s, (p["cab_back"] - 0.01, s * (p["body_w"] / 2 + 0.002), (B + H) / 2 + 0.03), (0.015, 0.006, H - B - 0.1), dark, (0.25, 0.25, 0.25)); box_uv(seam, 0.5)
        # mirror
        m = box("mirror%d" % s, (p["shield_base"] - 0.02, s * (p["cab_w"] / 2 + 0.16), H + 0.28), (0.03, 0.10, 0.14), chrome, (0.85, 0.85, 0.85)); box_uv(m, 0.5)
        arm = tube("marm%d" % s, [(p["shield_base"] - 0.02, s * (p["cab_w"] / 2), H + 0.08), (p["shield_base"] - 0.02, s * (p["cab_w"] / 2 + 0.14), H + 0.22)], 0.01, 5, chrome, (0.85, 0.85, 0.85)); box_uv(arm, 0.5)
    # wheels
    r = p["wheel_r"]
    for x in p["wb"]:
        for s in (-1, 1):
            y = s * (p["body_w"] / 2 - 0.16)
            tyre = lathe("tyre", [(0.20, -0.11), (r - 0.04, -0.11), (r, -0.07), (r, 0.07), (r - 0.04, 0.11), (0.20, 0.11)], 24, rubber, (0.13, 0.13, 0.13))
            tyre.rotation_euler = (math.pi / 2, 0, 0); tyre.location = (x, y, r); cyl_uv(tyre, 4, 0.3)
            paint(tyre, (0.13, 0.13, 0.13), noise=0.1, seed=4, grad=(0.0, 0.3, 1.4))
            rim = lathe("rim", [(0.0, 0.01), (0.14, 0.01), (0.24, -0.01), (0.25, -0.06)], 20, paint_m, pc)
            rim.rotation_euler = (s * -math.pi / 2, 0, 0); rim.location = (x, s * (p["body_w"] / 2 - 0.05), r); cyl_uv(rim, 1, 0.3)
            paint(rim, pc, noise=0.05)
            cap = lathe("cap", [(0.0, 0.07), (0.10, 0.06), (0.155, 0.02)], 16, chrome, (0.92, 0.91, 0.88))
            cap.rotation_euler = (s * -math.pi / 2, 0, 0); cap.location = (x, s * (p["body_w"] / 2 - 0.05), r); cyl_uv(cap, 1, 0.3)
    # chassis shadow block under the body
    ch = box("chassis", (0, 0, B - 0.02), (F - R - 0.6, p["body_w"] - 0.5, 0.18), dark, (0.2, 0.2, 0.2)); box_uv(ch, 1)

def view(p):
    return dict(cam_loc=(4.9, -6.2, 1.30), target=(0.1, 0, 0.88), focal=42, sun=(55, 200), res=(960, 544))

finish("BC2_PICKUP01", build, D, view)
