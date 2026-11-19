# Briar Court interior props v2: electric baseboard heater (enamelled steel
# housing, top discharge louvre, end caps, thermostat knob).
# Reference: HABS CA-2139 photo 11, "Interior of living room showing back wall
# electrical heater" (LOC ca2139.photos.315142p), public domain.
#
#   blender -b --factory-startup --python-exit-code 1 -P props_int_v2_heater.py -- OUT_DIR [--render PNG]
#
# Floor piece against a wall: origin on the floor at the centre of the back
# edge, front +Z, back face at z = 0.
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from props_int_v2_common import *

P = dict(L=1.10, H=.20, D=.068, cap=.022, slot=.024, gap=.07,
         enamel=(.74, .71, .64), dark=(.05, .05, .05))
reset()
rng = np.random.default_rng(1971)
# Enamel: flat factor with a baked dust/scuff map (darker toward the floor).
n = 256
grime = periodic_noise(rng, n, 6, 4)
yy = np.linspace(0, 1, n)[:, None]
shade = .93 + .07 * grime - .10 * np.clip(yy - .55, 0, 1) * 2
rgb = np.dstack([shade * c for c in P["enamel"]])
enamel_img = to_image("heater_enamel", rgb)
enamel = mat("heater_enamel", image=enamel_img, rough=.45)
dark = mat("heater_slot", P["dark"], .8)
knob = mat("heater_knob", (.12, .11, .10), .5)
finm = mat("heater_fin", (.22, .21, .19), .6)

L, H, D, C = P["L"], P["H"], P["D"], P["cap"]
part = Part()
body_l = L - 2 * C
# Main housing front: vertical lower face + a sloped upper face to the louvre.
prof = [(0, P["gap"]), (D, P["gap"]), (D, H * .80), (D * .62, H - .004), (0, H - .004)]
bm = bmesh.new()
for sx in (-1, 1):
    pass
verts_l = [bm.verts.new(E(-body_l / 2, y, z)) for z, y in prof]
verts_r = [bm.verts.new(E(body_l / 2, y, z)) for z, y in prof]
for i in range(len(prof) - 1):
    f = bm.faces.new((verts_l[i], verts_l[i + 1], verts_r[i + 1], verts_r[i]))
    f.material_index = 1 if i == 2 else 0
bm.normal_update()
bm.faces.ensure_lookup_table()
# Outward: the front face normal must point +Z engine (= -Y Blender).
if bm.faces[1].normal.y > 0:
    bmesh.ops.reverse_faces(bm, faces=list(bm.faces))
# The sloped upper face is the discharge grille: dark, with enamel fins.
grille = bmesh.new()
for f in [f for f in bm.faces if f.material_index == 1]:
    vs = [grille.verts.new(v.co) for v in f.verts]; grille.faces.new(vs)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index == 1], context='FACES_ONLY')
part.add(bm, enamel, uv_mode="fit")
part.add(grille, dark)
for k in range(int(body_l / .06)):
    x = -body_l / 2 + .03 + k * .06
    fin = bm_box((x, H * .90, D * .80), (.0015, H * .09, .002))
    tilt_bm(fin, -.9, 'x', (x, H * .86, D * .78))
    part.add(fin, finm)
# Discharge louvre: dark slot along the top behind the sloped face, with fins.
part.add(bm_box((0, H - .006, D * .30), (body_l / 2, .003, D * .22)), dark)
# Intake gap under the housing (dark).
part.add(bm_box((0, P["gap"] + .004, D * .5), (body_l / 2, .004, D * .45)), dark)
# Lower front: recessed dark intake louvre band.
part.add(bm_box((0, P["gap"] + .030, D + .0015), (body_l / 2 - .004, .018, .0015)), finm)
# End caps, a little taller and deeper, rounded.
for sx in (-1, 1):
    part.add(bm_box((sx * (L / 2 - C / 2), (H + P["gap"]) / 2, (D + .003) / 2), (C / 2, (H - P["gap"]) / 2, (D + .003) / 2), bevel=.003, seg=1), enamel)
# Thermostat knob on the right end cap top.
part.add(bm_cyl((L / 2 - C / 2, H + .006, D * .55), .009, .012, seg=12), knob)
ob = export("wall_heater", part)

if RENDER:
    ob.location = E(0, 0, 0)
    preview({"cam": (-0.1, 0.95, 1.7), "target": (0.0, 0.1, 0.0), "fov": 45,
             "room": {"box": (-2.2, 2.2, 0.0, 4.5, 2.6), "walls": ["back", "left", "right", "ceiling"],
                      "wall": (.78, .76, .72), "floor": (.16, .16, .16)},
             "lights": [((-1.6, 1.6, 1.2), 90, (1, 1, 1), 1.2), ((0.5, 2.4, 3.0), 60, (1, 1, 1), .8)],
             "ambient": .4, "sky": (.8, .8, .8), "agx": True})
