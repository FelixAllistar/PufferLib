"""Briar Court exterior kit: material library + render-only facade dressing.

Run (after motel_exterior_textures.py):
  blender -b --factory-startup --python-exit-code 1 \
    -P ocean/swat/tools/blender/motel_exterior_kit.py -- TEX_DIR OUT_DIR

Inputs : TEX_DIR with the baked maps from motel_exterior_textures.py.
Outputs: OUT_DIR/exterior_kit.glb   one GLB, every piece modelled about its own
                                    datum with identity node transforms
         OUT_DIR/placements.json    engine-space placements + owner tags
         (tools/import_motel_exterior.py turns both into motel_exterior_data.h)

Conventions: geometry is written in *engine* coordinates (metres, +Y up, +Z
toward the parking court / outward from a wall) and converted to Blender's
Z-up once (engine (x,y,z) -> Blender (x,-z,y)); the glTF exporter's +Y-up
conversion maps it back exactly. Pieces named L_* are material-library
swatches (two triangles each) whose materials replace the flat placeholder
maps of the motel_v1 shell at load time. Pieces named P_* are presentation
dressing. Nothing here is collidable; the game adds no colliders for them.
Deterministic: no randomness except a fixed-seed numpy RNG for tiny maps.
"""
import bpy, bmesh, sys, os, json, math
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
TEX, OUT = argv[0], argv[1]
os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
UVS = 1.1  # UV units per metre, matching the motel_v1 shell mapping

# ---------------------------------------------------------------- materials
def image(name, colorspace='sRGB'):
    img = bpy.data.images.load(os.path.join(TEX, name), check_existing=True)
    img.colorspace_settings.name = colorspace
    return img

def small_image(name, rgb, size=8, noise=0.0, seed=1):
    rng = np.random.default_rng(seed)
    a = np.ones((size, size, 4), np.float32); a[..., :3] = np.array(rgb, np.float32)
    if noise: a[..., :3] *= (1 + noise * (rng.random((size, size, 1)) - .5))
    img = bpy.data.images.new(name, size, size, alpha=False)
    img.colorspace_settings.name = 'sRGB'
    img.pixels.foreach_set(np.clip(a, 0, 1).ravel())
    img.file_format = 'PNG'; img.pack()
    return img

def pbr(name, base, normal=None, orm=None, rough=1.0, metal=0.0, emissive=None):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; bsdf = nt.nodes['Principled BSDF']
    bsdf.inputs['Roughness'].default_value = rough
    bsdf.inputs['Metallic'].default_value = metal
    if isinstance(base, tuple):
        bsdf.inputs['Base Color'].default_value = (*base, 1)
    else:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = base
        nt.links.new(t.outputs['Color'], bsdf.inputs['Base Color'])
    if normal is not None:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = normal
        nm = nt.nodes.new('ShaderNodeNormalMap')
        nt.links.new(t.outputs['Color'], nm.inputs['Color'])
        nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    if orm is not None:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = orm
        sep = nt.nodes.new('ShaderNodeSeparateColor')
        nt.links.new(t.outputs['Color'], sep.inputs['Color'])
        nt.links.new(sep.outputs['Green'], bsdf.inputs['Roughness'])
        nt.links.new(sep.outputs['Blue'], bsdf.inputs['Metallic'])
    if emissive is not None:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = emissive
        nt.links.new(t.outputs['Color'], bsdf.inputs['Emission Color'])
        bsdf.inputs['Emission Strength'].default_value = 1.0
    return m

