# BC2_WAGON01: generic 1970s compact station wagon, dirty white, black steel
# wheels, no badges. Reference: Wikimedia Commons "1970s Toyota Corolla
# estate (wagon) 1.2 Deluxe (E30) (10562484414).jpg" by Charlie from United
# Kingdom (CC BY 2.0), see LOG.md.
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(paint=[0.70, 0.69, 0.64], grain=0.2, L=(-2.05, 2.05), W=1.60, belt=0.90, roof=1.36, bottom=0.30,
         hood_front=0.80, shield=(0.62, -0.02), roof_end=-1.98, wheel_r=0.29, wb=(-1.22, 1.15), arch_r=0.36,
         gw=1.40, glass=[0.30, 0.32, 0.33], dirt=0.62, bumper=[0.80, 0.80, 0.78], bevel=0.05)

def build(p):
    grain = image("dirty_concrete", 512, gray=True, mean=0.88, gain=p["grain"])
    paint_m = material("paint", grain, rough=0.55)
    chrome = material("chrome", grain, rough=0.35, metal=0.5)
    rubber = material("rubber", grain, rough=0.92)
    glass = material("glass", None, rough=0.25)
    dark = material("trim", grain, rough=0.7)
    pc = p["paint"]; R, F = p["L"]; W = p["W"]; B = p["bottom"]; H = p["belt"]
    body = extrude_profile("body", [(R, B + 0.08), (R + 0.06, B), (F - 0.08, B), (F, B + 0.10), (F + 0.01, p["hood_front"]),
                                    (F - 0.12, p["hood_front"] + 0.03), (p["shield"][0], H + 0.02), (R + 0.02, H + 0.02), (R, H - 0.04)],
                           W, paint_m, pc, bevel=p["bevel"], segs=3)
    for x in p["wb"]:
        cut = cylinder("_arch", p["arch_r"], p["arch_r"], W + 0.2, 28, loc=(x, 0, p["wheel_r"] + 0.03), rot=(math.pi / 2, 0, 0), smooth=False)
        boolean(body, cut)
    box_uv(body, 1.5); paint(body, pc, noise=0.05, seed=1, grad=(0.25, 0.8, p["dirt"]))
    sx0, sx1 = p["shield"]
    gh = extrude_profile("cabin", [(sx0 + 0.05, H), (R + 0.03, H), (p["roof_end"], p["roof"] - 0.04), (p["roof_end"] + 0.06, p["roof"]),
                                   (sx1 + 0.04, p["roof"]), (sx1, p["roof"] - 0.03)], p["gw"], paint_m, pc, bevel=0.04, segs=2)
    box_uv(gh, 1.5); paint(gh, pc, noise=0.04, seed=2, grad=(0.3, 1.4, 0.9))
    yw = p["gw"] / 2 + 0.005
    gc = tuple(p.get("glass", (0.10, 0.11, 0.12)))
    for s in (-1, 1):
        a, b = H + 0.04, p["roof"] - 0.05
        # three side windows separated by pillars
        xs = [sx0 - 0.03, p.get("bpil", -0.35), p.get("cpil", -1.15), p["roof_end"] + 0.10]
        pil = lambda z: sx0 + (sx1 - sx0) * (z - H) / (p["roof"] - H)
        for k in range(3):
            x0, x1 = xs[k + 1] + 0.04, xs[k] - 0.04
            if k == 0: pts = [(x0, s * yw, a), (pil(a) - 0.07, s * yw, a), (pil(b) - 0.05, s * yw, b), (x0, s * yw, b)]
            else: pts = [(x0, s * yw, a), (x1, s * yw, a), (x1, s * yw, b), (x0, s * yw, b)]
            quad("win%d%d" % (s, k), pts, glass, gc)
        for k, sx in enumerate((p.get("bpil", -0.35) + 0.02, -1.25)):
            sm = box("seam%d%d" % (s, k), (sx, s * (W / 2 + 0.002), (B + H) / 2 + 0.02), (0.012, 0.006, H - B - 0.12), dark, (0.25, 0.25, 0.25)); box_uv(sm, 0.5)
        m = box("mirror%d" % s, (sx0 - 0.1, s * (W / 2 + 0.08), H + 0.12), (0.08, 0.04, 0.08), dark, (0.12, 0.12, 0.12)); box_uv(m, 0.5)
        hy = s * (W / 2 - 0.16)
        hl = cylinder("hl%d" % s, 0.085, 0.085, 0.05, 16, chrome, (0.92, 0.92, 0.90), loc=(F + 0.03, hy, B + 0.34), rot=(0, math.pi / 2, 0)); box_uv(hl, 0.5)
        ind = box("ind%d" % s, (F + 0.09, s * (W / 2 - 0.12), B + 0.03), (0.02, 0.10, 0.05), dark, (0.85, 0.45, 0.12)); box_uv(ind, 0.3)
        tl = box("taillight%d" % s, (R - 0.005, s * (W / 2 - 0.10), B + 0.40), (0.02, 0.10, 0.20), dark, (0.55, 0.12, 0.10)); box_uv(tl, 0.3)
    n = Vector((p["roof"] - H, 0, sx0 - sx1)).normalized() * 0.01
    quad("shield", [Vector((sx0, -yw + 0.05, H + 0.03)) + n, Vector((sx0, yw - 0.05, H + 0.03)) + n, Vector((sx1 + 0.04, yw - 0.07, p["roof"] - 0.04)) + n, Vector((sx1 + 0.04, -yw + 0.07, p["roof"] - 0.04)) + n], glass, gc)
    quad("rearwin", [(p["roof_end"] - 0.02, -yw + 0.08, H + 0.08), (p["roof_end"] - 0.02, yw - 0.08, H + 0.08), (p["roof_end"] + 0.03, yw - 0.1, p["roof"] - 0.07), (p["roof_end"] + 0.03, -yw + 0.1, p["roof"] - 0.07)], glass, gc)
    g = box("grille", (F + 0.005, 0, B + 0.34), (0.03, W - 0.16, 0.24), dark, (0.13, 0.13, 0.13), bevel=0.01); box_uv(g, 0.4)
    fb = box("fbumper", (F + 0.06, 0, B + 0.07), (0.08, W + 0.04, 0.12), chrome, p["bumper"], bevel=0.02); box_uv(fb, 0.6)
    rb = box("rbumper", (R - 0.05, 0, B + 0.07), (0.08, W + 0.04, 0.12), chrome, p["bumper"], bevel=0.02); box_uv(rb, 0.6)
    for x in p["wb"]:
        for s in (-1, 1):
            car_wheel(x, s * (W / 2 - 0.08), p["wheel_r"], s, rubber, dark, (0.16, 0.16, 0.16), dark, (0.22, 0.22, 0.22), width=0.17, rim_r=0.18)
    ch = box("chassis", (0, 0, B + 0.02), (F - R - 0.5, W - 0.4, 0.14), dark, (0.2, 0.2, 0.2)); box_uv(ch, 1)

def view(p):
    if p.get("side"): return dict(cam_loc=(0.0, 7.0, 1.0), target=(0, 0, 0.8), focal=35, sun=(40, 120), res=(960, 640))
    return dict(cam_loc=(3.7, 2.5, 1.3), target=(-0.15, 0, 0.6), focal=24, sun=(40, 120), res=(960, 640))

finish("BC2_WAGON01", build, D, view)
