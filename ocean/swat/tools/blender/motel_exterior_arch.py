"""Briar Court architecture overlay: roof mass, fascias, canopy structure,
wall plinths, door/window surrounds, AC sleeves, block coursing, rear
plumbing, scuppers/downspouts, roof equipment, office gable front and a
laundry annex parapet. Presentation only (no collider, navigation, cover,
sight or protocol data).

Run:
  blender -b --factory-startup --python-exit-code 1 \
    -P ocean/swat/tools/blender/motel_exterior_arch.py -- ASSET_DIR [PARAMS.json]

Inputs : ASSET_DIR/sections.json   authoritative instances + wall skin
                                    sections (tools/motel_exterior_sections.c)
Outputs: ASSET_DIR/architecture_kit.glb         one GLB, identity node
                                                 transforms, piece-local coords
         ASSET_DIR/architecture_placements.json  placements + owners
         (tools/import_motel_exterior.py --arch -> motel_exterior_arch_data.h)

Wall-mounted geometry is authored in world (engine) space and then clipped
into one piece per authoritative skin section (the section's prism along the
wall normal), so breaching a section removes exactly its part of every
overlay. Roof, canopy, post and office pieces belong to that object. Material
names MC15_<lib> borrow the exterior kit's tiled library maps at load; ARCH_*
materials are plain colours. Deterministic: no randomness.
"""
import bpy, bmesh, sys, os, json, math
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = argv[0]
P = dict(
    fascia_bottom=2.70, fascia_top=3.14, fascia_out=0.05, coping=0.03,
    canopy_fascia_bottom=2.26, canopy_fascia_top=2.74, canopy_fascia_z=2.005,
    rafter_step=0.6, rafter_w=0.05, rafter_h=0.16, beam_h=0.17, beam_w=0.16,
    post_w=0.17, brace=0.42, plinth_h=0.32, plinth_d=0.035, casing_w=0.11,
    casing_d=0.065, lintel_h=0.16, pilaster_w=0.14, pilaster_d=0.05,
    joint=0.011, course=0.2032, unit=0.4064, joint_d=0.0025,
    gable_ridge=4.05, gable_eave=2.80, gable_front=2.40, gable_back=-1.30,
    office_full=True, office_eave=2.85, office_ridge=3.55, office_front=2.45, office_fascia='red', office_fascia_h=0.40, office_sign_y=2.92, office_tie=True, office_tie_h=0.32, office_trim=True,
    annex_parapet=0.55, cooler=True, corbel=True, front_meter=True, capital=True, post_mat='cream', beam_mat='teal', parapet_mat='stucco', coping_mat='teal', coping_out=0.07, service_mast=True, end_plinth_h=0.20, end_meter=True, quoins=True, roof_vents=True, vent_h=0.75,
    canopy_style='corrugated', sheet_wall_y=3.0, sheet_eave_y=2.72, sheet_eave_z=2.10,
    wave=0.068, wave_amp=0.016, bullnose=0.36, bullnose_drop=0.06, wave_segs=2, canopy_gutter=False, canopy_board=0.22,
)
if len(argv) > 1 and os.path.exists(argv[1]):
    P.update(json.load(open(argv[1])))
SEC = json.load(open(os.path.join(OUT, 'sections.json')))
bpy.ops.wm.read_factory_settings(use_empty=True)
UVS = 1.1