nrm = lambda n: image(f'{n}_normal.png', 'Non-Color')
orm = lambda n: image(f'{n}_orm.png', 'Non-Color')
col = lambda n: image(f'{n}_basecolor.png')
LIB = {
    'stucco': pbr('LIB_stucco', col('stucco'), nrm('stucco'), orm('stucco')),
    'repair': pbr('LIB_repair', col('repair'), nrm('stucco'), orm('stucco')),
    'concrete': pbr('LIB_concrete', col('concrete'), nrm('concrete'), orm('concrete')),
    'concrete_dark': pbr('LIB_concrete_dark', col('concrete_dark'), nrm('concrete'), orm('concrete')),
    'roof': pbr('LIB_roof', col('roof'), nrm('concrete'), orm('concrete')),
    'cream': pbr('LIB_cream', col('cream'), nrm('plaster'), orm('plaster')),
    'teal': pbr('LIB_teal', col('teal'), nrm('paint'), orm('paint')),
    'ochre': pbr('LIB_ochre', col('ochre'), nrm('paint'), orm('paint')),
}
GALV = pbr('KIT_galvanized', small_image('galv', (.56, .58, .56), 16, .12, 3), nrm('paint'), None, rough=.5, metal=.75)
IRON = pbr('KIT_black_iron', (.035, .037, .036), nrm('paint'), None, rough=.55, metal=.6)
GLOW = small_image('lamp_glow', (1.0, .78, .52))
LAMP_GLASS = pbr('KIT_lamp_glass', (1.0, .86, .66), None, None, rough=.25, metal=0, emissive=GLOW)
PACK_LENS = pbr('KIT_wallpack_lens', (.95, .92, .85), None, None, rough=.3, metal=0, emissive=small_image('pack_glow', (1.0, .9, .72)))
UNIT = pbr('KIT_hvac_beige', small_image('hvac', (.62, .60, .52), 16, .10, 5), nrm('paint'), None, rough=.6, metal=.3)
GRILLE = pbr('KIT_grille', (.09, .09, .085), None, None, rough=.6, metal=.5)

# ---------------------------------------------------------------- geometry
def E(x, y, z): return Vector((x, -z, y))  # engine -> Blender

class Piece:
    def __init__(self, name):
        self.name = name; self.bm = bmesh.new(); self.mats = []
    def mat(self, m):
        if m not in self.mats: self.mats.append(m)
        return self.mats.index(m)
    def face(self, pts, m):
        vs = [self.bm.verts.new(E(*p)) for p in pts]
        f = self.bm.faces.new(vs); f.material_index = self.mat(m); return f
    def box(self, lo, hi, m, skip=()):
        (x0, y0, z0), (x1, y1, z1) = lo, hi
        F = {'-x': [(x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (x0, y0, z0)],
             '+x': [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)],
             '-y': [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)],
             '+y': [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)],
             '-z': [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)],
             '+z': [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]}
        for k, pts in F.items():
            if k not in skip: self.face(pts, m)
    def prism(self, prof, x0, x1, m, caps=True):
        """Extrude a closed CCW (z,y) profile along x."""
        n = len(prof)
        for i in range(n):
            (za, ya), (zb, yb) = prof[i], prof[(i + 1) % n]
            self.face([(x0, ya, za), (x0, yb, zb), (x1, yb, zb), (x1, ya, za)], m)
        if caps:
            self.face([(x1, y, z) for z, y in prof], m)
            self.face([(x0, y, z) for z, y in reversed(prof)], m)
    def tube(self, path, r, m, seg=8, caps=True, square=False):
        """Swept tube along a polyline of engine points (mitred corners)."""
        P = [Vector(p) for p in path]
        rings = []
        for i, p in enumerate(P):
            d0 = (P[i] - P[i - 1]).normalized() if i > 0 else None
            d1 = (P[i + 1] - P[i]).normalized() if i + 1 < len(P) else None
            t = ((d0 or d1) + (d1 or d0)).normalized()
            up = Vector((0, 1, 0)) if abs(t.y) < .9 else Vector((1, 0, 0))
            a = t.cross(up).normalized(); b = t.cross(a).normalized()
            k = 1.0
            if d0 and d1: k = 1 / max(.3, d0.dot(t))
            ring = []
            for s in range(seg):
                ang = 2 * math.pi * (s + .5) / seg
                if square: ca, sa = math.copysign(1, math.cos(ang)), math.copysign(1, math.sin(ang))
                else: ca, sa = math.cos(ang), math.sin(ang)
                # mitre: stretch along the bend plane only
                off = a * (ca * r[0] if isinstance(r, tuple) else ca * r) + b * (sa * r[1] if isinstance(r, tuple) else sa * r)
                if d0 and d1:
                    bend = (d1 - d0);
                    if bend.length > 1e-6:
                        bn = bend.normalized(); off = off + bn * (off.dot(bn)) * (k - 1)
                ring.append(p + off)
            rings.append(ring)
        for i in range(len(rings) - 1):
            for s in range(seg):
                s1 = (s + 1) % seg
                self.face([tuple(rings[i][s]), tuple(rings[i][s1]), tuple(rings[i + 1][s1]), tuple(rings[i + 1][s])], m)
        if caps:
            self.face([tuple(v) for v in reversed(rings[0])], m)
            self.face([tuple(v) for v in rings[-1]], m)
    def cyl(self, c, r, h, m, seg=12, axis='y', caps=(True, True)):
        cx, cy, cz = c; ring0, ring1 = [], []
        for s in range(seg):
            a = 2 * math.pi * s / seg; u, v = math.cos(a) * r, math.sin(a) * r
            if axis == 'y': ring0.append((cx + u, cy, cz + v)); ring1.append((cx + u, cy + h, cz + v))
            elif axis == 'z': ring0.append((cx + u, cy + v, cz)); ring1.append((cx + u, cy + v, cz + h))
            else: ring0.append((cx, cy + u, cz + v)); ring1.append((cx + h, cy + u, cz + v))
        for s in range(seg):
            s1 = (s + 1) % seg
            self.face([ring0[s1], ring0[s], ring1[s], ring1[s1]], m)
        if caps[0]: self.face(ring0, m)
        if caps[1]: self.face(list(reversed(ring1)), m)
    def finish(self):
        bm = self.bm
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        # Only closed shells can be re-oriented reliably; open shells keep
        # their authored (outward) winding.
        if all(e.is_manifold for e in bm.edges):
            bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        uv = bm.loops.layers.uv.new('UVMap')
        for f in bm.faces:
            n = f.normal; ax = max(range(3), key=lambda i: abs(n[i]))
            for l in f.loops:
                p = l.vert.co
                u, v = ((p.y, p.z), (p.x, p.z), (p.x, p.y))[ax]
                l[uv].uv = (u * UVS, v * UVS)
        me = bpy.data.meshes.new(self.name); bm.to_mesh(me); bm.free()
        for m in self.mats: me.materials.append(m)
        for p in me.polygons: p.use_smooth = False
        ob = bpy.data.objects.new(self.name, me); bpy.context.scene.collection.objects.link(ob)
        return ob

