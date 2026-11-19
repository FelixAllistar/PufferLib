# BC2_VAN01: generic mid-1960s forward-control panel van, warm grey, dirty,
# no badges or lettering. Reference: Wikimedia Commons "1965 Ford Econoline
# HD Van (54486012308).jpg" by Mustang Joe (CC0), see LOG.md.
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(paint=[0.42, 0.40, 0.37], grain=0.16, rough=0.4, L=(-2.18, 2.20), W=1.76, roof=1.92, belt=1.02, bottom=0.40,
         shield_top=1.80, shield_x=1.92, wheel_r=0.36, wb=(-1.20, 1.12), arch_r=0.46, bevel=0.13, nose=0.06,
         bumper=[0.78, 0.78, 0.76], dirt=0.65)

def build(p):
    grain = image("dirty_concrete", 512, gray=True, mean=0.88, gain=p["grain"])
    paint_m = material("paint", grain, rough=p.get("rough", 0.55))
    chrome = material("chrome", grain, rough=0.35, metal=0.5)
    rubber = material("rubber", grain, rough=0.92)
    glass = material("glass", None, rough=0.08)
    dark = material("trim", grain, rough=0.7)
    pc = p["paint"]; R, F = p["L"]; FN = F - 0.02 + 2 * p["nose"]; W = p["W"]; B = p["bottom"]
    body = extrude_profile("body", [(R, B + 0.04), (R + 0.04, B), (F - 0.05, B), (F, B + 0.08), (F + 0.02, p["belt"]),
                                    (p["shield_x"], p["shield_top"]), (p["shield_x"] - 0.12, p["roof"]), (R + 0.06, p["roof"]),
                                    (R, p["roof"] - 0.06)], W, paint_m, pc, bevel=p["bevel"], segs=3)
    nose = box("nose", (F - 0.12, 0, B + 0.36), (0.36 + 2 * p["nose"], W + 0.02, 0.78), paint_m, pc, bevel=0.17, segs=4)
    boolean(body, nose, "UNION")
    for x in p["wb"]:
        cut = cylinder("_arch", p["arch_r"], p["arch_r"], W + 0.2, 28, loc=(x, 0, p["wheel_r"] + 0.03), rot=(math.pi / 2, 0, 0), smooth=False)
        boolean(body, cut)
    box_uv(body, 1.5); paint(body, pc, noise=0.05, seed=1, grad=(0.3, 1.1, p["dirt"]))
    yw = W / 2 + 0.004
    # windshield (split) and side cab windows
    bx0, bz0, bx1, bz1 = F + 0.02, p["belt"] + 0.06, p["shield_x"] + 0.0, p["shield_top"] - 0.05
    n = Vector((bz1 - bz0, 0, -(bx1 - bx0))).normalized() * 0.012
    for s in (-1, 1):
        y0, y1 = s * 0.03, s * (W / 2 - 0.10)
        pts = [Vector((bx0, y0, bz0)) + n, Vector((bx0, y1, bz0)) + n, Vector((bx1, y1, bz1)) + n, Vector((bx1, y0, bz1)) + n]
        quad("shield%d" % s, pts if s < 0 else list(reversed(pts)), glass, (0.10, 0.11, 0.12))
        cx0, cx1 = 0.90, F - 0.22
        pts = [(cx0, s * yw, p["belt"] + 0.03), (cx1, s * yw, p["belt"] + 0.03), (cx1 - 0.04, s * yw, p["roof"] - 0.16), (cx0, s * yw, p["roof"] - 0.16)]
        quad("side%d" % s, pts if s > 0 else list(reversed(pts)), glass, (0.10, 0.11, 0.12))
        # door seams: cab door front/back, cargo doors
        for k, sx in enumerate((0.80, F - 0.12, 0.05, -0.95)):
            sm = box("seam%d%d" % (s, k), (sx, s * (W / 2 + 0.002), (B + p["roof"]) / 2 - 0.08), (0.012, 0.006, p["roof"] - B - 0.35), dark, (0.2, 0.2, 0.2)); box_uv(sm, 0.5)
        h = box("handle%d" % s, (0.70, s * (W / 2 + 0.01), p["belt"] - 0.1), (0.12, 0.02, 0.03), chrome, (0.85, 0.85, 0.85)); box_uv(h, 0.5)
        m = box("mirror%d" % s, (F - 0.10, s * (W / 2 + 0.14), p["belt"] + 0.15), (0.03, 0.1, 0.12), chrome, (0.85, 0.85, 0.85)); box_uv(m, 0.5)
        # headlight pods, lamps, mesh panels
        hy = s * (W / 2 - 0.30)
        pod = box("pod%d" % s, (FN + 0.03, hy, B + 0.62), (0.05, 0.40, 0.24), paint_m, pc, bevel=0.06, segs=2); box_uv(pod, 0.6)
        mesh = box("grillepod%d" % s, (FN + 0.055, hy + s * 0.06, B + 0.62), (0.02, 0.22, 0.17), dark, (0.30, 0.30, 0.30), bevel=0.02); box_uv(mesh, 0.3)
        hl = cylinder("hl%d" % s, 0.085, 0.085, 0.05, 16, chrome, (0.92, 0.92, 0.90), loc=(FN + 0.06, hy - s * 0.12, B + 0.62), rot=(0, math.pi / 2, 0)); box_uv(hl, 0.5)
        tl = box("taillight%d" % s, (R - 0.01, s * (W / 2 - 0.08), B + 0.5), (0.03, 0.08, 0.18), dark, (0.55, 0.12, 0.10)); box_uv(tl, 0.3)
    gr = box("grille", (FN + 0.02, 0, B + 0.22), (0.04, W - 0.5, 0.26), dark, (0.20, 0.21, 0.21), bevel=0.01); box_uv(gr, 0.5)
    for k in range(3):
        bar = box("gbar%d" % k, (FN + 0.045, 0, B + 0.13 + 0.08 * k), (0.02, W - 0.55, 0.02), chrome, (0.85, 0.85, 0.84)); box_uv(bar, 0.5)
    fb = box("fbumper", (FN + 0.08, 0, B - 0.03), (0.10, W + 0.02, 0.14), chrome, p["bumper"], bevel=0.03); box_uv(fb, 0.6)
    rb = box("rbumper", (R - 0.07, 0, B - 0.03), (0.10, W + 0.02, 0.14), chrome, p["bumper"], bevel=0.03); box_uv(rb, 0.6)
    # rear door glass
    for s in (-1, 1):
        quad("rearwin%d" % s, [(R - 0.004, s * 0.08, p["belt"] + 0.1), (R - 0.004, s * (W / 2 - 0.15), p["belt"] + 0.1), (R - 0.004, s * (W / 2 - 0.15), p["roof"] - 0.2), (R - 0.004, s * 0.08, p["roof"] - 0.2)][::(-s)], glass, (0.10, 0.11, 0.12))
    for x in p["wb"]:
        for s in (-1, 1):
            car_wheel(x, s * (W / 2 - 0.04), p["wheel_r"], s, rubber, paint_m, (0.85, 0.85, 0.82), chrome, (0.9, 0.9, 0.88), width=0.2, rim_r=0.21)
    ch = box("chassis", (0, 0, B - 0.03), (F - R - 0.5, W - 0.5, 0.16), dark, (0.2, 0.2, 0.2)); box_uv(ch, 1)

def view(p):
    return dict(cam_loc=(3.2, -3.7, 1.05), target=(0.0, 0, 0.95), focal=25, sun=(60, 210), res=(960, 640))

finish("BC2_VAN01", build, D, view)