# ---------------------------------------------------------------- materials
MATS = {}
def mat(name, rgb, rough=0.8, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*rgb, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Metallic'].default_value = metal
    MATS[name] = m; return m
# Library-borrowing materials (engine swaps in the kit's tiled PBR maps; the
# colour here is only the Blender/fallback swatch, close to the library mean).
STUCCO = mat('MC15_stucco', (.46, .40, .30))
CONC = mat('MC15_concrete', (.42, .41, .38))
CONC_D = mat('MC15_concrete_dark', (.22, .21, .20))
ROOF = mat('MC15_roof', (.30, .30, .29))
CREAM = mat('MC15_cream', (.62, .58, .48))
TEAL = mat('MC15_teal', (.07, .24, .22), .6, .2)
OCHRE = mat('MC15_ochre', (.55, .30, .06), .6, .2)
MORTAR = mat('ARCH_mortar', (.30, .26, .20), .95)
GALV = mat('ARCH_galv', (.42, .43, .42), .45, .8)
IRON = mat('ARCH_iron', (.03, .03, .03), .6, .5)
GLASS = mat('ARCH_glass', (.10, .12, .12), .12, .0)
SIGNW = mat('ARCH_sign_white', (.78, .76, .70), .5)
SIGNR = mat('ARCH_sign_red', (.50, .05, .04), .45)
METROOF = mat('ARCH_metal_roof', (.17, .045, .035), .5, .4)
SHEET = mat('ARCH_sheet', (.34, .34, .32), .5, .35)

# ---------------------------------------------------------------- geometry
def E(x, y, z): return Vector((x, -z, y))  # engine -> Blender

class Geo:
    """Polygon soup in engine world space: (points, material)."""
    def __init__(self): self.faces = []
    def face(self, pts, m, out=None, centre=None):
        pts = [Vector(p) for p in pts]
        n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
        if n.length < 1e-12 and len(pts) > 3: n = (pts[2] - pts[0]).cross(pts[3] - pts[0])
        if out is None and centre is not None:
            c = sum(pts, Vector()) / len(pts); out = c - Vector(centre)
        if out is not None and n.dot(Vector(out)) < 0: pts = pts[::-1]
        self.faces.append((pts, m))
    def hexa(self, c, m, skip=()):
        """c[i][j][k] corners (i,j,k in 0/1). Faces oriented away from centre."""
        ctr = sum((Vector(c[i][j][k]) for i in (0, 1) for j in (0, 1) for k in (0, 1)), Vector()) / 8
        F = {'-i': [c[0][0][0], c[0][1][0], c[0][1][1], c[0][0][1]], '+i': [c[1][0][0], c[1][1][0], c[1][1][1], c[1][0][1]],
             '-j': [c[0][0][0], c[1][0][0], c[1][0][1], c[0][0][1]], '+j': [c[0][1][0], c[1][1][0], c[1][1][1], c[0][1][1]],
             '-k': [c[0][0][0], c[1][0][0], c[1][1][0], c[0][1][0]], '+k': [c[0][0][1], c[1][0][1], c[1][1][1], c[0][1][1]]}
        for key, pts in F.items():
            if key not in skip: self.face(pts, m, centre=ctr)
    def box(self, lo, hi, m, skip=()):
        (x0, y0, z0), (x1, y1, z1) = lo, hi
        c = [[[(x, y, z) for z in (z0, z1)] for y in (y0, y1)] for x in (x0, x1)]
        self.hexa(c, m, skip)
    def cyl(self, a, b, r, m, seg=10, caps=True):
        a, b = Vector(a), Vector(b); t = (b - a).normalized()
        up = Vector((0, 1, 0)) if abs(t.y) < .9 else Vector((1, 0, 0))
        u = t.cross(up).normalized(); v = t.cross(u).normalized()
        ring = [(math.cos(2 * math.pi * s / seg), math.sin(2 * math.pi * s / seg)) for s in range(seg)]
        A = [a + (u * cs + v * sn) * r for cs, sn in ring]; B = [b + (u * cs + v * sn) * r for cs, sn in ring]
        mid = (a + b) / 2
        for s in range(seg):
            s1 = (s + 1) % seg
            q = [A[s], A[s1], B[s1], B[s]]
            cq = sum(q, Vector()) / 4; axis_pt = a + t * (cq - a).dot(t)
            self.face(q, m, out=cq - axis_pt)
        if caps:
            self.face(A, m, out=-t); self.face(B, m, out=t)
    def tube(self, path, r, m, seg=8):
        for i in range(len(path) - 1):
            self.cyl(path[i], path[i + 1], r, m, seg, caps=True)

class Frame:
    """Wall frame: u along the wall from its min end, v up, d outward from the face."""
    def __init__(self, origin, uaxis, normal):
        self.o, self.u, self.n = Vector(origin), Vector(uaxis), Vector(normal)
    def p(self, u, v, d): return tuple(self.o + self.u * u + Vector((0, v, 0)) + self.n * d)
    def box(self, g, u0, u1, v0, v1, d0, d1, m, skip=()):
        c = [[[self.p(u, v, d) for d in (d0, d1)] for v in (v0, v1)] for u in (u0, u1)]
        g.hexa(c, m, skip)
    def quad(self, g, u0, u1, v0, v1, d, m):
        g.face([self.p(u0, v0, d), self.p(u1, v0, d), self.p(u1, v1, d), self.p(u0, v1, d)], m, out=tuple(self.n))
    def tube(self, g, pts, r, m, seg=8):
        g.tube([self.p(*q) for q in pts], r, m, seg)

def text_faces(g, s, frame, u, v, d, size, m, depth=0.02, align='CENTER'):
    """Blender's built-in font, triangulated, laid on a wall frame."""
    cu = bpy.data.curves.new('t', 'FONT'); cu.body = s; cu.size = size; cu.extrude = depth / 2
    cu.align_x = align; cu.fill_mode = 'BOTH'; cu.resolution_u = 3
    ob = bpy.data.objects.new('t', cu); bpy.context.scene.collection.objects.link(ob)
    dg = bpy.context.evaluated_depsgraph_get(); me = ob.evaluated_get(dg).to_mesh()
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.triangulate(bm, faces=bm.faces)
    # Readable from outside: the text's +x must run along up x normal.
    mx = 1 if Vector((0, 1, 0)).cross(frame.n).dot(frame.u) > 0 else -1
    for f in bm.faces:
        pts = [frame.p(u + mx * l.vert.co.x, v + l.vert.co.y, d + depth / 2 + l.vert.co.z) for l in f.loops]
        nn = f.normal; out = frame.u * (mx * nn.x) + Vector((0, nn.y, 0)) + frame.n * nn.z
        g.face(pts, m, out=tuple(out))
    bm.free(); ob.evaluated_get(dg).to_mesh_clear(); bpy.data.objects.remove(ob); bpy.data.curves.remove(cu)

INST = {i['tag']: i for i in SEC['instances']}
WALL_GEO = {}   # owner tag -> Geo (world space), clipped into section pieces
OBJ_GEO = {}    # owner tag -> (Geo, roof flag)
def wall(tag):
    return WALL_GEO.setdefault(tag, Geo())
def obj(tag, roof=False):
    if tag not in OBJ_GEO: OBJ_GEO[tag] = (Geo(), roof)
    return OBJ_GEO[tag][0]

FACE = 0.09  # wall skin half depth: outer faces at +-0.09
def front(cx): return Frame((cx - 2, 0, FACE), (1, 0, 0), (0, 0, 1))
def rear(cx, z=-6.0): return Frame((cx - 2, 0, z - FACE), (1, 0, 0), (0, 0, -1))
def side(x, z0, out): return Frame((x + out * FACE, 0, z0), (0, 0, 1), (out, 0, 0))

BAYS = (-10, -6, -2, 2, 6)
ROOMS = (-6, -2, 2, 6)
CANOPY = {-10: 5, -6: 12, -2: 36, 2: 60, 6: 84}
ROOFT = {-10: 3, -6: 10, -2: 34, 2: 58, 6: 82}
FACADE = {-6: 13, -2: 37, 2: 61, 6: 85}
REAR = {-6: 14, -2: 38, 2: 62, 6: 86}
POSTS = dict(zip((-12, -8, -4, 0, 4, 8), range(114, 120)))

# ------------------------------------------------ main roof: fascia + coping
fb, ft, fo, cp = P['fascia_bottom'], P['fascia_top'], P['fascia_out'], P['coping']
PAR = {'stucco': STUCCO, 'teal': TEAL, 'cream': CREAM}[P['parapet_mat']]
COPM = {'galv': GALV, 'teal': TEAL}[P['coping_mat']]
for cx in BAYS:
    if cx == -10 and P['office_full']: continue
    g = obj(ROOFT[cx], roof=True); x0, x1 = cx - 2, cx + 2
    xa = x0 - (.15 + fo if cx == -10 else 0); xb = x1 + (.15 + fo if cx == 6 else 0)
    for z, s in ((-6.15, -1), (0.15, 1)):
        g.box((xa, fb, min(z, z + s * fo)), (xb, ft, max(z, z + s * fo)), PAR)               # fascia board
        g.box((xa, ft, min(z - s * .12, z + s * (fo + P['coping_out']))), (xb, ft + cp, max(z - s * .12, z + s * (fo + P['coping_out']))), COPM)  # coping cap
        g.box((xa, ft - .07, min(z + s * fo, z + s * (fo + P['coping_out']))), (xb, ft, max(z + s * fo, z + s * (fo + P['coping_out']))), COPM, skip=('+j',))  # coping skirt
        g.box((xa, 3.02, min(z, z - s * .12)), (xb, ft, max(z, z - s * .12)), STUCCO, skip=('-j',))  # parapet stub
        g.box((xa, fb - .03, min(z + s * fo, z + s * (fo + .015))), (xb, fb, max(z + s * fo, z + s * (fo + .015))), GALV)  # drip edge
    for x, s in (((-12.15, -1),) if cx == -10 else ()) + (((8.15, 1),) if cx == 6 else ()):
        g.box((min(x, x + s * fo), fb, -6.15), (max(x, x + s * fo), ft, .15), PAR)
        g.box((min(x - s * .12, x + s * (fo + P['coping_out'])), ft, -6.15 - fo - P['coping_out']), (max(x - s * .12, x + s * (fo + P['coping_out'])), ft + cp, .15 + fo + P['coping_out']), COPM)
        g.box((min(x + s * fo, x + s * (fo + P['coping_out'])), ft - .07, -6.15 - fo - P['coping_out']), (max(x + s * fo, x + s * (fo + P['coping_out'])), ft, .15 + fo + P['coping_out']), COPM, skip=('+j',))
        g.box((min(x, x - s * .12), 3.02, -6.15), (max(x, x - s * .12), ft, .15), STUCCO, skip=('-j',))
    # Rolled-roofing laps every 0.9 m and a patch or two.
    for k in range(1, 7):
        z = -6.0 + k * .9
        g.box((x0 + .02, 3.02, z), (x1 - .02, 3.026, z + .05), CONC_D, skip=('-j',))
    if cx in (-2, 6):
        g.box((cx - 1.4, 3.02, -1.9), (cx - .3, 3.03, -1.1), CONC_D, skip=('-j',))

# Scuppers + conductor heads through the rear parapet; downspouts go on walls.
def scupper(g, x, z):
    g.box((x - .1, fb + .02, z - .14), (x + .1, fb + .12, z), GALV)                 # scupper box
    g.box((x - .13, fb - .26, z - .26), (x + .13, fb + .02, z - .06), GALV)          # conductor head
for x, cx in ((-7.62, -6), (7.62, 6)):
    scupper(obj(ROOFT[cx], True), x, -6.15 - fo)
# Rectangular downspouts from the conductor heads (wall mounted, sectioned).
def downspout(g, F, u, top, d=.045):
    F.box(g, u - .04, u + .04, .30, top, d - .03, d + .03, GALV)
    F.tube(g, [(u, .32, d), (u, .12, d + .16), (u, .08, d + .30)], .035, GALV, 4)
    for v in (top - .3, (top + .3) / 2, .6): F.box(g, u - .05, u + .05, v - .015, v + .015, 0, d + .035, GALV)

# ------------------------------------------------ roof equipment
def cooler(g, x, z, yaw=0.0):
    """Down-draft evaporative (swamp) cooler on a steel stand, duct to roof."""
    y0 = 3.02; w, dpt, h = .92, .92, .86; st = .28
    for sx in (-1, 1):
        for sz in (-1, 1):
            g.box((x + sx * .40 - .025, y0, z + sz * .40 - .025), (x + sx * .40 + .025, y0 + st, z + sz * .40 + .025), GALV)
    g.box((x - .5, y0 + st, z - .5), (x + .5, y0 + st + .04, z + .5), GALV)
    yb = y0 + st + .04
    g.box((x - w / 2, yb, z - dpt / 2), (x + w / 2, yb + h, z + dpt / 2), CREAM)
    g.box((x - w / 2 - .03, yb + h, z - dpt / 2 - .03), (x + w / 2 + .03, yb + h + .05, z + dpt / 2 + .03), GALV)
    for i in range(7):  # louvres on three faces
        yy = yb + .1 + i * .1
        g.box((x - w / 2 + .06, yy, z + dpt / 2), (x + w / 2 - .06, yy + .035, z + dpt / 2 + .03), GALV)
        g.box((x - w / 2 - .03, yy, z - dpt / 2 + .06), (x - w / 2, yy + .035, z + dpt / 2 - .06), GALV)
        g.box((x + w / 2, yy, z - dpt / 2 + .06), (x + w / 2 + .03, yy + .035, z + dpt / 2 - .06), GALV)
    g.box((x - .28, y0, z - .28), (x + .28, y0 + st + .01, z + .28), GALV, skip=('-j',))  # duct drop
    g.cyl((x + .5, yb + .1, z - .3), (x + .9, y0 + .02, z - .3), .008, GALV, 6)     # water line
if P['cooler']:
    for cx in ROOMS:
        cooler(obj(ROOFT[cx], True), cx + .7, -2.0)
    if not P['office_full']: cooler(obj(ROOFT[-10], True), -10.6, -3.4)
# Per-room plumbing vent stacks (bath + basin), exhaust-fan cap, J-vent.
if P['roof_vents']:
    for cx in ROOMS:
        g = obj(ROOFT[cx], True)
        for x, z, h in ((cx - .95, -5.35, P['vent_h']), (cx + .15, -5.55, P['vent_h'] * .8)):
            g.cyl((x, 3.02, z), (x, 3.06, z), .10, GALV, 10)                       # flashing boot
            g.cyl((x, 3.02, z), (x, 3.02 + h, z), .055, GALV, 10)
        x, z = cx + 1.25, -4.9                                                        # exhaust-fan cap
        g.box((x - .2, 3.02, z - .2), (x + .2, 3.12, z + .2), GALV)
        g.box((x - .13, 3.12, z - .13), (x + .13, 3.30, z + .13), GALV)
        g.box((x - .19, 3.30, z - .19), (x + .19, 3.34, z + .19), GALV)
        x, z = cx - .2, -1.0                                                          # J-vent
        g.cyl((x, 3.02, z), (x, 3.32, z), .05, GALV, 8)
        g.tube([(x, 3.32, z), (x, 3.40, z + .08), (x, 3.34, z + .16)], .05, GALV, 8)

# TV antenna mast on two bays.
for cx in (-6, 2):
    g = obj(ROOFT[cx], True); x, z = cx - 1.3, -5.3
    g.cyl((x, 3.02, z), (x, 5.0, z), .02, GALV, 6)
    for k, (w, y) in enumerate(((1.1, 4.9), (.85, 4.7), (.6, 4.5))):
        g.cyl((x - w / 2, y, z), (x + w / 2, y, z), .008, GALV, 5)
    g.cyl((x, 4.0, z), (x - .9, 3.02, z + .9), .004, IRON, 4)

# Front K-style gutter along the canopy fascia top, downspouts at the end posts.
def kgutter(g, x0, x1, ytop, z):
    prof = [(0, 0), (0, -.11), (.08, -.11), (.10, -.10), (.115, -.075), (.12, -.05), (.13, -.035), (.135, -.012), (.14, 0)]
    for i in range(len(prof) - 1):
        (za, ya), (zb, yb) = prof[i], prof[i + 1]
        g.face([(x0, ytop + ya, z + za), (x1, ytop + ya, z + za), (x1, ytop + yb, z + zb), (x0, ytop + yb, z + zb)], OCHRE, out=(0, 0, 1) if za > .05 else (0, -1, 0))
        g.face([(x0, ytop + ya, z + za), (x1, ytop + ya, z + za), (x1, ytop + yb, z + zb), (x0, ytop + yb, z + zb)], OCHRE, out=(0, 0, -1) if za > .05 else (0, 1, 0))
    for x in (x0, x1):
        g.face([(x, ytop, z), (x, ytop - .11, z), (x, ytop - .11, z + .08), (x, ytop, z + .14)], OCHRE, out=(1 if x == x1 else -1, 0, 0))
        g.face([(x, ytop, z), (x, ytop - .11, z), (x, ytop - .11, z + .08), (x, ytop, z + .14)], OCHRE, out=(-1 if x == x1 else 1, 0, 0))

def corrugated(g, xa, xb, cx):
    """Sloped corrugated sheet from the wall to the eave, bent down into a
    bull-nose at the eave, over the flat deck; end closures."""
    yw, ye, ze, zw = P['sheet_wall_y'], P['sheet_eave_y'], P['sheet_eave_z'], FACE + .02
    R = P['bullnose']
    prof = [Vector((zw, yw))]
    d = Vector((ze - zw, ye - yw)).normalized()
    a0 = math.atan2(d.y, d.x)
    end = Vector((ze, ye)) - d * (R * math.tan((a0 + math.pi / 2) / 2) if R > 0 else 0)
    prof.append(end)
    if R > 0:  # arc from the slope direction to straight down
        ctr = end + Vector((-d.y, d.x)) * -R
        n0 = (end - ctr).normalized(); a_start = math.atan2(n0.y, n0.x)
        steps = 4
        for k in range(1, steps + 1):
            ang = a_start - (a_start - 0.0) * k / steps
            prof.append(ctr + Vector((math.cos(ang), math.sin(ang))) * R)
        prof.append(prof[-1] + Vector((0, -P['bullnose_drop'])))
    nseg = int(round((xb - xa) / P['wave'] * P['wave_segs'])); amp = P['wave_amp']
    norms = []
    for i in range(len(prof)):
        t = (prof[min(i + 1, len(prof) - 1)] - prof[max(i - 1, 0)]).normalized()
        norms.append(Vector((-t.y, t.x)))
    def pt(x, i, off):
        k = (x - xa) / P['wave'] * 2 * math.pi
        q = prof[i] + norms[i] * (amp * math.cos(k) + off)
        return Vector((x, q.y, q.x))
    for i in range(nseg):
        xs, xt = xa + (xb - xa) * i / nseg, xa + (xb - xa) * (i + 1) / nseg
        for j in range(len(prof) - 1):
            nn = norms[j] + norms[j + 1]; out = Vector((0, nn.y, nn.x))
            g.face([pt(xs, j, 0), pt(xt, j, 0), pt(xt, j + 1, 0), pt(xs, j + 1, 0)], SHEET, out=tuple(out))
            g.face([pt(xs, j, -.004), pt(xt, j, -.004), pt(xt, j + 1, -.004), pt(xs, j + 1, -.004)], SHEET, out=tuple(-out))
        L = len(prof) - 1; tn = prof[L] - prof[L - 1]
        g.face([pt(xs, L, 0), pt(xt, L, 0), pt(xt, L, -.004), pt(xs, L, -.004)], SHEET, out=(0, tn.y, tn.x))
    ye = prof[-1].y + .02
    if R <= 0:
        fb_ = ye - P['canopy_board']
        g.box((xa, fb_, cz - .04), (xb, ye - .01, cz), TEAL)                           # eave board
        g.box((xa, fb_, cz - .005), (xb, fb_ + .03, cz + .012), CREAM)                # drip trim
    g.box((xa, 2.63, cz - .12), (xb, min(ye, 2.63 + .08), cz - .04), TEAL, skip=('-j',))
    for x, s_ in ((xa, -1), (xb, 1)):
        if (s_ < 0 and cx != (-6 if P['office_full'] else -10)) or (s_ > 0 and cx != 6): continue
        poly = [(x, 2.63, zw)] + [(x, q.y - .005, q.x) for q in prof] + [(x, 2.63, prof[-1].x)]
        g.face(poly, CREAM, out=(s_, 0, 0))

# ------------------------------------------------ canopy: fascia, rafters, beam
cfb, cft, cz = P['canopy_fascia_bottom'], P['canopy_fascia_top'], P['canopy_fascia_z']
for cx in BAYS:
    g = obj(CANOPY[cx], roof=True); x0, x1 = cx - 2, cx + 2
    xa = x0 - (.08 if cx == -10 else 0); xb = x1 + (.08 if cx == 6 else 0)
    if cx == -10 and P['office_full']:
        pass  # the office roof covers this bay's walkway
    elif P['canopy_style'] == 'corrugated':
        corrugated(g, xa, xb, cx)
    else:
        g.box((xa, cfb, cz - .04), (xb, cft, cz), TEAL)                               # deep fascia
        g.box((xa, cft, cz - .12), (xb, cft + .025, cz + .015), GALV)                  # cap flashing
        g.box((xa, cfb, cz - .005), (xb, cfb + .035, cz + .012), CREAM)                # drip trim
        g.box((xa, 2.63, .05), (xb, cft, cz - .04), ROOF, skip=('-j',))               # deck build-up to fascia top
        g.box((xa, 2.63, cz - .12), (xb, cft, cz - .04), TEAL, skip=('-j',))
    # Exposed rafters under the deck soffit, ledger at the wall, beam on posts.
    n = int(round(4 / P['rafter_step']))
    for i in range(n + (1 if cx == 6 else 0)) if P['rafter_step'] > 0 else ():
        x = x0 + i * 4 / n
        if cx == -10 and i == 0: x += P['rafter_w']
        g.box((x - P['rafter_w'] / 2, 2.44 - P['rafter_h'], FACE), (x + P['rafter_w'] / 2, 2.44, cz - .04), CREAM, skip=('+j',))
    g.box((x0, 2.44 - P['rafter_h'] - .04, FACE), (x1, 2.44, FACE + .06), CREAM, skip=('+j',))
    bz = 1.77; bh, bw = P['beam_h'], P['beam_w']
    g.box((xa, 2.44 - bh, bz - bw / 2), (xb, 2.44, bz + bw / 2), {'teal': TEAL, 'cream': CREAM}[P['beam_mat']], skip=('+j',))
    g.box((xa, 2.44 - P['rafter_h'], cz - .10), (xb, 2.44, cz - .04), TEAL, skip=('+j',))  # rim joist
    if P['canopy_gutter']: kgutter(g, xa, xb, cft - .02, cz)
for x, tag in ((-12, 114), (8, 119)) if P['canopy_gutter'] else ():  # downspouts down the end posts
    g = obj(tag); z = cz + .07; top = P['canopy_fascia_top'] - .13
    g.box((x - .04, .30, z - .03), (x + .04, top, z + .03), OCHRE)
    g.cyl((x, .32, z), (x, .10, z + .2), .033, OCHRE, 4)
    g.box((x - .14, -.08, z + .1), (x + .14, -.05, z + .5), CONC)

# ------------------------------------------------ posts: sleeve, cap, knee braces
POSTM = {'teal': TEAL, 'cream': CREAM}[P['post_mat']]
pw = P['post_w'] / 2; ybeam = 2.44 - P['beam_h']
for x, tag in POSTS.items():
    g = obj(tag); z = 1.77
    g.box((x - pw, .02, z - pw), (x + pw, ybeam, z + pw), POSTM, skip=('+j', '-j'))
    g.box((x - pw - .025, 0, z - pw - .025), (x + pw + .025, .14, z + pw + .025), CONC)   # concrete pier collar
    g.box((x - pw - .015, ybeam - .03, z - pw - .015), (x + pw + .015, ybeam, z + pw + .015), POSTM)  # cap plate
    if P['capital']:
        g.box((x - pw - .035, ybeam - .13, z - pw - .035), (x + pw + .035, ybeam - .06, z + pw + .035), POSTM)
        g.box((x - pw - .02, ybeam - .06, z - pw - .02), (x + pw + .02, ybeam - .03, z + pw + .02), POSTM)
        g.box((x - pw - .02, .14, z - pw - .02), (x + pw + .02, .22, z + pw + .02), POSTM)   # base fillet
    b = P['brace']
    for s in (-1, 1):
        if (x == -12 and s < 0) or (x == 8 and s > 0): continue
        if P['corbel']:
            # Curved corbel bracket (quarter-round cut-out), 0.08 m thick.
            # (h, dv): h out from the post face, dv down from the beam soffit
            pts = [(0, 0), (b, 0), (b * .55, .06), (b * .25, b * .25), (.06, b * .55), (0, b)]
            hw = .04
            for zz in (z - hw, z + hw):
                g.face([(x + s * (pw + h), ybeam - dv, zz) for h, dv in pts], POSTM, out=(0, 0, zz - z))
            for k in range(len(pts)):
                (h0, d0), (h1, d1) = pts[k], pts[(k + 1) % len(pts)]
                q = [(x + s * (pw + h0), ybeam - d0, z - hw), (x + s * (pw + h1), ybeam - d1, z - hw),
                     (x + s * (pw + h1), ybeam - d1, z + hw), (x + s * (pw + h0), ybeam - d0, z + hw)]
                mid = Vector(((h0 + h1) / 2, -(d0 + d1) / 2)); ctr = Vector((b * .3, -b * .3))
                o2 = mid - ctr
                g.face(q, POSTM, out=(s * o2.x, o2.y, 0))
        else:
            a = (x + s * pw, ybeam - b, z); c = (x + s * (pw + b), ybeam, z)
            g.cyl(a, c, .022, POSTM, 4)

# ------------------------------------------------ front facades (sectioned)
ph, pd = P['plinth_h'], P['plinth_d']
for cx, tag in FACADE.items():
    g = wall(tag); F = front(cx)
    door0, door1 = .26, 1.34
    win0, win1 = 2.8 - 1.035, 2.8 + 1.035
    ac0, ac1 = 2.8 - .56, 2.8 + .56
    cw, cd = P['casing_w'], P['casing_d']
    # plinth (skips door and AC sleeve)
    for a, b in ((0, door0 - cw), (door1 + cw, ac0), (ac1, 4.0)):
        F.box(g, a, b, 0, ph, 0, pd, CONC_D)
        F.box(g, a, b, ph, ph + .02, 0, pd * .6, CONC_D, skip=('-j',))
    # door casing + head
    F.box(g, door0 - cw, door0, 0, 2.19 + cw + .03, 0, cd, CREAM)
    F.box(g, door1, door1 + cw, 0, 2.19 + cw + .03, 0, cd, CREAM)
    F.box(g, door0 - cw - .02, door1 + cw + .02, 2.19, 2.19 + cw + .03, 0, cd + .015, CREAM)
    # window surround: jambs, precast lintel
    F.box(g, win0 - .09, win0, .875, 2.19, 0, .05, CREAM)
    F.box(g, win1, win1 + .09, .875, 2.19, 0, .05, CREAM)
    F.box(g, win0 - .14, win1 + .14, 2.18, 2.18 + P['lintel_h'], 0, .06, CONC)
    # AC sleeve collar + angle brackets
    F.box(g, ac0, ac1, .12, .16, 0, .05, GALV); F.box(g, ac0, ac1, .76, .80, 0, .05, GALV)
    F.box(g, ac0, ac0 + .04, .12, .80, 0, .05, GALV); F.box(g, ac1 - .04, ac1, .12, .80, 0, .05, GALV)
    for u in (ac0 + .12, ac1 - .12):
        F.box(g, u - .02, u + .02, .02, .14, 0, .22, IRON)
    # per-room electric meter (socket meter + base) with conduit to the soffit
    if P['front_meter']:
        mu = 1.565
        F.box(g, mu - .09, mu + .09, 1.20, 1.52, 0, .09, GALV)
        F.box(g, mu - .07, mu + .07, 1.29, 1.43, .09, .10, GALV)
        F.tube(g, [(mu, 1.36, .10), (mu, 1.36, .16)], .065, GLASS, 10)
        F.tube(g, [(mu, 1.52, .045), (mu, 2.43, .045)], .014, GALV, 6)
        F.box(g, mu - .07, mu + .07, .95, 1.12, 0, .08, IRON)              # disconnect
        F.tube(g, [(mu, 1.12, .04), (mu, 1.20, .04)], .012, GALV, 6)
    # pilasters at the party-wall lines
    F.box(g, 0, P['pilaster_w'] / 2, ph, 2.80, 0, P['pilaster_d'], STUCCO)
    F.box(g, 4 - P['pilaster_w'] / 2, 4, ph, 2.80, 0, P['pilaster_d'], STUCCO)

# ------------------------------------------------ block coursing (rear, annex)
def coursing(g, F, length, v0=.15, v1=2.80, openings=()):
    j, c, un, dd = P['joint'], P['course'], P['unit'], P['joint_d']
    rows = int((v1 - v0) / c)
    def blocked(u, v): return any(a <= u <= b and lo <= v <= hi for a, b, lo, hi in openings)
    for r in range(rows + 1):
        v = v0 + r * c
        if v + j / 2 > v1: break
        segs, a = [], 0.0
        # horizontal bed joint, split around openings
        cuts = sorted([(a2, b2) for a2, b2, lo, hi in openings if lo <= v <= hi])
        for a2, b2 in cuts:
            if a2 > a: segs.append((a, a2))
            a = max(a, b2)
        if a < length: segs.append((a, length))
        for a2, b2 in segs: F.quad(g, a2, b2, v - j / 2, v + j / 2, dd, MORTAR)
        if r == rows: break
        off = (r % 2) * un / 2; u = off
        while u < length - .01:
            if u > .01 and not blocked(u, v + c / 2):
                F.quad(g, u - j / 2, u + j / 2, v + j / 2, min(v + c - j / 2, v1), dd, MORTAR)
            u += un

for cx, tag in REAR.items():
    g = wall(tag); F = rear(cx)
    # Rear frame u runs +x from cx-2 like the front; the bathroom is at cx-1..cx+2.
    win = (1.30, 1.90, 1.70, 2.20)
    coursing(g, F, 4.0, openings=(win,))
    a, b, lo, hi = win
    F.box(g, a - .05, b + .05, lo - .07, lo, 0, .07, CONC)                      # sill
    F.box(g, a - .04, a, lo, hi, 0, .035, GALV); F.box(g, b, b + .04, lo, hi, 0, .035, GALV)
    F.box(g, a - .04, b + .04, hi, hi + .04, 0, .035, GALV); F.box(g, a, b, lo, lo + .03, 0, .035, GALV)
    F.box(g, a, b, lo + .03, hi, 0, .012, GLASS, skip=('-k',))
    F.box(g, (a + b) / 2 - .012, (a + b) / 2 + .012, lo + .03, hi, 0, .03, GALV)   # mullion
    # plumbing vent + hose bib + gas stub, horizontal conduit under the eave
    F.tube(g, [(.55, .18, .05), (.55, 2.75, .05), (.55, 2.95, .05)], .028, GALV)
    F.box(g, .47, .63, .18, .26, 0, .09, GALV)                                   # cleanout tee
    F.box(g, 2.55, 2.65, .42, .52, 0, .04, GALV); F.tube(g, [(2.6, .47, .04), (2.6, .47, .13), (2.6, .40, .15)], .012, GALV)
    F.tube(g, [(0.05, 2.58, .03), (3.95, 2.58, .03)], .011, GALV, 6)

downspout(wall(14), rear(-6), .38, fb - .26 + fo)
downspout(wall(86), rear(6), 3.62, fb - .26 + fo)

# Electric service mast (riser + weatherhead) above the existing rear meter on
# 38, and a gas meter set with risers on 62.
if P['service_mast']:
    F = rear(-2); g = wall(38); mu = -2.7 - (-4.0)
    F.tube(g, [(mu, 1.55, .05), (mu, 3.75, .05)], .03, GALV, 8)
    F.tube(g, [(mu, 3.75, .05), (mu, 3.86, .14)], .036, IRON, 8)
    for v in (2.0, 2.6): F.box(g, mu - .06, mu + .06, v - .02, v + .02, 0, .085, GALV)
    F = rear(2); g = wall(62); gu = 1.0
    F.tube(g, [(gu - .18, 0, .08), (gu - .18, .55, .08), (gu - .05, .55, .08)], .022, GALV, 6)
    F.box(g, gu - .05, gu + .17, .40, .72, .02, .26, GALV)                               # meter body
    F.tube(g, [(gu + .06, .72, .14), (gu + .06, .86, .14), (gu + .06, .86, .02)], .022, GALV, 6)
    F.box(g, gu - .23, gu - .13, .25, .33, .02, .14, IRON)                              # regulator

# ------------------------------------------------ end walls (sectioned)
def plinth(g, F, a, b):
    F.box(g, a, b, 0, ph, 0, pd, CONC_D); F.box(g, a, b, ph, ph + .02, 0, pd * .6, CONC_D, skip=('-j',))
# 104: office west end (vending nook in front); 109: east end facing the sign.
for tag, F in ((104, side(-12, -6, -1)), (109, side(8, -6, 1))):
    g = wall(tag)
    F.box(g, .02, 5.98, 0, P['end_plinth_h'], 0, pd, CONC); F.box(g, .02, 5.98, P['end_plinth_h'], P['end_plinth_h'] + .02, 0, pd * .6, CONC, skip=('-j',))
    F.box(g, 2.6, 3.4, 2.20, 2.60, 0, .04, GALV)                                  # attic/louvre vent
    for i in range(6): F.box(g, 2.64, 3.36, 2.24 + i * .06, 2.27 + i * .06, 0, .06, GALV)
    for u in (1.5, 4.5): F.quad(g, u - .006, u + .006, ph + .02, 2.78, .002, MORTAR)  # control joints
    F.tube(g, [(5.2, .9, .03), (5.2, 2.75, .03)], .012, GALV, 6)                  # conduit to the roof
    F.box(g, 5.08, 5.32, .70, .95, 0, .09, IRON)                                  # disconnect box
    if P['quoins']:
        k = 0; v = P['end_plinth_h'] + .03
        while v + .24 < 2.72:
            L = .42 if k % 2 == 0 else .28
            for a, b in ((0.0, L), (6.0 - L, 6.0)):
                F.box(g, a + .01, b - .01, v, v + .23, 0, .028, CREAM)
            v += .26; k += 1
    if P['end_meter'] and tag == 109:
        F.box(g, 4.62, 4.84, 1.15, 1.50, 0, .09, GALV)                               # meter base
        F.tube(g, [(4.73, 1.33, .09), (4.73, 1.33, .16)], .07, GLASS, 10)
        F.tube(g, [(4.73, 1.15, .04), (4.73, .75, .04), (5.08, .75, .04)], .014, GALV, 6)
        # jelly-jar lamp near the front corner
        F.box(g, 5.45, 5.57, 2.05, 2.25, 0, .015, IRON)
        F.box(g, 5.48, 5.54, 2.12, 2.18, .015, .07, IRON)
        F.tube(g, [(5.51, 2.03, .12), (5.51, 2.17, .12)], .045, GLASS, 10)
        F.tube(g, [(5.51, 2.17, .12), (5.51, 2.20, .12)], .052, IRON, 10)
        F.box(g, 2.9, 3.1, .35, .45, 0, .04, GALV); F.tube(g, [(3.0, .40, .04), (3.0, .40, .12), (3.0, .33, .14)], .012, GALV, 6)

# Laundry annex: block building with its own parapet, door awning, sign, vents.
g129 = wall(129); F = rear(-10, -10.0)
coursing(g129, F, 4.0)
for u in (.9, 1.6, 2.3):  # dryer exhaust hoods
    F.box(g129, u - .14, u + .14, 1.05, 1.33, 0, .015, GALV)
    F.box(g129, u - .12, u + .12, 1.08, 1.30, .015, .13, GALV)
    F.box(g129, u - .11, u + .11, 1.075, 1.08, .02, .13, IRON)
F.tube(g129, [(3.4, .18, .05), (3.4, 2.95, .05)], .028, GALV)
g130 = wall(130); F = side(-12, -10, -1); coursing(g130, F, 4.0)
g131 = wall(131); F = side(-8, -10, 1)
coursing(g131, F, 4.0, openings=((1.30, 2.70, 0, 2.30),))
F.box(g131, 1.2, 2.8, 2.36, 2.44, 0, .85, GALV)                                     # door awning
F.box(g131, 1.2, 2.8, 2.30, 2.36, .80, .85, TEAL)
for u in (1.3, 2.7): F.tube(g131, [(u, 1.75, .02), (u, 2.33, .80)], .012, IRON, 5)
F.box(g131, 1.25, 2.75, 2.50, 2.76, 0, .03, SIGNW)
text_faces(g131, 'LAUNDRY', F, 2.0, 2.55, .03, .17, SIGNR, depth=.012)

# ------------------------------------------------ annex roof parapet (own block)
g = obj(128, roof=True); ah = P['annex_parapet']
for lo, hi in (((-12.18, 2.78, -10.13), (-7.82, 3.02 + ah, -10.05)), ((-12.18, 2.78, -10.13), (-12.10, 3.02 + ah, -6.32)),
               ((-7.90, 2.78, -10.13), (-7.82, 3.02 + ah, -6.32))):
    g.box(lo, hi, CREAM)
for lo, hi in (((-12.21, 3.02 + ah, -10.16), (-7.79, 3.05 + ah, -10.0)), ((-12.21, 3.02 + ah, -10.16), (-12.05, 3.05 + ah, -6.32)),
               ((-7.95, 3.02 + ah, -10.16), (-7.79, 3.05 + ah, -6.32))):
    g.box(lo, hi, GALV)
g.cyl((-9.2, 3.02, -8.6), (-9.2, 3.9, -8.6), .06, GALV, 10)                         # water heater flue
g.cyl((-9.2, 3.9, -8.6), (-9.2, 3.98, -8.6), .12, GALV, 10)
g.box((-11.0, 3.02, -9.3), (-10.4, 3.25, -8.7), GALV)                               # dryer roof jack

# ------------------------------------------------ office roof (roof 3)
g = obj(3, roof=True)
if P['office_full']:
    x0, x1 = -12.30, -8.0; xr = (x0 + x1) / 2
    ye, yr, zf, zb = P['office_eave'], P['office_ridge'], P['office_front'], -6.30
else:
    x0, x1, xr = -12.25, -7.75, -10.0
    ye, yr, zf, zb = P['gable_eave'], P['gable_ridge'], P['gable_front'], P['gable_back']
th = .10; FASC = {'teal': TEAL, 'red': METROOF}[P['office_fascia']]
def roof_y(x): return ye + (yr - ye) * (1 - abs(x - xr) / (xr - x0))
for s, xe in ((-1, x0), (1, x1)):
    c = [[[(xe, ye + dy, z) for z in (zb, zf)] for dy in (0, th)], [[(xr, yr + dy, z) for z in (zb, zf)] for dy in (0, th)]]
    g.hexa(c, METROOF)
    fd = P['office_fascia_h']
    g.box((min(xe, xe - s * .05), ye - fd, zb), (max(xe, xe - s * .05), ye + th + .01, zf), FASC)  # eave fascia
    # standing seams every 0.5 m (down the slope)
    k = 0
    while zb + .25 + k * .5 < zf - .1:
        z = zb + .25 + k * .5; k += 1
        c = [[[(xe, ye + th + dy, zz) for zz in (z - .012, z + .012)] for dy in (0, .03)], [[(xr, yr + th + dy, zz) for zz in (z - .012, z + .012)] for dy in (0, .03)]]
        g.hexa(c, METROOF, skip=('-j',))
g.box((xr - .06, yr + th - .01, zb), (xr + .06, yr + th + .05, zf), METROOF)                 # ridge cap
# gable infill (front, set back 0.25) and back
for z, mm, o in ((zf - .25, CREAM, 1), (zb + .05, STUCCO, -1)):
    g.face([(x0 + .05, ye, z), (x1 - .05, ye, z), (xr, yr - .02, z)], mm, out=(0, 0, o))
    g.face([(x0 + .05, ye, z), (x1 - .05, ye, z), (xr, yr - .02, z)], mm, out=(0, 0, -o))
if P['office_full']:  # close the soffit line between the eave and the main roof/canopy
    g.box((x0 + .05, 2.63, zf - .26), (x1 - .05, ye, zf - .24), CREAM)
for i in range(1, 15):
    x = x0 + .05 + i * (x1 - x0 - .1) / 15; yt = roof_y(x) - .06
    g.box((x - .02, ye, zf - .25), (x + .02, yt, zf - .22), CREAM)
for s, xe in ((-1, x0), (1, x1)):  # rake boards
    c = [[[(xe, ye + dy, z) for z in (zf - .04, zf + .02)] for dy in (-P['office_fascia_h'] * .6, th + .03)], [[(xr, yr + dy, z) for z in (zf - .04, zf + .02)] for dy in (-P['office_fascia_h'] * .6, th + .03)]]
    g.hexa(c, FASC)
if not P['office_full']:
    g.box((x0 + .3, 2.63, zb), (x1 - .3, ye, zb + .06), STUCCO)                      # back closure above roof
Fo = Frame((xr, 0, zf - .22), (1, 0, 0), (0, 0, 1))
sy = P['office_sign_y']
Fo.box(g, -.85, .85, sy, sy + .30, 0, .03, SIGNW)
text_faces(g, 'OFFICE', Fo, 0, sy + .05, .03, .22, SIGNR, depth=.02)
if P['office_tie']:  # horizontal fascia band across the gable front at eave level
    g.box((x0, ye - P['office_tie_h'], zf - .06), (x1, ye + .02, zf), FASC)
if P['office_trim']:  # painted trim around the lobby glazing (owned by the office walkway slab, tag 4: the lobby glass draws on its own path)
    g6 = obj(4); gx0, gx1, gy0, gy1, gz = -10.615, -8.075, .075, 2.365, .10
    w = .09
    g6.box((gx0 - w, gy0 - w, gz), (gx0, gy1 + w, gz + .035), FASC)
    g6.box((gx1, gy0 - w, gz), (gx1 + w, gy1 + w, gz + .035), FASC)
    g6.box((gx0 - w, gy1, gz), (gx1 + w, gy1 + w, gz + .035), FASC)
    g6.box((gx0 - w, .0, gz), (gx1 + w, gy0, gz + .05), CONC_D)

# ------------------------------------------------ section clipping
SECTIONS = {}
for s in SEC['sections']:
    SECTIONS.setdefault(s['owner'], []).append(s)

def section_prism(s):
    c = Vector(s['center']); yaw = s['yaw']; cs, sn = math.cos(yaw), math.sin(yaw)
    pts = [c + Vector((sn * z, y, cs * z)) for y, z in s['corners']]
    n = Vector((cs, 0, -sn))  # local +x (depth axis) in world
    ctr = sum(pts, Vector()) / 4
    planes = []
    for k in range(4):
        a, b = pts[k], pts[(k + 1) % 4]
        pn = (b - a).cross(n).normalized()
        if pn.dot(ctr - a) < 0: pn = -pn
        planes.append((a, pn))
    return planes

def clip(poly, planes):
    for a, pn in planes:
        out = []
        for i in range(len(poly)):
            p, q = poly[i], poly[(i + 1) % len(poly)]
            dp, dq = (p - a).dot(pn), (q - a).dot(pn)
            if dp >= -1e-7: out.append(p)
            if (dp >= -1e-7) != (dq >= -1e-7):
                t = dp / (dp - dq); out.append(p + (q - p) * t)
        poly = out
        if len(poly) < 3: return []
    return poly

def area(poly):
    s = Vector()
    for i in range(1, len(poly) - 1): s += (poly[i] - poly[0]).cross(poly[i + 1] - poly[0])
    return s.length / 2

PIECES = []    # (name, faces(world), origin)
PLACE = []
lost = 0
for tag, g in sorted(WALL_GEO.items()):
    secs = SECTIONS[tag]; prisms = [section_prism(s) for s in secs]
    buckets = [[] for _ in secs]
    for pts, m in g.faces:
        a0 = area(pts); got = 0.0
        for k, pl in enumerate(prisms):
            cpoly = clip(pts, pl)
            if len(cpoly) >= 3:
                ar = area(cpoly)
                if ar > 1e-9: buckets[k].append((cpoly, m)); got += ar
        if got < a0 * .98:
            c = sum(pts, Vector()) / len(pts); lost += 1
            k = min(range(len(secs)), key=lambda i: (Vector(secs[i]['center']) - c).length)
            if got < 1e-9: buckets[k].append((pts, m))
            else:  # keep the uncovered remainder whole on the nearest section
                buckets[k].append((pts, m))
    for k, (s, faces) in enumerate(zip(secs, buckets)):
        if not faces: continue
        name = f'A_w{tag}_{k:02d}'
        PIECES.append((name, faces, Vector(s['center'])))
        PLACE.append({'piece': name, 'owner': tag, 'origin': [round(v, 4) for v in s['center']], 'yaw': 0.0,
                      'mount': 'section', 'roof': False})
for tag, (g, roof) in sorted(OBJ_GEO.items()):
    o = Vector(INST[tag]['origin'])
    name = f'A_o{tag}'
    PIECES.append((name, g.faces, o))
    PLACE.append({'piece': name, 'owner': tag, 'origin': [round(v, 4) for v in o], 'yaw': 0.0, 'mount': 'object', 'roof': roof})

# ------------------------------------------------ export
order = list(MATS.values())
objs = []; tris = 0
for name, faces, origin in PIECES:
    bm = bmesh.new(); uv = bm.loops.layers.uv.new('UVMap'); used = []
    for pts, m in faces:
        if m not in used: used.append(m)
    used.sort(key=order.index)
    for pts, m in faces:
        vs = [bm.verts.new(E(*(p - origin))) for p in pts]
        try: f = bm.faces.new(vs)
        except ValueError: continue
        f.material_index = used.index(m)
        n = (pts[1] - pts[0]).cross(pts[2] - pts[0]); ax = max(range(3), key=lambda i: abs(n[i]))
        for l, p in zip(f.loops, pts):
            u, v = ((p.z, p.y), (p.x, p.z), (p.x, p.y))[ax]
            l[uv].uv = (u * UVS, v * UVS)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    tris += len(bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    for m in used: me.materials.append(m)
    for p in me.polygons: p.use_smooth = False
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob); objs.append(ob)
# Every material appears at least once: a swatch piece keeps the material
# table fixed (the engine checks the count) whatever the content.
bm = bmesh.new(); me = bpy.data.meshes.new('L_arch_swatch')
for i, m in enumerate(order):
    vs = [bm.verts.new(E(i * .1 + a, b, 0)) for a, b in ((0, 0), (.1, 0), (.1, .1), (0, .1))]
    f = bm.faces.new(vs); f.material_index = i
bmesh.ops.triangulate(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
for m in order: me.materials.append(m)
ob = bpy.data.objects.new('L_arch_swatch', me); bpy.context.scene.collection.objects.link(ob)
path = os.path.join(OUT, 'architecture_kit.glb')
bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', export_yup=True, export_apply=False,
                          export_normals=True, export_tangents=False, export_materials='EXPORT',
                          export_cameras=False, export_lights=False, export_extras=False, use_selection=False)
SUPPRESS = ['P_gutter_ochre', 'P_gutter_cap_ochre', 'P_downspout_front', 'P_gutter_galv', 'P_gutter_cap_galv', 'P_downspout_rear', 'P_post_base', 'P_canopy_membrane']
json.dump({'schema': 'swat_motel_architecture_v1', 'units': 'metres, engine +Y up, +Z toward the court',
           'params': P, 'materials': [m.name for m in order], 'suppress': SUPPRESS,
           'pieces': [p[0] for p in PIECES], 'placements': PLACE},
          open(os.path.join(OUT, 'architecture_placements.json'), 'w'), indent=1)
print('architecture kit', path, 'pieces', len(PIECES), 'triangles', tris, 'faces kept whole', lost)