PIECES = []
def piece(name):
    p = Piece(name); PIECES.append(p); return p

# Library swatches (one quad per library material).
for i, (k, m) in enumerate(LIB.items()):
    p = piece(f'L_{k}'); p.face([(0, 0, 0), (.1, 0, 0), (.1, .1, 0), (0, .1, 0)], m)

# K-style gutter: datum = top of the back (mounting) edge; runs along x,
# opens toward +y, projects toward +z. 4 m run, hangers every ~0.8 m.
def gutter(name, m, length=4.0):
    p = piece(name); h = length / 2
    outer = [(0, 0), (0, -.105), (.075, -.105), (.095, -.095), (.11, -.075), (.115, -.05), (.125, -.035), (.13, -.012), (.135, 0)]
    t = .006
    # Simple polygon: the outer skin, then the inner skin back (6 mm sheet).
    prof = outer + [(.135 - t, 0), (.13 - t, -.012), (.125 - t, -.035 + t * .5), (.115 - t, -.05 + t * .5),
                    (.11 - t, -.075 + t * .7), (.095 - t * .7, -.095 + t), (.075, -.105 + t), (t, -.105 + t), (t, 0)]
    p.prism(prof, -h, h, m)
    n = int(round(length / .8))
    for i in range(n):
        x = -h + (i + .5) * length / n
        p.box((x - .015, -.004, 0), (x + .015, 0, .135), m)  # hidden hanger strap
    return p
gutter('P_gutter_ochre', LIB['ochre'])
gutter('P_gutter_galv', GALV)
for name, m in (('P_gutter_cap_ochre', LIB['ochre']), ('P_gutter_cap_galv', GALV)):
    p = piece(name)
    p.prism([(0, 0), (0, -.108), (.078, -.108), (.138, 0)], -.003, .003, m)

