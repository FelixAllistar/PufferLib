# BC2_SPEEDSIGN01: US-style regulatory speed sign (white panel, black border
# and lettering) on a galvanised round post. Generic text only. Lettering is
# Blender's built-in text font converted to mesh. Reference: Wikimedia
# Commons "HGV-Bus Speed Limit Sign.jpg" by CharlieDueckPlett (CC0), see
# LOG.md. Front = +X, origin at the post base.
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe2_common import *

D = dict(grain=0.12, white=[0.93, 0.93, 0.91], font=1.3, post_h=2.68, post_r=0.028, pw=0.60, ph=1.10, bottom=1.6, border=0.03, inset=0.012,
         lines=[["SPEED", 0.07], ["LIMIT", 0.07], ["25", 0.18], ["MPH", 0.06], ["", 0.05], ["TRUCKS", 0.07], ["& BUSES", 0.07], ["15", 0.18], ["MPH", 0.06]],
         black=[0.08, 0.08, 0.08], galv=[0.62, 0.63, 0.62], tilt=2.0)

def text_mesh(s, size, z, mat, col, x):
    cu = bpy.data.curves.new("t", "FONT"); cu.body = s; cu.size = size; cu.align_x = "CENTER"; cu.align_y = "CENTER"
    cu.extrude = 0.0015; cu.resolution_u = 3
    ob = bpy.data.objects.new("txt_" + s, cu); bpy.context.scene.collection.objects.link(ob)
    ob.rotation_euler = (math.pi / 2, 0, math.pi / 2); ob.location = (x, 0, z)
    bpy.context.view_layer.objects.active = ob; ob.select_set(True)
    bpy.ops.object.convert(target="MESH"); ob.select_set(False)
    ob.data.materials.append(mat); paint(ob, col); box_uv(ob, 0.3)
    return ob

def build(p):
    grain = image("dirty_concrete", 256, gray=True, mean=0.9, gain=p.get("grain", 0.35))
    sheet = material("sheeting", grain, rough=0.45)
    ink = material("ink", grain, rough=0.6)
    galv = material("galv", grain, rough=0.4, metal=0.7)
    post = cylinder("post", p["post_r"], p["post_r"], p["post_h"], 10, galv, p["galv"], loc=(-0.035, 0, p["post_h"] / 2)); cyl_uv(post, 1, 1)
    cap = cylinder("cap", p["post_r"] * 1.15, p["post_r"] * 1.15, 0.03, 10, galv, p["galv"], loc=(-0.035, 0, p["post_h"] + 0.015)); cyl_uv(cap, 1, 1)
    sl = cylinder("sleeve", p["post_r"] * 1.3, p["post_r"] * 1.3, 0.25, 10, galv, [c * 0.9 for c in p["galv"]], loc=(-0.035, 0, 0.12)); cyl_uv(sl, 1, 1)
    pw, ph, b = p["pw"], p["ph"], p["bottom"]
    zc = b + ph / 2
    back = box("panel", (0.0, 0, zc), (0.008, pw, ph), galv, p["galv"], bevel=0.003); box_uv(back, 0.5)
    face = box("face", (0.0055, 0, zc), (0.003, pw - 0.004, ph - 0.004), sheet, p["white"]); box_uv(face, 0.5)
    i = p["inset"]; t = p["border"]
    for (cy, cz, sy, sz) in ((0, zc + ph / 2 - i - t / 2, pw - 2 * i, t), (0, zc - ph / 2 + i + t / 2, pw - 2 * i, t),
                             (pw / 2 - i - t / 2, zc, t, ph - 2 * i), (-pw / 2 + i + t / 2, zc, t, ph - 2 * i)):
        br = box("border", (0.0075, cy, cz), (0.001, sy, sz), ink, p["black"]); box_uv(br, 0.3)
    # lettering, top to bottom
    total = sum(h for _, h in p["lines"]); gap = (ph - 2 * i - 2 * t - 0.06 - total) / (len(p["lines"]) - 1)
    z = zc + ph / 2 - i - t - 0.03
    for s, h in p["lines"]:
        z -= h / 2
        if s: text_mesh(s, h * p.get("font", 1.32), z, ink, p["black"], 0.0085)
        z -= h / 2 + gap
    for zz in (b + 0.12, b + ph - 0.12):
        bolt = cylinder("bolt", 0.012, 0.012, 0.01, 6, galv, p["galv"], loc=(0.0085, 0, zz), rot=(0, math.pi / 2, 0)); cyl_uv(bolt, 1, 1)

def view(p):
    return dict(cam_loc=(4.6, -1.0, 1.45), target=(0, 0, 1.55), focal=30, sun=(55, 200), res=(720, 960), ground=(0.55, 0.53, 0.50), sky=(0.35, 0.55, 0.95))

finish("BC2_SPEEDSIGN01", build, D, view)