# Rectangular 3x2 in downspout (75 x 55 mm), datum = gutter back edge line.
def downspout(name, m, top, wall, outlet=.062, ground=0.0, splash=True):
    """top: gutter bottom height; wall: local z of the mounting surface (<0)."""
    p = piece(name); r = (.0375, .0275)
    zc = wall + .0275 + .006
    path = [(0, top + .02, outlet), (0, top - .06, outlet), (0, top - .24, zc), (0, ground + .32, zc),
            (0, ground + .16, zc + .16), (0, ground + .12, zc + .26)]
    p.tube(path, r, m, seg=4, square=True)
    for y in (top - .5, (top + ground) * .5, ground + .55):  # wall straps
        p.box((-.045, y - .015, wall), (.045, y + .015, zc + .03), m)
    if splash:
        y0 = ground - .005
        p.box((-.16, y0, zc + .12), (.16, y0 + .035, zc + .55), LIB['concrete'])
        p.box((-.12, y0 + .035, zc + .14), (.12, y0 + .04, zc + .53), LIB['concrete_dark'])
    return p
# Front: gutter bottom 2.49 m, canopy post face 0.16 m behind the gutter line,
# court surface -0.08 m. Rear: gutter bottom 2.885 m, wall 0.0575 m behind.
downspout('P_downspout_front', LIB['ochre'], 2.495, -.16, ground=-.08)
downspout('P_downspout_rear', GALV, 2.885, -.0575, ground=0.0)

# Caged "jelly jar" porch lamp: datum = wall surface at the backplate centre.
p = piece('P_porch_lamp')
p.box((-.06, -.10, 0), (.06, .10, .012), IRON)           # backplate
p.box((-.035, -.03, .012), (.035, .03, .05), IRON)       # arm/box
p.cyl((0, -.03, .095), .055, .025, IRON, seg=12, axis='y')  # fitter ring under the cap
p.cyl((0, .085, .095), .058, .02, IRON, seg=12, axis='y')   # cap
p.cyl((0, -.005, .095), .045, .09, LAMP_GLASS, seg=12, axis='y')  # glass jar
for a in range(4):
    ang = math.pi / 4 + a * math.pi / 2; x, z = math.cos(ang) * .053, .095 + math.sin(ang) * .053
    p.box((x - .004, -.03, z - .004), (x + .004, .085, z + .004), IRON)
p.cyl((0, .03, .095), .056, .008, IRON, seg=12, axis='y', caps=(False, False))
p.box((-.02, .05, .012), (.02, .07, .05), IRON)

# Security wall-pack: datum = wall surface at the back centre.
p = piece('P_wallpack')
p.box((-.13, -.11, 0), (.13, .11, .08), IRON)
p.prism([(0, .11), (.08, .11), (.15, .06), (.15, -.06), (.08, -.11), (0, -.11)], -.12, .12, IRON)
p.box((-.10, -.075, .149), (.10, .045, .152), PACK_LENS)
p.cyl((0, -.11, .03), .012, .2, IRON, seg=6, axis='y', caps=(False, True))  # conduit stub
p.box((-.02, -.25, 0), (.02, -.11, .03), IRON)

# Membrane overlay for the gallery canopy deck: datum = canopy origin. The
# deck's ribbed sheet and cream slab top stay underneath (top at 0.18 m).
p = piece('P_canopy_membrane')
p.box((-2, .181, -.04), (2, .187, 1.921), LIB['roof'], skip=('-y',))
for x in (-1.0, 0.0, 1.0):  # lapped seams every metre
    p.box((x - .04, .187, -.04), (x + .04, .191, 1.921), LIB['roof'], skip=('-y',))

# Precast window sill: datum = wall face at the window centre, top = 0.
p = piece('P_window_sill')
p.box((-1.11, -.055, -.015), (1.11, 0, .085), LIB['concrete'])
p.box((-1.11, -.075, -.015), (1.11, -.055, .07), LIB['concrete'])  # drip lip
for x in (-1.11, 1.11):
    p.box((x - .002, -.06, -.015), (x + .002, -.01, .086), LIB['concrete_dark'])

# Electric meter base with conduit to the roof edge: datum = wall face, ground.
p = piece('P_meter')
p.box((-.15, 1.15, 0), (.15, 1.55, .11), GALV)
p.cyl((0, 1.38, .11), .075, .07, GRILLE, seg=14, axis='z', caps=(False, True))
p.cyl((0, 1.38, .11), .078, .012, GALV, seg=14, axis='z', caps=(True, False))
p.cyl((0, 1.55, .04), .02, 1.30, GALV, seg=8, axis='y')
p.cyl((0, .25, .04), .02, .9, GALV, seg=8, axis='y')
p.box((-.12, .30, 0), (.12, .55, .09), IRON)  # disconnect box
for y in (1.9, 2.45): p.box((-.035, y - .012, 0), (.035, y + .012, .065), GALV)

# Bathroom exhaust louvre hood: datum = wall face centre.
p = piece('P_exhaust_hood')
p.box((-.11, -.11, 0), (.11, .11, .012), GALV)
p.prism([(.012, .09), (.11, .09), (.11, .07), (.012, -.09)], -.09, .09, GALV)
for i in range(4):
    y = -.075 + i * .04
    p.box((-.085, y, .012), (.085, y + .012, .05), IRON)

# Canopy post base plate with anchor nuts and a rust collar: datum = post foot.
p = piece('P_post_base')
p.box((-.13, 0, -.13), (.13, .014, .13), IRON)
for sx in (-1, 1):
    for sz in (-1, 1):
        p.cyl((sx * .095, .014, sz * .095), .013, .018, GALV, seg=6, axis='y')
p.box((-.087, .014, -.087), (.087, .09, .087), IRON, skip=('+y', '-y'))

# Packaged rooftop condensing unit on a curb: datum = roof surface centre.
p = piece('P_rooftop_unit')
p.box((-.55, 0, -.42), (.55, .12, .42), LIB['concrete_dark'])
p.box((-.5, .12, -.37), (.5, .86, .37), UNIT)
for i in range(9):
    z = -.32 + i * .08
    p.box((-.505, .25, z), (-.5, .75, z + .045), GRILLE)
    p.box((.5, .25, z), (.505, .75, z + .045), GRILLE)
p.cyl((0, .86, 0), .3, .03, GRILLE, seg=20, axis='y')
p.box((-.32, .89, -.012), (.32, .9, .012), IRON); p.box((-.012, .89, -.32), (.012, .9, .32), IRON)
p.tube([(.3, .5, .37), (.3, .5, .55), (.3, .02, .55), (.3, .02, .9)], .022, IRON, seg=6)
p.box((.22, 0, .9), (.38, .1, 1.05), IRON)

# Plumbing vent stack + mushroom vent: datum = roof surface.
p = piece('P_roof_vents')
p.cyl((0, 0, 0), .11, .03, IRON, seg=10)
p.cyl((0, 0, 0), .045, .42, IRON, seg=10)
p.cyl((1.1, 0, .4), .2, .03, GALV, seg=12)
p.cyl((1.1, .03, .4), .09, .24, GALV, seg=12)
p.cyl((1.1, .27, .4), .19, .08, GALV, seg=14)

# Gooseneck sign floodlight: datum = top of the sign cabinet, +z = sign face.
p = piece('P_sign_flood')
p.box((-.05, 0, -.04), (.05, .02, .04), IRON)
p.tube([(0, .01, 0), (0, .32, 0), (0, .42, .12), (0, .40, .48)], .016, IRON, seg=6, caps=False)
p.cyl((0, .27, .52), .11, .16, IRON, seg=12, axis='y', caps=(False, True))
p.cyl((0, .265, .52), .1, .005, LAMP_GLASS, seg=12, axis='y', caps=(True, False))

objs = [p.finish() for p in PIECES]

# ---------------------------------------------------------------- placements
# owner = engine object tag (motel instance index + 1). mount: 'wall' resolves
# the surviving skin section of that wall at the origin; 'object' requires
# the owner itself to be active. roof hides the piece in the cutaway view.
PL = []
def place(piece, owner, x, y, z, yaw=0.0, mount='object', roof=False, scale=(1, 1, 1)):
    PL.append({'piece': piece, 'owner': owner, 'origin': [round(x, 4), round(y, 4), round(z, 4)],
               'yaw': round(yaw, 6), 'mount': mount, 'roof': roof, 'scale': list(scale)})
PI = math.pi
canopies = {-10: 5, -6: 12, -2: 36, 2: 60, 6: 84}
roofs = {-6: 10, -2: 34, 2: 58, 6: 82}
facades = {-6: 13, -2: 37, 2: 61, 6: 85}
rears = {-6: 14, -2: 38, 2: 62, 6: 86}
for cx, tag in canopies.items():
    place('P_gutter_ochre', tag, cx, 2.605, 2.0105, roof=True)
    place('P_canopy_membrane', tag, cx, 2.44, 0, roof=True)
place('P_gutter_cap_ochre', 5, -12.003, 2.605, 2.0105, roof=True)
place('P_gutter_cap_ochre', 84, 8.003, 2.605, 2.0105, roof=True)
place('P_downspout_front', 114, -12.0, 0, 2.0105)
place('P_downspout_front', 119, 8.0, 0, 2.0105)
for cx, tag in roofs.items():
    place('P_gutter_galv', tag, cx, 2.99, -6.1475, PI, roof=True)
place('P_gutter_cap_galv', 10, -8.003, 2.99, -6.1475, PI, roof=True)
place('P_gutter_cap_galv', 82, 8.003, 2.99, -6.1475, PI, roof=True)
place('P_gutter_galv', 128, -10.0, 2.99, -10.0993, PI, roof=True)
place('P_gutter_cap_galv', 128, -12.003, 2.99, -10.0993, PI, roof=True)
place('P_gutter_cap_galv', 128, -7.997, 2.99, -10.0993, PI, roof=True)
place('P_downspout_rear', 14, -7.62, 0, -6.1475, PI, mount='wall')
place('P_downspout_rear', 86, 7.62, 0, -6.1475, PI, mount='wall')
place('P_downspout_rear', 129, -11.62, 0, -10.0993, PI, mount='wall')
for cx, tag in facades.items():
    place('P_window_sill', tag, cx + .8, .875, .0905, mount='wall')
place('P_porch_lamp', 104, -12.0905, 2.2, -1.25, -PI / 2, mount='wall')
for cx, tag in rears.items():
    place('P_exhaust_hood', tag, cx + 1.25, 2.3, -6.0905, PI, mount='wall')
place('P_meter', 38, -2.7, 0, -6.0905, PI, mount='wall')
place('P_meter', 86, 5.3, 0, -6.0905, PI, mount='wall')
place('P_wallpack', 14, -4.7, 2.42, -6.0905, PI, mount='wall')
place('P_wallpack', 62, 3.3, 2.42, -6.0905, PI, mount='wall')
place('P_wallpack', 109, 8.0905, 2.35, -3.0, PI / 2, mount='wall')
for x, tag in zip((-12, -8, -4, 0, 4, 8), range(114, 120)):
    place('P_post_base', tag, x, 0, 1.77)
place('P_rooftop_unit', 34, -2.4, 3.02, -3.6, 0, roof=True)
place('P_rooftop_unit', 82, 5.6, 3.02, -3.2, 0, roof=True)
place('P_rooftop_unit', 128, -10.4, 3.02, -8.2, PI / 2, roof=True)
for cx, tag in roofs.items():
    place('P_roof_vents', tag, cx - .9, 3.02, -4.6, 0, roof=True)
# Roadside sign cabinet top (4.10 m), yaw -0.13 rad about (9.7, -0.08, 8).
sy = -.13
for lx in (-.8, .8):
    x = 9.7 + math.cos(sy) * lx; z = 8.0 - math.sin(sy) * lx
    place('P_sign_flood', 140, x, -.08 + 4.10, z, sy)

# ---------------------------------------------------------------- export
names = [o.name for o in objs]
used = {p['piece'] for p in PL}
missing = used - set(names)
assert not missing, missing
path = os.path.join(OUT, 'exterior_kit.glb')
bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', export_yup=True, export_apply=False,
                          export_normals=True, export_tangents=False,
                          export_materials='EXPORT', export_image_format='AUTO', export_cameras=False,
                          export_lights=False, export_extras=False, use_selection=False)
json.dump({'schema': 'swat_motel_exterior_v1', 'units': 'metres, engine +Y up, +Z toward the court',
           'pieces': names, 'placements': PL}, open(os.path.join(OUT, 'placements.json'), 'w'), indent=1)
tris = 0
for o in objs:
    tris += sum(len(p.vertices) - 2 for p in o.data.polygons)
print('exterior kit', path, 'pieces', len(objs), 'placements', len(PL), 'piece triangles', tris)
