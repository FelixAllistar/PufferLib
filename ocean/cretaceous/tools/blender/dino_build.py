# Blender (4.2) headless generator: a procedural dinosaur (skeleton, skinned
# body, baked textures, every clip render/dino.c plays) from a species spec,
# written as a glTF that tools/dino_convert.py turns into a .dino.
#   blender -b --factory-startup --disable-autoexec --python-exit-code 1 -P dino_build.py -- \
#       SPECIES OUT.glb [--spec FILE] [--sim sim.c] [--tex 1024] [--render DIR] [--samples 64]
#
# Nothing is downloaded or read besides the spec and sim.c: the mesh is
# metaballs over the bones, the textures are baked from procedural shader
# nodes and the clips are computed here (two-bone leg IK with planted feet).
#
# Blender space: the animal faces +X, +Y is its left, +Z is up, the feet
# stand on z = 0 and the hips are at x = 0. glTF turns this into the .dino's
# frame (Y up, facing +x).
import bpy, bmesh, sys, os, json, math, re, tempfile
import numpy as np
from mathutils import Matrix, Vector, Quaternion

argv = sys.argv[sys.argv.index("--") + 1:]
SPECIES, OUT_GLB = argv[0], argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
opt = {"--spec": os.path.join(HERE, "species", SPECIES + ".json"), "--sim": os.path.join(HERE, "..", "..", "sim.c"),
       "--tex": "1024", "--render": "", "--samples": "64", "--lod0": "24000"}
for i in range(2, len(argv), 2): opt[argv[i]] = argv[i + 1]
SPEC = json.load(open(opt["--spec"]))
TEX = int(opt["--tex"])
FPS = 30
QUAD = SPEC["plan"] == "quad"


# ---------------------------------------------------------------- the sim's numbers
def sim_row(path, asset):
    """Length, height, gait speeds and attack timing of `asset` from CRET_SPECIES_TABLE in sim.c."""
    src = open(path).read()
    m = re.search(r'\{"[^"]*",\s*"%s",\s*CRET_DIET_\w+,([^}]*)' % re.escape(asset), src)
    if not m: raise SystemExit("dino_build: %s not in %s" % (asset, path))
    v = [float(x) for x in re.findall(r"-?\d+\.\d+|-?\d+", m.group(1))]
    return dict(length=v[0], height=v[1], walk=v[4], trot=v[5], run=v[6], hit=v[16], attack_time=v[17])

SIM = sim_row(opt["--sim"], SPECIES)
L = SIM["length"]
SPEEDS = [SIM["walk"], SIM["trot"], SIM["run"]]


# ---------------------------------------------------------------- small helpers
def lerp(a, b, t): return a + (b - a) * t
def smooth(t): t = min(max(t, 0.0), 1.0); return t * t * (3 - 2 * t)
def interp_rows(rows, t):
    """Piecewise-linear interpolation of [t, values...] rows."""
    if t <= rows[0][0]: return list(rows[0][1:])
    for a, b in zip(rows, rows[1:]):
        if t <= b[0]:
            f = (t - a[0]) / max(b[0] - a[0], 1e-9)
            return [lerp(x, y, f) for x, y in zip(a[1:], b[1:])]
    return list(rows[-1][1:])
def rot_y(a): return Matrix.Rotation(a, 4, "Y")
def rot_z(a): return Matrix.Rotation(a, 4, "Z")
def rot_x(a): return Matrix.Rotation(a, 4, "X")
# Rotations named by what they do to a bone pointing forward (+X): nose up
# (about the side axis), yaw to the left, roll the left side down.
def nose_up(a): return rot_y(-a)
def yaw_left(a): return rot_z(a)
def roll_left_down(a): return rot_x(-a)
def about(p, R):
    """R turned about point p."""
    return Matrix.Translation(p) @ R @ Matrix.Translation(-p)


# ---------------------------------------------------------------- layout: joints from the spec
# Everything is laid out at the spec's fractions of the body length, then
# scaled so the snout-to-tail-tip extent is exactly the sim's length.
seg = SPEC["segments"]; sp = SPEC["spine"]; hind = SPEC["hind"]; fore = SPEC["fore"]


def leg_rest(H, leg, front):
    """Standing leg: the ball of the foot under the hip, the metatarsus at the
    spec's angle from vertical (positive: the ankle behind the ball), and the
    hip-to-ankle distance `stand` of the straight femur + tibia. Returns the
    hip height that makes this so, and the ball's forward offset."""
    a, b, m = leg["femur" if not front else "humerus"], leg["tibia" if not front else "forearm"], leg["metatarsus" if not front else "manus"]
    th = math.radians(leg["mt_deg"])
    toe_r = SPEC["limbs"]["toe"]
    az = toe_r + m * math.cos(th)
    dx_ankle = 0.03 * (a + b) * (1 if not front else -1)   # the ankle a little behind (front: ahead of) the hip line
    ball_dx = dx_ankle + m * math.sin(th)
    d = leg["stand"] * (a + b)
    return az + math.sqrt(max(d * d - dx_ankle * dx_ankle, 1e-6)), ball_dx


def build_layout():
    """Joint positions (unit body length) and the bone list: name, parent, head, tail."""
    hip_h, ball_dx = leg_rest(None, hind, False)
    Hs = hip_h + sp["sacrum"]                     # spine height over the hips
    bones = []
    def add(name, parent, head, tail): bones.append([name, parent, Vector(head), Vector(tail)])
    # Torso: hips to shoulders.
    if QUAD:
        sh_h, fball_dx = leg_rest(None, fore, True)
        sh_spine = sh_h + sp["withers"]
        dz = sh_spine - Hs; tl = seg["torso"]
        back = math.asin(max(-0.9, min(0.9, dz / tl)))
    else:
        back = math.radians(sp.get("back_deg", 0)); fball_dx = 0; sh_h = None
    nt = sp["torso_joints"]
    P = [Vector((0, 0, Hs))]
    for k in range(nt):
        # A gentle arch: the middle of the back rises a little above the hip-shoulder line.
        f = (k + 1) / nt
        p = Vector((seg["torso"] * math.cos(back) * f, 0, Hs + seg["torso"] * math.sin(back) * f + 0.04 * seg["torso"] * math.sin(math.pi * f) * (1 if QUAD else 0.3)))
        P.append(p)
    names = ["hips"] + ["spine_%02d" % (k + 1) for k in range(nt - 1)]
    for k in range(nt): add(names[k], names[k - 1] if k else None, P[k], P[k + 1])
    chest = names[-1]; shoulder = P[-1]
    # Neck: an S from the shoulders, its slope easing from the base angle to the head end's.
    nn = sp["neck_joints"]; a0, a1 = [math.radians(x) for x in sp["neck_rise_deg"]]
    q = shoulder.copy(); prev = chest; neck_pts = [q.copy()]
    for k in range(nn):
        a = lerp(a0, a1, (k + 0.5) / nn); step = seg["neck"] / nn
        q = q + Vector((math.cos(a) * step, 0, math.sin(a) * step)); neck_pts.append(q.copy())
    for k in range(nn): add("neck_%02d" % (k + 1), prev if k == 0 else "neck_%02d" % k, neck_pts[k], neck_pts[k + 1])
    hp = math.radians(sp["head_pitch_deg"]); sk = seg["skull"]
    occ = neck_pts[-1]
    snout = occ + Vector((math.cos(hp) * sk, 0, math.sin(hp) * sk))
    add("head", "neck_%02d" % nn, occ, snout)
    H = SPEC["head"]; depth = H["depth"] * sk
    hinge = occ + Vector((0.12 * sk * math.cos(hp), 0, 0)) + Vector((math.sin(hp), 0, -math.cos(hp))) * (0.45 * depth)
    jaw_tip = snout + Vector((math.sin(hp), 0, -math.cos(hp))) * (0.32 * depth) - Vector((math.cos(hp), 0, math.sin(hp))) * (0.06 * sk)
    add("jaw", "head", hinge, jaw_tip)
    # Tail: back from the hips, drooping a little, the tip curving up slightly.
    ntl = sp["tail_joints"]; dr = math.radians(sp["tail_droop_deg"])
    q = P[0].copy(); tail_pts = [q.copy()]
    for k in range(ntl):
        f = (k + 0.5) / ntl; a = dr * (1 - 0.6 * f)
        step = seg["tail"] / ntl * (1.25 - 0.5 * f)   # longer vertebrae near the base
        q = q + Vector((-math.cos(a) * step, 0, -math.sin(a) * step)); tail_pts.append(q.copy())
    s_ = seg["tail"] / (tail_pts[0] - tail_pts[-1]).length
    tail_pts = [tail_pts[0] + (p - tail_pts[0]) * s_ for p in tail_pts]
    for k in range(ntl): add("tail_%02d" % (k + 1), "hips" if k == 0 else "tail_%02d" % k, tail_pts[k], tail_pts[k + 1])
    # Hind legs, posed by the same IK the clips use.
    legs = {}
    for side, sg in (("L", 1), ("R", -1)):
        Hj = Vector((0, sg * hind["spread"], hip_h))
        B = Vector((ball_dx, sg * hind["foot_spread"], SPEC["limbs"]["toe"]))
        K, A = solve_leg(Hj, B, hind["femur"], hind["tibia"], hind["metatarsus"], math.radians(hind["mt_deg"]), Vector((1, 0, 0)))
        T = B + Vector((hind["toe"], 0, 0))
        nm = ("upperleg", "lowerleg", "backfoot", "backtoe") if QUAD else ("upperleg", "lowerleg", "ankle", "toe")
        add("%s.%s" % (nm[0], side), "hips", Hj, K); add("%s.%s" % (nm[1], side), "%s.%s" % (nm[0], side), K, A)
        add("%s.%s" % (nm[2], side), "%s.%s" % (nm[1], side), A, B); add("%s.%s" % (nm[3], side), "%s.%s" % (nm[2], side), B, T)
        legs[side + "H"] = ["%s.%s" % (n, side) for n in nm]
        if QUAD:
            Sj = Vector((shoulder.x - 0.02, sg * fore["spread"], sh_h))
            Bf = Vector((Sj.x + fball_dx, sg * fore["foot_spread"], SPEC["limbs"]["toe"]))
            K, A = solve_leg(Sj, Bf, fore["humerus"], fore["forearm"], fore["manus"], math.radians(fore["mt_deg"]), Vector((-1, 0, 0)))
            T = Bf + Vector((fore["toe"], 0, 0))
            nm = ("upperarm", "lowerarm", "foot", "fronttoe")
            add("upperarm.%s" % side, chest, Sj, K); add("lowerarm.%s" % side, "upperarm.%s" % side, K, A)
            add("foot.%s" % side, "lowerarm.%s" % side, A, Bf); add("fronttoe.%s" % side, "foot.%s" % side, Bf, T)
            legs[side + "F"] = ["%s.%s" % (n, side) for n in nm]
        else:
            # Arms folded: the humerus down and back, the forearm forward, the hand hanging.
            g = SPEC["girth"]["torso"][-1]
            Sj = shoulder + Vector((-0.01, sg * fore["spread"], -0.6 * g[3]))
            E = Sj + Vector((-0.35, 0, -0.94)).normalized() * fore["humerus"]
            W = E + Vector((0.85, 0, -0.3)).normalized() * fore["forearm"]
            Hd = W + Vector((0.5, 0, -0.85)).normalized() * fore["manus"]
            add("upperarm.%s" % side, chest, Sj, E); add("lowerarm.%s" % side, "upperarm.%s" % side, E, W)
            add("hand.%s" % side, "lowerarm.%s" % side, W, Hd)
    return bones, legs, dict(hip_h=hip_h, ball_dx=ball_dx, fball_dx=fball_dx, sh_h=sh_h)


def solve_leg(H, B, a, b, m, phi, pole):
    """Two-bone IK below a fixed metatarsus: the ankle sits m from the ball B at
    angle phi from vertical (positive: behind), turned toward the hip only as
    far as needed for the hip to reach it; the knee bends toward `pole`.
    Returns the knee and ankle."""
    def ankle(ph): return B + Vector((-math.sin(ph), 0, math.cos(ph))) * m
    reach = 0.985 * (a + b)
    A = ankle(phi)
    if (A - H).length > reach:
        # Lift the heel: swing the metatarsus about the ball toward the hip.
        d = H - B; ph_h = math.atan2(-d.x, d.z)
        lo, hi = 0.0, 1.0
        for _ in range(30):
            mid = 0.5 * (lo + hi)
            if (ankle(lerp(phi, ph_h, mid)) - H).length > reach: lo = mid
            else: hi = mid
        A = ankle(lerp(phi, ph_h, hi))
    u = A - H; dist = u.length; u.normalize()
    dist = min(max(dist, abs(a - b) + 1e-4), (a + b) * 0.9999)
    v = pole - u * pole.dot(u)
    if v.length < 1e-6: v = Vector((0, 0, 1)).cross(u)
    v.normalize()
    ca = (a * a + dist * dist - b * b) / (2 * a * dist); ca = min(max(ca, -1), 1)
    K = H + (u * ca + v * math.sqrt(1 - ca * ca)) * a
    return K, H + u * dist


BONES, LEGS, GEO = build_layout()
# Scale to the sim's length: the extent from the snout to the tail tip.
xs = [p.x for b in BONES for p in (b[2], b[3])]
SC = L / (max(xs) - min(xs))
for b in BONES: b[2] = b[2] * SC; b[3] = b[3] * SC
HIP_H = GEO["hip_h"] * SC
def S(x): return x * SC                         # spec fraction -> metres
BONE = {b[0]: b for b in BONES}
print("dino_build: %s %.2f m, hip height %.2f m, %d bones, scale %.3f" % (SPECIES, L, HIP_H, len(BONES), SC))


# ---------------------------------------------------------------- armature
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = FPS
arm_data = bpy.data.armatures.new("rig")
ARM = bpy.data.objects.new("rig", arm_data)
scene.collection.objects.link(ARM)
bpy.context.view_layer.objects.active = ARM
ARM.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
for name, parent, head, tail in BONES:
    eb = arm_data.edit_bones.new(name); eb.head = head; eb.tail = tail
    eb.align_roll(Vector((0, 0, 1)) if abs((tail - head).normalized().z) < 0.9 else Vector((1, 0, 0)))
for name, parent, head, tail in BONES:
    if parent:
        eb = arm_data.edit_bones[name]; eb.parent = arm_data.edit_bones[parent]; eb.use_connect = False
bpy.ops.object.mode_set(mode="OBJECT")
REST = {b.name: b.matrix_local.copy() for b in arm_data.bones}
PARENT = {b[0]: b[1] for b in BONES}
ORDER = [b[0] for b in BONES]                   # parents always come first
DEFORM_SKIP = set()


# ---------------------------------------------------------------- body: metaballs over the bones
MB_STIFF, MB_THRESH = 2.0, 0.6
def mb_radius(visible, spacing_ratio):
    """Element radius whose dense chain (spacing = ratio * radius) shows `visible`.
    Each element adds s (1 - d^2/r^2)^3; summed along a line that is
    s/c * 32/35 * (1 - u^2)^(7/2), so the surface sits where that is the threshold."""
    k = (MB_THRESH * spacing_ratio * 35.0 / (32.0 * MB_STIFF)) ** (2.0 / 7.0)
    return visible / math.sqrt(1 - k)


class Body:
    def __init__(self):
        self.mb = bpy.data.metaballs.new("body")
        self.mb.threshold = MB_THRESH
        self.mb.resolution = self.mb.render_resolution = L / 320.0
        self.ob = bpy.data.objects.new("body", self.mb)
        scene.collection.objects.link(self.ob)

    def chain(self, pts, prof, c=0.3, sx=0.6):
        """Ellipsoids along a polyline. prof(t) -> (half width, up, down) at t in 0..1
        along it; 'up' and 'down' are measured from the line, so the section's
        centre sits below the line when the belly hangs lower than the back is high."""
        cum = [0.0]
        for a, b in zip(pts, pts[1:]): cum.append(cum[-1] + (b - a).length)
        total = cum[-1]; s = 0.0
        while s <= total + 1e-9:
            k = max(i for i in range(len(cum)) if cum[i] <= s + 1e-12)
            k = min(k, len(pts) - 2)
            f = (s - cum[k]) / max(cum[k + 1] - cum[k], 1e-9)
            p = pts[k].lerp(pts[k + 1], f); tng = (pts[k + 1] - pts[k]).normalized()
            w, up, dn = prof(s / total)
            hv = 0.5 * (up + dn); centre = p + tng.cross(Vector((0, 1, 0))).normalized() * (0.5 * (up - dn)) * (1 if tng.cross(Vector((0, 1, 0))).z > 0 else -1)
            big = max(w, hv)
            r = mb_radius(big, c)
            e = self.mb.elements.new(type="ELLIPSOID"); e.co = centre; e.radius = r
            e.size_x = sx; e.size_y = w / big; e.size_z = hv / big
            e.rotation = Vector((1, 0, 0)).rotation_difference(tng)
            s += c * r * sx

    def tube(self, a, b, r0, r1, c=0.3):
        """Balls from a to b, the visible radius going from r0 to r1."""
        d = (b - a).length; s = 0.0
        while s <= d + 1e-9:
            t = s / max(d, 1e-9); rv = lerp(r0, r1, t); r = mb_radius(rv, c)
            e = self.mb.elements.new(type="BALL"); e.co = a.lerp(b, t); e.radius = r
            s += max(c * r, 1e-4)

    def blob(self, centre, half, rot=None):
        """One ellipsoid of half-extents `half` (an isolated element shows 0.575 of its radius)."""
        big = max(half); r = big / math.sqrt(1 - (MB_THRESH / MB_STIFF) ** (1 / 3))
        e = self.mb.elements.new(type="ELLIPSOID"); e.co = centre; e.radius = r
        e.size_x, e.size_y, e.size_z = half[0] / big, half[1] / big, half[2] / big
        if rot is not None: e.rotation = rot

    def to_mesh(self):
        dg = bpy.context.evaluated_depsgraph_get()
        me = bpy.data.meshes.new_from_object(self.ob.evaluated_get(dg))
        ob = bpy.data.objects.new("hide", me); scene.collection.objects.link(ob)
        bpy.data.objects.remove(self.ob); bpy.data.metaballs.remove(self.mb)
        return ob


def bone_pts(names):
    pts = [BONE[names[0]][2]]
    for n in names: pts.append(BONE[n][3])
    return pts


def build_body():
    body = Body(); G = SPEC["girth"]; lim = SPEC["limbs"]
    torso = ["hips"] + ["spine_%02d" % (k + 1) for k in range(sp["torso_joints"] - 1)]
    neck = ["neck_%02d" % (k + 1) for k in range(sp["neck_joints"])]
    tail = ["tail_%02d" % (k + 1) for k in range(sp["tail_joints"])]
    def prof(rows): return lambda t: [S(x) for x in interp_rows(rows, t)]
    body.chain(bone_pts(torso), prof(G["torso"]))
    body.chain(bone_pts(neck), prof(G["neck"]))
    body.chain(bone_pts(tail), prof(G["tail"]))
    # Head: cranium, snout and lower jaw as ellipsoids along the head bone.
    hb = BONE["head"]; occ, tip = hb[2], hb[3]; ax = (tip - occ).normalized(); sk = (tip - occ).length
    Hs = SPEC["head"]; dep = Hs["depth"] * sk; wid = Hs["width"] * sk
    upv = ax.cross(Vector((0, -1, 0))).normalized()
    if upv.z < 0: upv = -upv
    rot = Vector((1, 0, 0)).rotation_difference(ax)
    body.blob(occ + ax * (0.3 * sk) + upv * (0.05 * dep), (0.36 * sk, 0.5 * wid, 0.48 * dep), rot)
    body.blob(occ + ax * (0.66 * sk) + upv * (Hs["snout_up"] * sk), (0.36 * sk, 0.5 * wid * (0.45 + 0.5 * Hs["snout"]), 0.4 * dep * (0.5 + 0.6 * Hs["snout"])), rot)
    jb = BONE["jaw"]; ja = (jb[3] - jb[2]).normalized(); jl = (jb[3] - jb[2]).length
    body.blob(jb[2].lerp(jb[3], 0.45), (0.5 * jl, 0.42 * wid, Hs["jaw"] * 0.5 * dep), Vector((1, 0, 0)).rotation_difference(ja))
    if Hs.get("dome"):   # the pachycephalosaur's dome over the braincase
        body.blob(occ + ax * (0.3 * sk) + upv * (0.42 * dep), (0.36 * sk, 0.5 * wid * 0.95, Hs["dome"] * dep), rot)
    # Limbs: tapering tubes along each bone; the toes are three splayed digits.
    for key, names in LEGS.items():
        fr = key.endswith("F")
        b0, b1, b2, b3 = [BONE[n] for n in names]
        th = lim["upperarm"] if fr else lim["thigh"]; sh = lim["forearm"] if fr else lim["shank"]
        # Thigh: a muscular cone from the hip, its top merging with the body.
        body.tube(b0[2], b0[3], S(th[0]), S(th[1])); body.tube(b1[2], b1[3], S(sh[0]), S(sh[1]))
        body.tube(b2[2], b2[3], S(lim["mt"][0]), S(lim["mt"][1]))
        tr = S(lim["toe"]); tl = (b3[3] - b3[2]).length
        for ang in (-0.32, 0.0, 0.32):
            d = Matrix.Rotation(ang, 3, "Z") @ Vector((1, 0, 0))
            body.tube(b3[2], b3[2] + d * tl * (1.0 if ang == 0 else 0.8), tr, tr * 0.7)
    if not QUAD:
        for side in "LR":
            u, l_, h = BONE["upperarm." + side], BONE["lowerarm." + side], BONE["hand." + side]
            body.tube(u[2], u[3], S(lim["upperarm"][0]), S(lim["upperarm"][1]))
            body.tube(l_[2], l_[3], S(lim["forearm"][0]), S(lim["forearm"][1]))
            body.tube(h[2], h[3], S(lim["hand"]), S(lim["hand"]) * 0.6)
    return body.to_mesh()


def remesh_and_reduce(ob, target_tris):
    """Voxel remesh (one closed, even surface), smooth, then decimate to the budget."""
    bpy.context.view_layer.objects.active = ob; ob.select_set(True)
    m = ob.modifiers.new("remesh", "REMESH"); m.mode = "VOXEL"; m.voxel_size = L / 300.0; m.adaptivity = 0.0
    bpy.ops.object.modifier_apply(modifier="remesh")
    m = ob.modifiers.new("smooth", "CORRECTIVE_SMOOTH"); m.iterations = 4; m.smooth_type = "SIMPLE"; m.use_only_smooth = True
    bpy.ops.object.modifier_apply(modifier="smooth")
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    if tris > target_tris:
        m = ob.modifiers.new("dec", "DECIMATE"); m.ratio = target_tris / tris; m.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier="dec")
    for p in ob.data.polygons: p.use_smooth = True
    ob.select_set(False)


# ---------------------------------------------------------------- armour, plates, claws, eyes
FEAT_BODY, FEAT_PLATE, FEAT_HORN, FEAT_EYE, FEAT_DOME = 0.0, 1.0, 2.0, 3.0, 4.0
EXTRAS = []    # (object, bone) pairs joined after the body is weighted; each weighted rigidly to its bone


def add_extra(bm, bone, feat, name):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me); scene.collection.objects.link(ob)
    at = me.attributes.new("feat", "FLOAT", "POINT"); at.data.foreach_set("value", [feat] * len(me.vertices))
    for p in me.polygons: p.use_smooth = True
    EXTRAS.append((ob, bone))


def cone(base, axis, length, radius, segs=10, flat=1.0, side=None):
    """A cone (spike, claw) from `base` along `axis`; flat < 1 squashes it across `side`."""
    bm = bmesh.new(); ax = axis.normalized()
    s = side.normalized() if side is not None else ax.orthogonal().normalized()
    t = ax.cross(s).normalized()
    ring = []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        ring.append(bm.verts.new(base + (s * math.cos(a) * flat + t * math.sin(a)) * radius))
    apex = bm.verts.new(base + ax * length)
    bm.faces.new(ring[::-1])
    for i in range(segs): bm.faces.new((ring[i], ring[(i + 1) % segs], apex))
    return bm


def plate(base, up, fwd, height, length, thick):
    """A stegosaur plate: a thin rounded-diamond slab standing on the back."""
    bm = bmesh.new(); n = 14; side = fwd.cross(up).normalized()
    front, back = [], []
    for i in range(n):
        a = math.pi * i / (n - 1)        # 0 at the front foot of the plate, pi at the back
        x = -math.cos(a) * 0.5 * length; h = max(math.sin(a), 0.0) ** 0.8 * height
        # The outline leans back and peaks behind the middle.
        p = base + fwd * (x - 0.15 * h) + up * h
        front.append(bm.verts.new(p + side * thick * (1 - h / height * 0.7)))
        back.append(bm.verts.new(p - side * thick * (1 - h / height * 0.7)))
    for i in range(n - 1):
        bm.faces.new((front[i], front[i + 1], back[i + 1], back[i]))
    bm.faces.new(front[::-1]); bm.faces.new(back)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def sphere(c, r):
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=r)
    for v in bm.verts: v.co += c
    return bm


def chain_point(names, t):
    """Point and tangent at fraction t along a bone chain."""
    pts = bone_pts(names); cum = [0.0]
    for a, b in zip(pts, pts[1:]): cum.append(cum[-1] + (b - a).length)
    s = t * cum[-1]
    for k in range(len(pts) - 1):
        if s <= cum[k + 1] + 1e-9:
            f = (s - cum[k]) / max(cum[k + 1] - cum[k], 1e-9)
            return pts[k].lerp(pts[k + 1], f), (pts[k + 1] - pts[k]).normalized(), names[k]
    return pts[-1], (pts[-1] - pts[-2]).normalized(), names[-1]


def build_extras():
    F = SPEC["features"]; G = SPEC["girth"]; lim = SPEC["limbs"]
    torso = ["hips"] + ["spine_%02d" % (k + 1) for k in range(sp["torso_joints"] - 1)]
    neck = ["neck_%02d" % (k + 1) for k in range(sp["neck_joints"])]
    tail = ["tail_%02d" % (k + 1) for k in range(sp["tail_joints"])]
    # The dorsal line from the neck's head end back to the tail tip, with the back's height above the spine.
    back_line = [(n, rows) for n, rows in ((neck[::-1], G["neck"]), (torso[::-1], G["torso"]), (tail, G["tail"]))]
    # Eyes: on the side of the cranium, a third of the way along the skull.
    hb = BONE["head"]; occ, tip = hb[2], hb[3]; sk = (tip - occ).length; ax = (tip - occ).normalized()
    upv = ax.cross(Vector((0, -1, 0))).normalized()
    if upv.z < 0: upv = -upv
    Hs = SPEC["head"]; wid = Hs["width"] * sk; dep = Hs["depth"] * sk
    for sg in (1, -1):
        c = occ + ax * (0.3 * sk) + upv * (0.18 * dep) + Vector((0, sg * 0.38 * wid, 0))
        add_extra(sphere(c, 0.075 * dep), "head", FEAT_EYE, "eye")
    EYES.extend([occ + ax * (0.3 * sk) + upv * (0.18 * dep) + Vector((0, sg * 0.38 * wid, 0)) for sg in (1, -1)])
    if "claws" in F:
        for key, names in LEGS.items():
            toe = BONE[names[3]]; tl = (toe[3] - toe[2]).length
            for ang in (-0.32, 0.0, 0.32):
                d = Matrix.Rotation(ang, 3, "Z") @ Vector((1, 0, 0))
                tipp = toe[2] + d * tl * (1.0 if ang == 0 else 0.8)
                base = tipp - d * S(lim["toe"]) * 0.3 + Vector((0, 0, 0.2 * S(lim["toe"])))
                add_extra(cone(base, d + Vector((0, 0, -0.3)), S(F["claws"]), S(lim["toe"]) * 0.55, 8), names[3], FEAT_HORN, "claw")
        if not QUAD:
            for side in "LR":
                h = BONE["hand." + side]; d = (h[3] - h[2]).normalized()
                for k in (-1, 0, 1):
                    add_extra(cone(h[3] + Vector((0, k * S(lim["hand"]), 0)), d + Vector((0.3, 0, -0.4)), S(F["claws"]) * 0.9, S(lim["hand"]) * 0.45, 6), "hand." + side, FEAT_HORN, "claw")
    if "sickle_claw" in F:   # the raised second-toe claw of dromaeosaurs
        for side, sg in (("L", 1), ("R", -1)):
            toe = BONE["toe." + side]; b = toe[2] + Vector((0.2 * S(F["sickle_claw"]), sg * S(lim["toe"]) * 1.2, S(lim["toe"]) * 1.3))
            add_extra(cone(b, Vector((0.5, 0, 1.0)), S(F["sickle_claw"]), S(lim["toe"]) * 0.7, 8, 0.5, Vector((0, 1, 0))), "toe." + side, FEAT_HORN, "sickle")
    if "plates" in F:
        P = F["plates"]; n = P["count"]
        # Two staggered rows along neck, back and tail; the tallest over the hips and the tail base.
        dorsal = ["neck_%02d" % (k + 1) for k in range(sp["neck_joints"])][::-1]
        line = [BONE[dorsal[0]][3]] + [BONE[d][2] for d in dorsal] + [BONE[t][2] for t in torso[::-1][:-1]] + [BONE["hips"][2]] + [BONE[t][3] for t in tail]
        names = dorsal + torso[::-1][:-1] + ["hips"] + tail
        cum = [0.0]
        for a, b in zip(line, line[1:]): cum.append(cum[-1] + (b - a).length)
        for i in range(n):
            t = lerp(P["from"], P["to"], (i + 0.5) / n); s = t * cum[-1]
            k = max(j for j in range(len(cum) - 1) if cum[j] <= s + 1e-9); f = (s - cum[k]) / max(cum[k + 1] - cum[k], 1e-9)
            p = line[k].lerp(line[k + 1], f); fwd = (line[k] - line[k + 1]).normalized()
            hip_t = cum[len(dorsal) + len(torso) - 1] / cum[-1]
            hgt = S(P["height"]) * (0.35 + 0.65 * math.exp(-((t - hip_t - 0.05) / 0.22) ** 2))
            up_off = S(sp["sacrum"]) * 0.6 + S(0.025)
            sg = 1 if i % 2 else -1
            base = p + Vector((0, sg * S(0.012), 0)) + Vector((0, 0, 1)) * up_off * (0.5 if t < 0.15 else 1.0)
            upd = Vector((0, sg * 0.18, 1)).normalized()
            add_extra(plate(base - upd * hgt * 0.15, upd, fwd, hgt, hgt * 1.15, S(0.006)), names[min(k, len(names) - 1)], FEAT_PLATE, "plate")
    if "thagomizer" in F:
        tl = S(F["thagomizer"]["length"])
        for j, t in enumerate((0.84, 0.92)):
            p, d, bn = chain_point(tail, t)
            w = S(interp_rows(G["tail"], t)[0])
            for sg in (1, -1):
                axis = Vector((-0.45 * (1 + j * 0.3), sg * 1.0, 0.75)).normalized()
                add_extra(cone(p + Vector((0, sg * w * 0.6, w * 0.4)), axis, tl, tl * 0.09, 10), bn, FEAT_HORN, "spike")
    if "osteoderms" in F:
        O = F["osteoderms"]; sz = S(O["size"])
        # Bands of keeled scutes across the back and tail, a row of spikes along the flanks.
        span = torso[::-1] + tail[:6]
        for r in range(O["rows"]):
            t = (r + 0.5) / O["rows"]
            p, d, bn = chain_point(span, t)
            region = "torso" if t < len(torso) / len(span) else "tail"
            tt = t * len(span) / len(torso) if region == "torso" else (t * len(span) - len(torso)) / len(tail)
            w, up, dn = [S(x) for x in interp_rows(G[region], 1 - tt if region == "torso" else tt)]
            for c in range(O["per_row"]):
                a = math.pi * (c + 0.5) / O["per_row"]          # across the back, from flank to flank
                off = Vector((0, math.cos(a) * w * 0.92, math.sin(a) * up * 0.9 + (0.2 * w if region == "torso" else 0.0) * math.sin(a)))
                nrm = Vector((0, math.cos(a) * up, math.sin(a) * w)).normalized()
                big = 1.0 + 0.8 * (1 - math.sin(a))     # bigger, spikier at the flanks
                add_extra(cone(p + off - nrm * sz * 0.3, nrm + Vector((-0.25, 0, 0)), sz * big, sz * 0.7, 8), bn, FEAT_HORN, "scute")
    if "spikes" in F:
        Sx = F["spikes"]; ln = S(Sx["length"])
        # Long spikes on the neck and shoulders, up and out, the longest at the shoulders.
        spots = [(neck[0], 0.3, 0.6), (neck[min(1, len(neck) - 1)], 0.5, 0.75), (torso[-1], 0.85, 1.0), (torso[-1], 0.4, 0.8)]
        for (bn, f, k) in spots[:Sx["count"]]:
            b = BONE[bn]; p = b[2].lerp(b[3], f); t = 0.0
            reg = "neck" if bn.startswith("neck") else "torso"
            w, up, dn = [S(x) for x in interp_rows(G[reg], 0.2 if reg == "neck" else 0.9)]
            for sg in (1, -1):
                axis = Vector((0.25, sg * 1.0, 0.55)).normalized()
                add_extra(cone(p + Vector((0, sg * w * 0.8, up * 0.4)), axis, ln * k, ln * k * 0.16, 10), bn, FEAT_HORN, "spike")
    if "dome_knobs" in F:
        kr = S(F["dome_knobs"])
        for i in range(9):
            a = math.pi * (0.15 + 0.7 * i / 8)
            c = occ + ax * (0.05 * sk + 0.12 * sk * math.sin(a)) + upv * (0.35 * dep) + Vector((0, math.cos(a) * 0.45 * wid, 0))
            add_extra(cone(c - upv * kr * 0.3, -ax + upv * 0.5 + Vector((0, math.cos(a), 0)), kr, kr * 0.7, 6), "head", FEAT_HORN, "knob")
    if "brow_horns" in F:
        kr = S(F["brow_horns"])
        for sg in (1, -1):
            c = occ + ax * (0.22 * sk) + upv * (0.4 * dep) + Vector((0, sg * 0.32 * wid, 0))
            add_extra(cone(c, upv + ax * 0.3, kr, kr * 0.8, 6), "head", FEAT_HORN, "brow")


EYES = []


# ---------------------------------------------------------------- weights
def bone_segments():
    """Deforming bones as segments (head, tail) in rest space."""
    return {n: (BONE[n][2], BONE[n][3]) for n in ORDER}


def seg_dist(p, a, b):
    ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (p - (a + ab * t)).length


def hops(a, b):
    """Bones between a and b in the hierarchy."""
    def up(n):
        out = []
        while n: out.append(n); n = PARENT[n]
        return out
    ua, ub = up(a), up(b)
    common = next(x for x in ua if x in ub)
    return ua.index(common) + ub.index(common)


def weight_body(ob):
    """Automatic (bone heat) weights, then cleaned: every vertex keeps only
    joints within two bones of its nearest one in the hierarchy, never a joint
    of the other side, and at most four, renormalised. Vertices bone heat left
    empty take their nearest bone."""
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True); ARM.select_set(True); bpy.context.view_layer.objects.active = ARM
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    segs = bone_segments(); names = list(segs)
    gi = {g.index: g.name for g in ob.vertex_groups}
    cleaned = empty = 0; far_max = 0.0
    W = []
    for v in ob.data.vertices:
        p = v.co
        dist = {n: seg_dist(p, *segs[n]) for n in names}
        near = min(dist, key=dist.get)
        side = "L" if p.y > 0.01 * L else "R" if p.y < -0.01 * L else None
        ws = {}
        for g in v.groups:
            n = gi[g.group]
            if g.weight < 1e-4 or n not in segs: continue
            other = n.endswith("." + ("R" if side == "L" else "L")) if side else False
            if hops(n, near) > 2 or other or dist[n] > 3.0 * dist[near] + 0.02 * L:
                cleaned += 1; continue
            ws[n] = g.weight
        if not ws: ws = {near: 1.0}; empty += 1
        top = sorted(ws.items(), key=lambda kv: -kv[1])[:4]; s = sum(w for _, w in top)
        W.append([(n, w / s) for n, w in top])
        far_max = max(far_max, max(dist[n] - dist[near] for n, _ in top))
    for g in list(ob.vertex_groups): ob.vertex_groups.remove(g)
    groups = {n: ob.vertex_groups.new(name=n) for n in names}
    for i, ws in enumerate(W):
        for n, w in ws: groups[n].add([i], w, "REPLACE")
    print("dino_build: weights: %d links to far or wrong-side joints removed, %d vertices given their nearest bone, worst kept joint %.0f mm beyond the nearest"
          % (cleaned, empty, far_max * 1000))
    return far_max


def join_extras(body):
    for ob, bone in EXTRAS:
        g = ob.vertex_groups.new(name=bone); g.add(list(range(len(ob.data.vertices))), 1.0, "REPLACE")
    bpy.ops.object.select_all(action="DESELECT")
    for ob, _ in EXTRAS: ob.select_set(True)
    body.select_set(True); bpy.context.view_layer.objects.active = body
    bpy.ops.object.join()
    EXTRAS.clear()


# ---------------------------------------------------------------- UVs and baked textures
def unwrap(ob):
    bpy.ops.object.select_all(action="DESELECT"); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004, area_weight=0.0, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="OBJECT")


def node(nt, kind, loc=(0, 0), **inputs):
    n = nt.nodes.new(kind); n.location = loc
    for k, v in inputs.items():
        if k in n.inputs: n.inputs[k].default_value = v
        else: setattr(n, k, v)
    return n


def link(nt, a, b): nt.links.new(a, b)


def pattern_material():
    """The procedural skin, in rest-pose object space (metres): albedo with
    countershading (pale where the surface faces down, dark along the top) and
    the species' pattern on the back and flanks, scales in the bump, and
    roughness. Feature parts (plates, horn, eyes) are told apart by the 'feat'
    attribute."""
    # The spec's colours are sRGB; shader nodes take linear values.
    C = {k: ([c ** 2.2 for c in v] if isinstance(v, list) else v) for k, v in SPEC["colors"].items()}
    mat = bpy.data.materials.new("bake"); mat.use_nodes = True; nt = mat.node_tree; nt.nodes.clear()
    out = node(nt, "ShaderNodeOutputMaterial", (1600, 0))
    tc = node(nt, "ShaderNodeTexCoord", (-1400, 0))
    geo = node(nt, "ShaderNodeNewGeometry", (-1400, -300))
    nz = node(nt, "ShaderNodeSeparateXYZ", (-1200, -300)); link(nt, geo.outputs["Normal"], nz.inputs[0])
    feat = node(nt, "ShaderNodeAttribute", (-1400, 300), attribute_name="feat", attribute_type="GEOMETRY")
    obj = tc.outputs["Object"]
    def mapr(lo, hi, src, loc):
        m = node(nt, "ShaderNodeMapRange", loc); m.inputs[1].default_value = lo; m.inputs[2].default_value = hi
        m.interpolation_type = "SMOOTHSTEP"; link(nt, src, m.inputs[0]); return m.outputs[0]
    def mix(a, b, fac, loc):
        m = node(nt, "ShaderNodeMix", loc); m.data_type = "RGBA"; m.blend_type = "MIX"
        link(nt, fac, m.inputs[0])
        for sock, val in ((m.inputs[6], a), (m.inputs[7], b)):
            if isinstance(val, (list, tuple)): sock.default_value = (*val, 1)
            else: link(nt, val, sock)
        return m.outputs[2]
    def math_(op, a, b, loc):
        m = node(nt, "ShaderNodeMath", loc, operation=op)
        for sock, val in ((m.inputs[0], a), (m.inputs[1], b)):
            if isinstance(val, (int, float)): sock.default_value = val
            else: link(nt, val, sock)
        return m.outputs[0]
    # Countershading: belly colour where the normal points down, base on the flanks, dark along the top.
    belly_k = mapr(0.15, -0.45, nz.outputs[2], (-1000, -300))
    top_k = mapr(0.55, 0.98, nz.outputs[2], (-1000, -450))
    col = mix(C["base"], C["belly"], belly_k, (-800, -300))
    col = mix(col, C["dark"], math_("MULTIPLY", top_k, 0.55, (-900, -500)), (-600, -350))
    # The pattern, kept off the belly.
    sc = C["pattern_scale"]; kind = C["pattern"]
    if kind == "stripes":       # dark bands across the back, broken up by noise
        w = node(nt, "ShaderNodeTexWave", (-1100, 100), wave_type="BANDS", bands_direction="X")
        w.inputs["Scale"].default_value = 1.0 / sc; w.inputs["Distortion"].default_value = 6.0; w.inputs["Detail"].default_value = 3.0
        link(nt, obj, w.inputs["Vector"]); pat = mapr(0.62, 0.8, w.outputs["Fac"], (-900, 100))
    elif kind == "blotch":      # large irregular patches
        v = node(nt, "ShaderNodeTexVoronoi", (-1100, 100), feature="F1"); v.inputs["Scale"].default_value = 1.0 / sc
        v.inputs["Randomness"].default_value = 1.0; link(nt, obj, v.inputs["Vector"])
        n = node(nt, "ShaderNodeTexNoise", (-1100, 300)); n.inputs["Scale"].default_value = 3.0 / sc; link(nt, obj, n.inputs["Vector"])
        pat = mapr(0.32, 0.18, math_("ADD", v.outputs["Distance"], math_("MULTIPLY", n.outputs["Fac"], 0.25, (-950, 300)), (-950, 150)), (-800, 100))
    elif kind == "mottle":      # fine dark mottling
        n = node(nt, "ShaderNodeTexNoise", (-1100, 100)); n.inputs["Scale"].default_value = 1.0 / sc; n.inputs["Detail"].default_value = 6.0
        link(nt, obj, n.inputs["Vector"]); pat = mapr(0.52, 0.66, n.outputs["Fac"], (-900, 100))
    elif kind == "spots":       # pale and dark spots on the flanks
        v = node(nt, "ShaderNodeTexVoronoi", (-1100, 100), feature="F1"); v.inputs["Scale"].default_value = 1.0 / sc
        link(nt, obj, v.inputs["Vector"]); pat = mapr(0.28, 0.12, v.outputs["Distance"], (-900, 100))
    elif kind == "bands":       # dark bands across the armour
        w = node(nt, "ShaderNodeTexWave", (-1100, 100), wave_type="BANDS", bands_direction="X")
        w.inputs["Scale"].default_value = 1.0 / sc; w.inputs["Distortion"].default_value = 1.5
        link(nt, obj, w.inputs["Vector"]); pat = mapr(0.55, 0.75, w.outputs["Fac"], (-900, 100))
    else:                       # "dorsal": a darker saddle along the top only
        n = node(nt, "ShaderNodeTexNoise", (-1100, 100)); n.inputs["Scale"].default_value = 1.0 / sc
        link(nt, obj, n.inputs["Vector"]); pat = math_("MULTIPLY", mapr(0.2, 0.75, nz.outputs[2], (-900, 0)), mapr(0.35, 0.6, n.outputs["Fac"], (-900, 100)), (-800, 50))
    off_belly = mapr(-0.25, 0.15, nz.outputs[2], (-800, 200))
    pat = math_("MULTIPLY", pat, off_belly, (-650, 150))
    tone = C["accent"] if kind in ("spots", "stripes") else C["dark"]
    col = mix(col, tone, math_("MULTIPLY", pat, 0.85, (-500, 150)), (-400, -200))
    # Feather streaks (raptor, gallimimus): fine stretched noise along the body.
    if C.get("feathers"):
        fm = node(nt, "ShaderNodeMapping", (-1200, 500)); fm.inputs["Scale"].default_value = (0.15, 1.0, 1.0); link(nt, obj, fm.inputs["Vector"])
        fn = node(nt, "ShaderNodeTexNoise", (-1000, 500)); fn.inputs["Scale"].default_value = 0.35 / C["scale_size"]; link(nt, fm.outputs[0], fn.inputs["Vector"])
        col = mix(col, C["dark"], math_("MULTIPLY", mapr(0.45, 0.7, fn.outputs["Fac"], (-850, 500)), 0.3, (-700, 500)), (-250, -150))
    # Low-frequency colour variation so large areas are not flat.
    vn = node(nt, "ShaderNodeTexNoise", (-1100, 700)); vn.inputs["Scale"].default_value = 4.0 / L; link(nt, obj, vn.inputs["Vector"])
    col = mix(col, C["dark"], math_("MULTIPLY", mapr(0.4, 0.7, vn.outputs["Fac"], (-900, 700)), 0.25, (-700, 700)), (-100, -150))
    # Eyes: near black with a dark rim around them.
    eyed = None
    for e in EYES:
        d = node(nt, "ShaderNodeVectorMath", (-1200, 900), operation="DISTANCE"); d.inputs[1].default_value = e; link(nt, obj, d.inputs[0])
        eyed = d.outputs["Value"] if eyed is None else math_("MINIMUM", eyed, d.outputs["Value"], (-1000, 900))
    er = 0.075 * SPEC["head"]["depth"] * SC * SPEC["segments"]["skull"]
    col = mix(col, C["dark"], mapr(er * 2.2, er * 1.2, eyed, (-800, 900)), (50, -150))
    # Feature parts: plates (accent colour toward their rims), horn (dark keratin), eyes.
    is_ = lambda v: mapr(v - 0.5, v - 0.49, feat.outputs["Fac"], (-900, 1100 + 100 * v))
    lo_ = lambda v: math_("SUBTRACT", 1.0, mapr(v + 0.49, v + 0.5, feat.outputs["Fac"], (-900, 1300 + 100 * v)), (-800, 1300))
    plate_k = math_("MULTIPLY", is_(FEAT_PLATE), lo_(FEAT_PLATE), (-700, 1100))
    horn_k = math_("MULTIPLY", is_(FEAT_HORN), lo_(FEAT_HORN), (-700, 1200))
    eye_k = is_(FEAT_EYE)
    col = mix(col, [c * 0.85 + a * 0.15 for c, a in zip(C["accent"], C["base"])], plate_k, (200, -150))
    col = mix(col, [0.13, 0.11, 0.09], horn_k, (350, -150))
    col = mix(col, [0.02, 0.018, 0.015], eye_k, (500, -150))
    # Scales: cell edges from a Voronoi, finer wrinkles from noise.
    sv = node(nt, "ShaderNodeTexVoronoi", (-1100, -800), feature="SMOOTH_F1"); sv.inputs["Scale"].default_value = 1.0 / C["scale_size"]
    sv.inputs["Smoothness"].default_value = 0.3; link(nt, obj, sv.inputs["Vector"])
    wn = node(nt, "ShaderNodeTexNoise", (-1100, -1000)); wn.inputs["Scale"].default_value = 0.4 / C["scale_size"]; link(nt, obj, wn.inputs["Vector"])
    height = math_("ADD", mapr(0.0, 0.45, sv.outputs["Distance"], (-900, -800)), math_("MULTIPLY", wn.outputs["Fac"], 0.35, (-900, -1000)), (-700, -900))
    height = math_("MULTIPLY", height, math_("SUBTRACT", 1.0, eye_k, (-700, -1100)), (-600, -900))
    bump = node(nt, "ShaderNodeBump", (-400, -900)); bump.inputs["Strength"].default_value = 0.55; bump.inputs["Distance"].default_value = C["scale_size"] * 0.25
    link(nt, height, bump.inputs["Height"])
    rough = math_("ADD", 0.68, math_("MULTIPLY", height, -0.18, (-400, -1100)), (-200, -1100))
    rough = math_("ADD", rough, math_("MULTIPLY", eye_k, -0.6, (-300, -1200)), (-100, -1150))
    rough = math_("ADD", rough, math_("MULTIPLY", horn_k, -0.25, (-300, -1300)), (0, -1200))
    bsdf = node(nt, "ShaderNodeBsdfPrincipled", (1000, 0))
    link(nt, col, bsdf.inputs["Base Color"]); link(nt, bump.outputs["Normal"], bsdf.inputs["Normal"]); link(nt, rough, bsdf.inputs["Roughness"])
    emit = node(nt, "ShaderNodeEmission", (1000, 400))
    link(nt, bsdf.outputs[0], out.inputs["Surface"])
    return mat, dict(color=col, rough=rough, bsdf=bsdf, emit=emit, out=out)


def bake_textures(ob, workdir):
    """Albedo and roughness (as emission), tangent-space normal and AO, baked at TEX."""
    scene.render.engine = "CYCLES"; scene.cycles.device = "CPU"; scene.cycles.samples = 4
    scene.render.bake.margin = 6
    mat, S_ = pattern_material(); nt = mat.node_tree
    ob.data.materials.clear(); ob.data.materials.append(mat)
    imgs = {}
    def target(name, color):
        img = bpy.data.images.new(name, TEX, TEX, alpha=False, float_buffer=False)
        img.colorspace_settings.name = "sRGB" if color else "Non-Color"
        tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = img; nt.nodes.active = tn; tn.select = True
        imgs[name] = img; return tn
    bpy.ops.object.select_all(action="DESELECT"); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    def emit_of(sock):
        for l in list(nt.links):
            if l.to_node == S_["emit"]: nt.links.remove(l)
        link(nt, sock, S_["emit"].inputs["Color"]); link(nt, S_["emit"].outputs[0], S_["out"].inputs["Surface"])
    emit_of(S_["color"]); target("albedo", True); bpy.ops.object.bake(type="EMIT")
    emit_of(S_["rough"]); target("rough", False); bpy.ops.object.bake(type="EMIT")
    link(nt, S_["bsdf"].outputs[0], S_["out"].inputs["Surface"])
    target("normal", False); bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT")
    scene.cycles.samples = 48; scene.world = scene.world or bpy.data.worlds.new("w")
    target("ao", False); bpy.ops.object.bake(type="AO")
    px = {k: np.array(v.pixels[:], np.float32).reshape(TEX, TEX, 4) for k, v in imgs.items()}
    arm = np.ones((TEX, TEX, 4), np.float32)
    arm[..., 0] = 0.35 + 0.65 * px["ao"][..., 0]; arm[..., 1] = px["rough"][..., 0]; arm[..., 2] = 0.0
    files = {}
    for name, data, cs in (("albedo", px["albedo"], "sRGB"), ("normal", px["normal"], "Non-Color"), ("arm", arm, "Non-Color")):
        img = bpy.data.images.new("tex_" + name, TEX, TEX, alpha=False); img.colorspace_settings.name = cs
        img.pixels.foreach_set(data.ravel()); path = os.path.join(workdir, name + ".png")
        img.filepath_raw = path; img.file_format = "PNG"; img.save(); files[name] = img
    for v in imgs.values(): bpy.data.images.remove(v)
    bpy.data.materials.remove(mat)
    return files


def export_material(ob, files):
    """Principled material over the baked images; ARM drives roughness, metal and (glTF) occlusion."""
    mat = bpy.data.materials.new("hide"); mat.use_nodes = True; nt = mat.node_tree; nt.nodes.clear()
    out = node(nt, "ShaderNodeOutputMaterial", (600, 0)); bsdf = node(nt, "ShaderNodeBsdfPrincipled", (300, 0))
    link(nt, bsdf.outputs[0], out.inputs[0])
    ta = node(nt, "ShaderNodeTexImage", (-400, 200)); ta.image = files["albedo"]; link(nt, ta.outputs["Color"], bsdf.inputs["Base Color"])
    tn = node(nt, "ShaderNodeTexImage", (-400, -200)); tn.image = files["normal"]
    nm = node(nt, "ShaderNodeNormalMap", (0, -200)); link(nt, tn.outputs["Color"], nm.inputs["Color"]); link(nt, nm.outputs[0], bsdf.inputs["Normal"])
    tr = node(nt, "ShaderNodeTexImage", (-400, -500)); tr.image = files["arm"]
    sep = node(nt, "ShaderNodeSeparateColor", (-100, -500)); link(nt, tr.outputs["Color"], sep.inputs[0])
    link(nt, sep.outputs["Green"], bsdf.inputs["Roughness"]); link(nt, sep.outputs["Blue"], bsdf.inputs["Metallic"])
    bsdf.inputs["Specular IOR Level"].default_value = 0.4
    grp = bpy.data.node_groups.get("glTF Material Output") or bpy.data.node_groups.new("glTF Material Output", "ShaderNodeTree")
    if not grp.interface.items_tree: grp.interface.new_socket("Occlusion", in_out="INPUT", socket_type="NodeSocketFloat")
    gn = nt.nodes.new("ShaderNodeGroup"); gn.node_tree = grp; gn.location = (300, -400)
    link(nt, sep.outputs["Red"], gn.inputs["Occlusion"])
    ob.data.materials.clear(); ob.data.materials.append(mat)
    return mat


# ---------------------------------------------------------------- posing
# A pose is built from a State: the root's offset and rotation (about the
# hips joint), extra rotations per bone (in rest axes, about the bone's own
# joint, carried by its parent's motion) and, per leg, either an IK target
# (the ball of the foot, the metatarsus angle, the toes' pitch) or nothing,
# in which case the leg is posed by its rotations like any other bone.
LEG_KEYS = sorted(LEGS)
LEG_OF = {n: k for k, names in LEGS.items() for n in names}
LEN = {n: (BONE[n][3] - BONE[n][2]).length for n in ORDER}
HEAD = {n: BONE[n][2] for n in ORDER}
NECK = ["neck_%02d" % (k + 1) for k in range(sp["neck_joints"])]
TAIL = ["tail_%02d" % (k + 1) for k in range(sp["tail_joints"])]
TORSO = ["hips"] + ["spine_%02d" % (k + 1) for k in range(sp["torso_joints"] - 1)]


def rest_phi(key):
    A, B = HEAD[LEGS[key][2]], HEAD[LEGS[key][3]]
    return math.atan2(-(A.x - B.x), A.z - B.z)
REST_FOOT = {k: (HEAD[LEGS[k][3]].copy(), rest_phi(k)) for k in LEG_KEYS}
# On which side of the hip-to-ball line each leg's ankle sits (+1 behind it,
# as in the hind legs), and the metatarsus' standing angle to that line.
def line_phi(H, B): return math.atan2(-(H.x - B.x), H.z - B.z)
LEG_BRANCH = {k: 1 if REST_FOOT[k][1] >= line_phi(HEAD[LEGS[k][0]], REST_FOOT[k][0]) else -1 for k in LEG_KEYS}
REACH_CAP = {k: max(0.95 * (LEN[LEGS[k][0]] + LEN[LEGS[k][1]]), (HEAD[LEGS[k][2]] - HEAD[LEGS[k][0]]).length + 0.01 * (LEN[LEGS[k][0]] + LEN[LEGS[k][1]])) for k in LEG_KEYS}
REST_GAMMA = {k: abs(REST_FOOT[k][1] - line_phi(HEAD[LEGS[k][0]], REST_FOOT[k][0])) for k in LEG_KEYS}


def keep_reach(st, key, H, B):
    """The metatarsus angle of a planted leg, the ball at B and the hip at H.
    st.legs[key][3] is either "bend": the metatarsus keeps its standing angle
    to the hip-to-ball line and the knee takes up the change of reach (a
    walk, many frames to a stance); or a hip-to-ankle distance d the ankle
    keeps, the metatarsus rolling about the ball instead (a vaulting run, two
    or three frames to a stance, where a working knee would turn too fast). In
    both the knee stays below 95% of the leg's reach (where a small change of
    reach turns it fast) or its standing extension if that is straighter (the
    stegosaur's pillar legs). st.fit records whether that holds."""
    up, lo, ft = LEGS[key][:3]; m = LEN[ft]; w = B - H; spec = st.legs[key][3]
    cap = REACH_CAP[key]
    if spec == "bend":
        phi = line_phi(H, B) + LEG_BRANCH[key] * REST_GAMMA[key]
        A = B + Vector((-math.sin(phi), 0, math.cos(phi))) * m
        st.fit[key] = (A - H).length <= cap
    else:
        # |B + m (-sin phi, 0, cos phi) - H|^2 = d^2 gives cos(phi - line) = c / R.
        c = (w.length_squared + m * m - spec * spec) / (2 * m); R = math.hypot(w.x, w.z)
        st.fit[key] = abs(c) <= R and spec <= cap
        phi = math.pi - math.atan2(w.x, w.z) + LEG_BRANCH[key] * math.acos(min(max(c / R, -1.0), 1.0))
    phi = math.atan2(math.sin(phi), math.cos(phi))
    st.solved[key] = phi
    return phi


class State:
    def __init__(self):
        self.off = Vector((0, 0, 0)); self.root = Matrix.Identity(4); self.rot = {}
        # Per leg: ball, metatarsus angle phi, toe pitch (tip down +) and, when
        # phi is None, the hip-to-ankle distance pose() solves phi for.
        self.legs = {k: [REST_FOOT[k][0].copy(), REST_FOOT[k][1], 0.0] for k in LEG_KEYS}
        self.solved, self.fit = {}, {}     # phi pose() solved per leg, and whether the distance was reachable

    def turn(self, bone, R):
        self.rot[bone] = R @ self.rot.get(bone, Matrix.Identity(4))


def aim(carry, head, d):
    """The carried bone frame `carry` re-aimed along d with the least turn, placed at head."""
    R = carry.to_3x3(); cur = R @ Vector((0, 1, 0))
    Q = cur.rotation_difference(d.normalized()).to_matrix()
    M = (Q @ R).to_4x4(); M.translation = head
    return M


def pose(st):
    """Armature-space matrices of every bone for a State."""
    D, P = {}, {}
    for n in ORDER:
        par = PARENT[n]
        Dp = (Matrix.Translation(st.off) @ about(HEAD["hips"], st.root)) if par is None else D[par]
        key = LEG_OF.get(n)
        if key is None or st.legs.get(key) is None or n != LEGS[key][0]:
            if key is not None and st.legs.get(key) is not None: continue   # placed with its leg below
            D[n] = Dp @ about(HEAD[n], st.rot.get(n, Matrix.Identity(4))); P[n] = D[n] @ REST[n]
            continue
        # An IK leg: hip from the parent's motion, then knee, ankle, ball and toes.
        up, lo, ft, toe = LEGS[key]
        B, phi, toe_pitch = st.legs[key][:3]
        H = Dp @ HEAD[up]
        if phi is None: phi = keep_reach(st, key, H, B)
        front = key.endswith("F")
        pole = (Dp.to_3x3() @ Vector((-1 if front else 1, 0, 0)))
        K, A = solve_leg(H, B, LEN[up], LEN[lo], LEN[ft], phi, pole)
        P[up] = aim(Dp @ REST[up], H, K - H); D[up] = P[up] @ REST[up].inverted()
        P[lo] = aim(D[up] @ REST[lo], K, A - K); D[lo] = P[lo] @ REST[lo].inverted()
        P[ft] = aim(D[lo] @ REST[ft], A, B - A); D[ft] = P[ft] @ REST[ft].inverted()
        td = Vector((math.cos(toe_pitch), 0, -math.sin(toe_pitch)))
        P[toe] = aim(D[ft] @ REST[toe], A + (B - A).normalized() * LEN[ft], td); D[toe] = P[toe] @ REST[toe].inverted()
    return P


def basis_of(P):
    out = {}
    for n in ORDER:
        par = PARENT[n]
        out[n] = REST[n].inverted() @ P[n] if par is None else REST[n].inverted() @ REST[par] @ P[par].inverted() @ P[n]
    return out


def tip(P, n):
    return P[n] @ Vector((0, LEN[n], 0))


def spread(st, names, R_of_share):
    """Spread a turn evenly over a chain: R_of_share(1/n) on each joint."""
    for n in names: st.turn(n, R_of_share(1.0 / len(names)))


def neck_bend(st, pitch, yaw=0.0, head_share=0.25):
    """Bend the neck (nose up positive) and turn it, the head taking a share."""
    k = 1 - head_share
    spread(st, NECK, lambda f: yaw_left(yaw * k * f) @ nose_up(pitch * k * f))
    st.turn("head", yaw_left(yaw * head_share) @ nose_up(pitch * head_share))


def tail_bend(st, raise_, swing, wave=None):
    """Raise the tail (tip up positive) and swing it (tip to the left positive);
    wave(j) adds a per-joint swing (a travelling wave)."""
    n = len(TAIL)
    for j, t in enumerate(TAIL):
        sw = swing / n + (wave(j) if wave else 0.0)
        st.turn(t, rot_z(-sw) @ rot_y(raise_ / n))


def snout_drop(st_fn, target_z):
    """The neck pitch (negative) that brings the snout to target_z, for a State builder st_fn(pitch)."""
    lo, hi = -2.2, 0.0
    for _ in range(26):
        mid = 0.5 * (lo + hi); z = tip(pose(st_fn(mid)), "head").z
        if z < target_z: lo = mid
        else: hi = mid
    return 0.5 * (lo + hi)


# ---------------------------------------------------------------- clips
G_ = SPEC["gait"]
ATTACK = SPEC["attack"]
CLIPS = {}      # name -> (states, loop)
SPEED_OF = {}   # gait clip -> ground speed (m/s)
GAIT_INFO = {}


def gait_phases(gi):
    if not QUAD: return {"LH": 0.0, "RH": 0.5}
    if gi == 1: return {"LH": 0.0, "RF": 0.0, "RH": 0.5, "LF": 0.5}             # trot: diagonal pairs
    return {"LH": 0.0, "LF": 0.25, "RH": 0.5, "RF": 0.75}                       # walk and amble: lateral sequence


def foot_swing(key, s, beta, stride, v, T, lift, ease):
    """Ball position and toe pitch of one foot at swing fraction s (0: lift-off,
    1: touchdown). The ball leaves and lands moving back at exactly the ground
    speed (the stance's velocity, so a foot never skids as it lands or lifts)
    and returns along a smooth arc; the match is eased in and out over the
    first and last `ease` of the swing, so a fast gait's foot runs on past the
    stance by little more than a frame's travel. It rises at once, as a foot
    peels off heel first (one that lingered just above the ground would scuff
    it), and the toes curl in the air."""
    B0 = REST_FOOT[key][0]
    sweep = beta * stride; M = (1 - beta) * v * T          # ground speed in swing-fraction units
    def w(x): return x * (1 - x / ease) ** 2 if x < ease else 0.0   # w(0) = 0, w'(0) = 1, flat from `ease` on
    x = B0.x - 0.5 * sweep + sweep * smooth(s) - M * (w(s) - w(1 - s))
    z = B0.z + lift * math.sin(math.pi * s) ** 0.5
    return Vector((x, B0.y, z)), 0.5 * math.sin(math.pi * s) ** 1.5


def hermite(s, p0, m0, p1, m1):
    return (2 * s ** 3 - 3 * s ** 2 + 1) * p0 + (s ** 3 - 2 * s ** 2 + s) * m0 + (-2 * s ** 3 + 3 * s ** 2) * p1 + (s ** 3 - s ** 2) * m1


def max_step(states):
    """The largest local rotation (degrees) of any joint between two frames,
    as tools/animcheck.c measures it, and the joint."""
    frames = [basis_of(pose(s)) for s in states]; worst = (0.0, "")
    for a, b in zip(frames, frames[1:]):
        for n in ORDER:
            d = abs(a[n].to_quaternion().dot(b[n].to_quaternion()))
            ang = 2 * math.degrees(math.acos(min(d, 1.0)))
            if ang > worst[0]: worst = (ang, n)
    return worst


def gait_clip(gi):
    """One gait cycle at the sim's speed. Planted feet hold still on the
    ground (the clip is in place, so a planted ball moves back at exactly the
    ground speed). In stance the knee holds a nearly constant bend and the
    metatarsus rolls to keep the ball where it is: at 30 frames a second a
    run's stance is two or three frames, so a knee that flexed and straightened
    in it would turn faster than any clip should. In the swing the metatarsus
    angle runs smoothly from its value at lift-off to its value at the next
    touchdown."""
    v = SPEEDS[gi]; h = HIP_H
    lift = G_["lift"][gi] * h * 2.5; bob = G_["bob"][gi] * h * 2.5
    crouch = G_["crouch"][gi] * h; lean = math.radians(G_["lean_deg"][gi])
    phases = gait_phases(gi); running = G_["duty"][gi] < 0.5
    first = LEG_KEYS[0]
    drop = [0.0]       # extra lowering of the hips the stance needs to reach (searched below)
    def stance_d(k, u, beta):
        """How a planted leg is solved (keep_reach): a vaulting leg holds its
        ankle at 92% of its reach, a walking one bends its knee."""
        return 0.92 * (LEN[LEGS[k][0]] + LEN[LEGS[k][1]]) if vault else "bend"

    # A biped's run and trot vault: the hips ride over the planted foot at the
    # height that keeps the leg's shape (the hip-to-ball distance r0 of the
    # stance pose), so in a stance of two or three frames only the hip joint
    # turns; between steps they follow a smooth arc with the gait's bob on top.
    vault = running and not QUAD
    def r0_of(k):
        d = 0.92 * (LEN[LEGS[k][0]] + LEN[LEGS[k][1]]); m = LEN[LEGS[k][2]]
        H0, (B0, phi0) = HEAD[LEGS[k][0]], REST_FOOT[k]
        g_ = abs(phi0 - line_phi(H0, B0))
        return m * math.cos(g_) + math.sqrt(d * d - (m * math.sin(g_)) ** 2)
    R0 = {k: r0_of(k) for k in phases}

    def vault_z(k, u, T, beta):
        """The hips' offset that keeps planted leg k at its stance reach at stance fraction u."""
        B = stance_ball(k, u, T, beta); H0 = HEAD[LEGS[k][0]]
        return B.z + math.sqrt(max(R0[k] ** 2 - (H0.x - B.x) ** 2 - (H0.y - B.y) ** 2, 1e-6)) - H0.z

    def hips_z(t, T, beta):
        """The hips' vertical offset at time t: vaulting over the planted foot, or in the air between steps."""
        if not vault:
            # Highest at the first leg's mid-stance in a walk, lowest in a run;
            # never above the standing height, which a straight-legged animal
            # could not reach.
            c2 = math.cos(4 * math.pi * (t / T - beta / 2 - phases[first]))
            return bob * ((c2 - 1) if not running else (-c2 - 1)) / 2
        lo = td = None
        for k, ph in phases.items():
            u = (t / T - ph) % 1.0
            if u < beta: return vault_z(k, u, T, beta)
            since, until = u - beta, 1.0 - u          # cycle fractions since lift-off and until touchdown
            if lo is None or since < lo[0]: lo = (since, k)
            if td is None or until < td[0]: td = (until, k)
        span = lo[0] + td[0]; s_ = lo[0] / span; e = 1e-3
        z_lo, z_td = vault_z(lo[1], beta, T, beta), vault_z(td[1], 0.0, T, beta)
        dz_lo = (z_lo - vault_z(lo[1], beta - e, T, beta)) / e * span
        dz_td = (vault_z(td[1], e, T, beta) - z_td) / e * span
        return hermite(s_, z_lo, dz_lo, z_td, dz_td) + bob * math.sin(math.pi * s_) ** 2

    def body(t, T, beta):
        """The State of everything above the legs at time t of a cycle T long."""
        ph = 2 * math.pi * (t / T - beta / 2 - phases[first])   # 0: the first leg at mid-stance
        st = State(); c1 = math.cos(ph); c2 = math.cos(2 * ph)
        st.off.z += -crouch - drop[0] + hips_z(t, T, beta)
        yaw = math.radians(4.0 if not QUAD else 2.0) * c1 * (0.6 if running else 1.0)
        roll = math.radians(2.5 if not QUAD else 1.5) * c1
        st.off.y += (0.012 if not QUAD else 0.004) * h * c1 * (0.4 if running else 1.0)
        st.root = yaw_left(yaw) @ roll_left_down(roll) @ nose_up(-lean + (0.012 if running else -0.008) * c2)
        # The chest counter-turns, the neck holds the head steady, the tail swings against the hips.
        spread(st, TORSO[1:], lambda f_: yaw_left(-0.6 * yaw * f_) @ roll_left_down(-0.5 * roll * f_))
        neck_bend(st, lean * 0.6 - (0.01 if running else -0.01) * c2, -0.5 * yaw)
        tail_bend(st, -0.02 * c2 + lean * 0.3, 0.0, lambda j: -1.6 * yaw * math.cos(ph - 0.35 * j) / len(TAIL))
        if not QUAD:
            for sd, sg in (("L", 1), ("R", -1)):
                st.turn("upperarm." + sd, nose_up(sg * 0.08 * c1 + (0.5 if running else 0.15)))
                st.turn("lowerarm." + sd, nose_up(0.3 if running else 0.1))
        return st

    def stance_ball(k, u, T, beta):
        B0 = REST_FOOT[k][0]
        return Vector((B0.x + v * T * beta * (0.5 - u / beta), B0.y, B0.z))

    def stance_phi(k, u, T, beta):
        """The metatarsus angle the planted leg k takes at stance fraction u, and whether it fits."""
        st = body(((phases[k] + u) % 1.0) * T, T, beta)
        st.legs[k] = [stance_ball(k, u, T, beta), None, 0.0, stance_d(k, u, beta)]
        P = pose(st)
        # The metatarsus stays within 50 degrees of its standing angle.
        ok = st.fit[k] and (tip(P, LEGS[k][2]) - st.legs[k][0]).length < 1e-4 and abs(st.solved[k] - REST_FOOT[k][1]) < math.radians(50)
        return st.solved[k], ok

    def fits(T, beta):
        return all(stance_phi(k, u, T, beta)[1] for k in phases for u in np.linspace(0, beta, 7))

    def cycle(T, beta, lift):
        ends = {}
        for k in phases:
            e = 1e-3
            p_lo, p_td = stance_phi(k, beta, T, beta)[0], stance_phi(k, 0.0, T, beta)[0]
            d_lo = (p_lo - stance_phi(k, beta - e, T, beta)[0]) / e * (1 - beta)
            d_td = (stance_phi(k, e, T, beta)[0] - p_td) / e * (1 - beta)
            ends[k] = (p_lo, d_lo, p_td, d_td)
        states = []
        for f in range(int(round(T * FPS)) + 1):
            t = f / FPS; st = body(t, T, beta)
            for k, ph in phases.items():
                u = (t / T - ph) % 1.0
                if u < beta:
                    st.legs[k] = [stance_ball(k, u, T, beta), None, 0.0, stance_d(k, u, beta)]
                else:
                    s = (u - beta) / (1 - beta); B, toe = foot_swing(k, s, beta, v * T, v, T, lift, min(0.4, 2.5 / ((1 - beta) * T * FPS)))
                    st.legs[k] = [B, hermite(s, *ends[k]), toe]
            states.append(st)
        return states

    # Stride from leg length (Alexander 1976: stride / hip height = 2.3 (v^2 / g h)^0.3),
    # in whole frames. For each stride the hips drop as little as lets every
    # planted leg reach its ball through the stance (at most 6% of the hip
    # height: a straight-legged animal such as the stegosaur cannot reach
    # further at a standing height, and more would read as a crouch). When no
    # drop fits, the stride shortens, except in a biped's run, which keeps its
    # stride (and so its slow swing) and shortens the time each foot is down.
    stride = h * 2.3 * (v * v / (9.81 * h)) ** 0.3; beta = G_["duty"][gi]
    if vault:
        # A vaulting leg sweeps at most 25 degrees either side of the vertical:
        # further, and the hips would rise and fall too much over each step.
        N = max(8, int(round(FPS * stride / v)))
        beta = min(beta, 2 * min(R0.values()) * math.sin(math.radians(25)) / (v * N / FPS))
    for _ in range(60):
        N = max(8, int(round(FPS * stride / v))); T = N / FPS
        for drop[0] in np.linspace(0.0, 0.06 * h, 7):
            if fits(T, beta): break
        else:
            drop[0] = 0.06 * h
            if vault and beta > 0.12: beta *= 0.96; continue
            if N > 8: stride *= 0.96; continue
        break
    # The swing's lift is lowered until no joint turns more than 29 degrees in a
    # frame (animcheck's limit is 30): a fast swing folds the knee quickly.
    for _ in range(12):
        states = cycle(T, beta, lift)
        step = max_step(states)
        if step[0] <= 29.0: break
        lift *= 0.85
    name = ("walk", "trot", "run")[gi]; stride = v * T
    CLIPS[name] = (states, True); SPEED_OF[name] = v
    GAIT_INFO[name] = dict(speed=v, stride=stride, period=T, duty=beta, frames=N, lift=lift, drop=drop[0], step=step[0])
    print("dino_build: %s %.2f m/s: stride %.2f m, cycle %.2f s (%d frames), duty %.2f, hips %.2f m lower, lift %.2f m, largest step %.1f deg (%s)"
          % (name, v, stride, T, N, beta, crouch + drop[0], lift, step[0], step[1]))


def standing(t, T, breathe=1.0):
    """The resting stance with breathing (chest and belly rise, the hips settle slightly)."""
    st = State(); b = math.sin(2 * math.pi * t / T)
    st.off.z += -0.004 * HIP_H * b * breathe
    spread(st, TORSO[1:], lambda f: nose_up(0.006 * b * breathe * f))
    tail_bend(st, 0.01 * b * breathe, 0.0)
    return st


def idle_clips():
    T = 4.0; N = int(T * FPS); states = []
    for f in range(N + 1):
        t = f / FPS; st = standing(t, T)
        neck_bend(st, 0.04 * math.sin(2 * math.pi * t / T + 0.6), 0.12 * math.sin(2 * math.pi * t / T))
        tail_bend(st, 0.0, 0.08 * math.sin(2 * math.pi * t / T + 1.0))
        st.turn("jaw", nose_up(-0.02 * (0.5 + 0.5 * math.sin(2 * math.pi * t / T))))
        states.append(st)
    CLIPS["idle"] = (states, True)
    # idle2: looks left, right and up, a shake of the head, the tail flicking.
    T = 6.0; N = int(T * FPS); states = []
    for f in range(N + 1):
        t = f / FPS; st = standing(t, T / 2)
        look = 0.45 * math.sin(2 * math.pi * t / T) * smooth(min(t, T - t) / 0.8)
        up = 0.15 * math.sin(4 * math.pi * t / T) ** 2
        shake = 0.12 * math.sin(2 * math.pi * 7 * t / T) * math.exp(-((t - 0.72 * T) / 0.25) ** 2)
        neck_bend(st, up, look + shake, 0.35)
        st.turn("head", roll_left_down(shake * 0.8))
        tail_bend(st, 0.03 * math.sin(2 * math.pi * t / T), -0.5 * look + 0.15 * math.sin(6 * math.pi * t / T))
        states.append(st)
    CLIPS["idle2"] = (states, True)


def head_down_state(body_pitch, crouch, pitch, yaw=0.0):
    st = State(); st.off.z -= crouch; st.root = nose_up(-body_pitch)
    tail_bend(st, body_pitch * 0.8, 0.0)
    neck_bend(st, pitch, yaw, 0.3)
    return st


def feed_clips():
    carn = SPEC["attack"]["kind"] == "bite"
    bp, cr = (0.0, 0.06 * HIP_H) if QUAD else (0.16, 0.14 * HIP_H)
    reach = snout_drop(lambda p: head_down_state(bp, cr, p), 0.04 * HIP_H)
    for name, T in (("eat", 3.0), ("drink", 3.0)):
        N = int(T * FPS); states = []
        for f in range(N + 1):
            t = f / FPS; ph = 2 * math.pi * t / T
            if name == "eat" and carn:      # tearing at a carcass: tug up and sideways, jaws working
                tug = max(0.0, math.sin(ph)) ** 2
                st = head_down_state(bp + 0.04 * tug, cr, reach + 0.25 * tug, 0.18 * math.sin(2 * ph) * (0.3 + tug))
                st.turn("jaw", nose_up(-(0.12 + 0.22 * (0.5 + 0.5 * math.sin(6 * ph)) * (1 - tug))))
            elif name == "eat":             # cropping plants: small nods and chewing
                st = head_down_state(bp, cr, reach + 0.05 + 0.05 * math.sin(2 * ph), 0.12 * math.sin(ph))
                st.turn("jaw", nose_up(-(0.04 + 0.08 * (0.5 + 0.5 * math.sin(8 * ph)))))
            else:                           # drinking: the head steady, the jaw lapping
                st = head_down_state(bp, cr, reach + 0.02 * math.sin(4 * ph), 0.0)
                st.turn("jaw", nose_up(-(0.04 + 0.06 * (0.5 + 0.5 * math.sin(6 * ph)))))
            st.off.z -= 0.003 * HIP_H * math.sin(ph)
            states.append(st)
        CLIPS[name] = (states, True)


def call_clip():
    T = 2.4; N = int(T * FPS); states = []
    for f in range(N + 1):
        t = f / FPS; k = smooth(t / 0.45) * (1 - smooth((t - 1.8) / 0.6)); o = smooth((t - 0.35) / 0.25) * (1 - smooth((t - 1.6) / 0.4))
        st = State(); st.root = nose_up(0.06 * k)
        spread(st, TORSO[1:], lambda f_: nose_up(0.04 * k * f_))
        neck_bend(st, (0.35 if not QUAD else 0.25) * k, 0.03 * math.sin(t * 19) * o, 0.35)
        st.turn("jaw", nose_up(-0.55 * o))
        tail_bend(st, 0.1 * k, 0.0)
        states.append(st)
    CLIPS["call"] = (states, False)


def attack_clip():
    kind = ATTACK["kind"]; hit = SIM["hit"] if ATTACK["hit"] == "sim" else ATTACK["hit"]; T = ATTACK["duration"]
    N = int(round(T * FPS)); states = []
    for f in range(N + 1):
        t = f / FPS; st = State()
        wind = smooth((t - (hit - 0.45)) / 0.3) * (1 - smooth((t - hit + 0.12) / 0.1))   # 1 through the wind-up
        strike = math.exp(-((t - hit) / 0.1) ** 2)                                        # peaks on the hit
        rec = 1 - smooth((t - hit) / (T - hit))
        if kind == "bite":
            st.off.x += HIP_H * (-0.06 * wind + 0.14 * strike); st.off.z -= HIP_H * 0.05 * (wind + strike)
            st.root = nose_up(0.05 * wind - 0.1 * strike)
            neck_bend(st, 0.3 * wind - 0.35 * strike, 0.0, 0.3)
            o = smooth((t - (hit - 0.4)) / 0.3) * (1 - smooth((t - hit + 0.04) / 0.05))
            st.turn("jaw", nose_up(-0.6 * o))
            tail_bend(st, -0.05 * wind + 0.12 * strike, 0.0)
        elif kind == "headbutt":
            down = smooth((t - (hit - 0.7)) / 0.4) * (1 - smooth((t - hit - 0.2) / (T - hit - 0.2)))
            st.off.x += HIP_H * (-0.05 * wind + 0.18 * strike); st.off.z -= HIP_H * 0.06 * down
            st.root = nose_up(-0.12 * down - 0.05 * strike)
            neck_bend(st, -0.55 * down, 0.0, 0.5)
            tail_bend(st, 0.12 * down, 0.0)
        elif kind == "tail":
            up = smooth(t / 0.45) * rec
            side = -0.55 * smooth((t - 0.15) / 0.35) * (1 - smooth((t - 0.6) / 0.2)) + 0.45 * strike * 1.6 * rec ** 0.5
            st.root = yaw_left(-0.12 * side); tail_bend(st, 0.25 * up, side)
            neck_bend(st, -0.08 * up, 0.15 * side)
        elif kind == "bash":
            st.off.x += HIP_H * (-0.06 * wind + 0.22 * strike); st.root = roll_left_down(0.18 * strike - 0.05 * wind) @ nose_up(-0.06 * strike)
            neck_bend(st, -0.25 * (wind + strike), 0.2 * strike)
            tail_bend(st, 0.0, -0.3 * strike)
        else:   # kick: the right foot strikes forward and up, the body leans back over the left
            k = smooth((t - (hit - 0.18)) / 0.18) * (1 - smooth((t - hit - 0.08) / (T - hit - 0.15)))
            B0, phi0 = REST_FOOT["RH"]
            st.legs["RH"] = [B0 + Vector((0.45 * HIP_H * k, 0, 0.35 * HIP_H * k)), phi0 + 0.6 * k, -0.3 * k]
            st.off.y += 0.04 * HIP_H * k; st.off.z -= 0.05 * HIP_H * k
            st.root = nose_up(0.12 * k) @ roll_left_down(0.04 * k)
            neck_bend(st, -0.1 * k, 0.0); tail_bend(st, -0.1 * k, 0.0)
        states.append(st)
    CLIPS["attack"] = (states, False)
    print("dino_build: attack (%s) %.2f s, hit at %.2f s" % (kind, T, hit))


def hurt_clip():
    T = 0.8; N = int(T * FPS); states = []
    for f in range(N + 1):
        # The flinch rises from the standing pose and settles back into it.
        t = f / FPS; k = (math.exp(-((t - 0.18) / 0.12) ** 2) + 0.4 * math.exp(-((t - 0.4) / 0.15) ** 2)) * smooth(t / 0.12) * (1 - smooth((t - 0.5) / 0.3))
        st = State(); st.off.x -= 0.03 * HIP_H * k; st.off.z -= 0.04 * HIP_H * k
        st.root = roll_left_down(0.06 * k) @ nose_up(0.04 * k)
        neck_bend(st, 0.25 * k, 0.25 * k, 0.4); st.turn("jaw", nose_up(-0.35 * k))
        tail_bend(st, 0.1 * k, -0.25 * k)
        states.append(st)
    CLIPS["hurt"] = (states, False)


def jump_clip():
    T = 1.2; N = int(T * FPS); states = []
    for f in range(N + 1):
        t = f / FPS; st = State()
        crouch = smooth(t / 0.18) * (1 - smooth((t - 0.18) / 0.12)) + 0.8 * smooth((t - 0.93) / 0.12) * (1 - smooth((t - 1.05) / 0.15))
        # The legs tuck over 0.3 s each way (a faster tuck turns the knees too fast).
        air = smooth((t - 0.22) / 0.3) * (1 - smooth((t - 0.72) / 0.3))
        st.off.z -= 0.16 * HIP_H * crouch
        st.root = nose_up(0.08 * air - 0.06 * crouch)
        neck_bend(st, -0.1 * air + 0.05 * crouch, 0.0); tail_bend(st, 0.15 * air, 0.0)
        if air > 0:
            for k in LEG_KEYS:
                B0, phi0 = REST_FOOT[k]
                st.legs[k] = [B0 + Vector((0.12 * HIP_H * air, 0, 0.32 * HIP_H * air)), phi0 - 0.5 * air, 0.5 * air]
        states.append(st)
    CLIPS["jump"] = (states, False)


def fk_legs(st, fold):
    """Legs posed by rotation (lying, dead): fold 0 straight as at rest .. 1 tucked."""
    for k in LEG_KEYS:
        st.legs[k] = None
        up, lo, ft, toe = LEGS[k]; sg = -1 if k.endswith("F") else 1
        st.turn(up, nose_up(sg * 1.0 * fold)); st.turn(lo, nose_up(-sg * 2.0 * fold))
        st.turn(ft, nose_up(sg * 1.3 * fold)); st.turn(toe, nose_up(-0.3 * fold))


def ground_offset(st):
    """How far the skinned mesh in pose `st` dips below z = 0 (positive: needs lifting)."""
    apply_pose(basis_of(pose(st)))
    dg = bpy.context.evaluated_depsgraph_get(); ev = BODY.evaluated_get(dg); me = ev.to_mesh()
    z = min(v.co.z for v in me.vertices); ev.to_mesh_clear()
    return -z


def sleep_clip():
    T = 4.0; N = int(T * FPS)
    def lying(drop, side=1.0):
        st = State(); st.off.z -= drop; fk_legs(st, 1.0 if not QUAD else 0.8)
        neck_bend(st, -0.35, 0.45 * side, 0.3); tail_bend(st, 0.0, -0.6 * side)
        return st
    # Sink until the belly and folded legs rest on the ground.
    lo, hi = 0.0, HIP_H
    for _ in range(14):
        mid = 0.5 * (lo + hi)
        if ground_offset(lying(mid)) > 0: hi = mid
        else: lo = mid
    drop = lo
    hz = tip(pose(lying(drop)), "head").z
    states = []
    for f in range(N + 1):
        t = f / FPS; b = math.sin(2 * math.pi * t / T)
        st = lying(drop)
        spread(st, TORSO[1:], lambda f_: nose_up(0.01 * b * f_))
        # The head rests: pitch it down onto the ground beside the body.
        st.turn("head", nose_up(-0.15 - 0.01 * b))
        states.append(st)
    CLIPS["sleep"] = (states, True)
    print("dino_build: sleep: body lowered %.2f m (head %.2f m up)" % (drop, hz))


def death_clip():
    T = 2.6; N = int(T * FPS)
    def dead(k, s):
        st = State(); fk_legs(st, 0.25 * k)
        st.root = roll_left_down(1.45 * k); neck_bend(st, -0.2 * k + 0.25 * s, 0.4 * k, 0.3)
        tail_bend(st, -0.05 * k, -0.3 * k); st.turn("jaw", nose_up(-0.25 * k))
        return st
    # Lying on its left side: lift the body until it rests on the ground.
    lift = ground_offset(dead(1.0, 0.0))
    states = []
    for f in range(N + 1):
        t = f / FPS
        s = math.exp(-((t - 0.25) / 0.15) ** 2)                 # the stagger
        k = smooth((t - 0.35) / 0.95) ** 1.6                     # the fall
        st = dead(k, s)
        st.off.z += lift * k + 0.04 * HIP_H * math.exp(-((t - 1.35) / 0.08) ** 2)   # a small bounce as it lands
        states.append(st)
    # Before the body lands, a straight body rolled partway can dip below the ground: lift those frames too.
    for st in states:
        dz = ground_offset(st)
        if dz > 0: st.off.z += dz
    CLIPS["death"] = (states, False)


# ---------------------------------------------------------------- actions
def apply_pose(B):
    for n, M in B.items(): ARM.pose.bones[n].matrix_basis = M
    bpy.context.view_layer.update()


def write_action(name, states):
    """One Blender action, keyed on every frame (local location and rotation per bone)."""
    act = bpy.data.actions.new(name); act.use_fake_user = True; act.id_root = "OBJECT"
    frames = [basis_of(pose(s)) for s in states]
    for n in ORDER:
        dp = 'pose.bones["%s"]' % n
        loc = [B[n].to_translation() for B in frames]; rq = [B[n].to_quaternion() for B in frames]
        for i in range(1, len(rq)):
            if rq[i].dot(rq[i - 1]) < 0: rq[i] = -rq[i]
        for path, vals, cnt in (("location", loc, 3), ("rotation_quaternion", rq, 4)):
            for c in range(cnt):
                fc = act.fcurves.new(dp + "." + path, index=c, action_group=n)
                fc.keyframe_points.add(len(vals))
                co = []
                for f, v in enumerate(vals): co += [float(f), float(v[c])]
                fc.keyframe_points.foreach_set("co", co)
                for kp in fc.keyframe_points: kp.interpolation = "LINEAR"
    act.use_frame_range = True; act.frame_start = 0; act.frame_end = len(states) - 1
    return act


def export(path):
    bpy.ops.object.select_all(action="DESELECT")
    # Each action on its own (muted) NLA track: the exporter's ACTIONS mode writes each as one glTF animation.
    ad = ARM.animation_data_create(); ad.action = None
    for act in bpy.data.actions:
        tr = ad.nla_tracks.new(); tr.name = act.name; tr.strips.new(act.name, 0, act); tr.mute = True
    for pb in ARM.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=False, export_yup=True,
                              export_apply=False, export_skins=True, export_def_bones=False, export_leaf_bone=False,
                              export_animations=True, export_animation_mode="ACTIONS", export_force_sampling=True,
                              export_optimize_animation_size=False, export_anim_slide_to_zero=True,
                              export_image_format="AUTO", export_tangents=False, export_materials="EXPORT")


# ---------------------------------------------------------------- review renders (Cycles, CPU)
def render_setup(samples):
    scene.render.engine = "CYCLES"; scene.cycles.device = "CPU"; scene.cycles.samples = samples
    scene.cycles.use_denoising = True; scene.render.film_transparent = False
    scene.view_settings.view_transform = "AgX"; scene.view_settings.look = "AgX - Medium High Contrast"
    w = bpy.data.worlds.new("sky"); scene.world = w; w.use_nodes = True
    bg = w.node_tree.nodes["Background"]; bg.inputs[0].default_value = (0.55, 0.62, 0.72, 1); bg.inputs[1].default_value = 0.7
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); scene.collection.objects.link(sun)
    sun.data.energy = 3.5; sun.data.angle = math.radians(4); sun.rotation_euler = (math.radians(50), math.radians(-10), math.radians(-35))
    bpy.ops.mesh.primitive_plane_add(size=L * 8, location=(0, 0, 0))
    gm = bpy.data.materials.new("ground"); gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.32, 0.30, 0.24, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.95
    bpy.context.active_object.data.materials.append(gm)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
    return cam


def look(cam, eye, target, ortho=None):
    cam.location = eye; d = (target - eye).normalized()
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    if ortho: cam.data.type = "ORTHO"; cam.data.ortho_scale = ortho
    else: cam.data.type = "PERSP"; cam.data.lens = 50


def render_to(path, w, h):
    scene.render.resolution_x, scene.render.resolution_y = w, h; scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"; scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def save_jpeg(arr, path):
    h, w = arr.shape[:2]; img = bpy.data.images.new("out", w, h, alpha=False)
    px = np.ones((h, w, 4), np.float32); px[..., :3] = arr[..., :3]; img.pixels.foreach_set(px.ravel())
    img.filepath_raw = path; img.file_format = "JPEG"; scene.render.image_settings.quality = 82
    img.save(filepath=path, quality=82); bpy.data.images.remove(img)


def load_px(path):
    img = bpy.data.images.load(path); img.colorspace_settings.name = "Non-Color"
    a = np.array(img.pixels[:], np.float32).reshape(img.size[1], img.size[0], 4); bpy.data.images.remove(img); return a


def review_renders(outdir, samples):
    os.makedirs(outdir, exist_ok=True); tmp = tempfile.mkdtemp(prefix="dino_render_")
    cam = render_setup(samples)
    zc = 0.5 * HIP_H + 0.1 * L; xc = 0.5 * (min(xs) + max(xs)) * SC
    apply_pose(basis_of(pose(CLIPS["idle"][0][0])))
    side_eye = Vector((xc, -3 * L, zc + 0.35 * L))
    look(cam, side_eye, Vector((xc, 0, zc)), ortho=L * 1.12)
    render_to(os.path.join(tmp, "side.png"), 512, 288); save_jpeg(load_px(os.path.join(tmp, "side.png")), os.path.join(outdir, SPECIES + "_side.jpg"))
    eye = Vector((xc + 1.2 * L, -1.5 * L, HIP_H + 0.45 * L))
    look(cam, eye, Vector((xc + 0.05 * L, 0, 0.55 * HIP_H)))
    cam.data.lens = 50 * (eye - Vector((xc, 0, 0.5 * HIP_H))).length / (2.1 * L)
    render_to(os.path.join(tmp, "q.png"), 512, 384); save_jpeg(load_px(os.path.join(tmp, "q.png")), os.path.join(outdir, SPECIES + "_34.jpg"))
    # Walk contact sheet: eight evenly spaced frames of one cycle, side on, 2 x 4 tiles.
    st = CLIPS["walk"][0]; n = len(st) - 1; tiles = []
    look(cam, side_eye, Vector((xc, 0, zc)), ortho=L * 1.12)
    for i in range(8):
        apply_pose(basis_of(pose(st[int(round(i * n / 8))])))
        p = os.path.join(tmp, "w%d.png" % i); render_to(p, 256, 144); tiles.append(load_px(p))
    rows = [np.concatenate([tiles[r * 2 + 1], tiles[r * 2]][::-1], axis=1) for r in range(4)]
    sheet = np.concatenate(rows[::-1], axis=0)          # Blender's pixel rows run bottom-up
    save_jpeg(sheet, os.path.join(outdir, SPECIES + "_walk.jpg"))
    for f in os.listdir(tmp): os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)


# ---------------------------------------------------------------- main
if __name__ == "__main__":
    workdir = tempfile.mkdtemp(prefix="dino_build_")
    BODY = build_body()
    nextra_budget = 2500
    remesh_and_reduce(BODY, int(opt["--lod0"]) - nextra_budget)
    BODY.data.attributes.new("feat", "FLOAT", "POINT")
    far = weight_body(BODY)
    build_extras(); join_extras(BODY)
    tris = sum(len(p.vertices) - 2 for p in BODY.data.polygons)
    print("dino_build: body %d triangles, %d vertices" % (tris, len(BODY.data.vertices)))
    unwrap(BODY)
    files = bake_textures(BODY, workdir)
    export_material(BODY, files)
    idle_clips()
    for gi in range(3): gait_clip(gi)
    feed_clips(); call_clip(); attack_clip(); hurt_clip(); jump_clip(); sleep_clip(); death_clip()
    for name, (states, loop) in CLIPS.items(): write_action(name, states)
    export(OUT_GLB)
    info = dict(species=SPECIES, length=L, height_table=SIM["height"], hip_height=HIP_H, tris=tris, far_weight_mm=far * 1000,
                gaits=GAIT_INFO, speeds=dict(walk=SPEEDS[0], trot=SPEEDS[1], run=SPEEDS[2]),
                feet=[LEGS[k][3] for k in LEG_KEYS], clips={n: len(s) for n, (s, _) in CLIPS.items()})
    json.dump(info, open(OUT_GLB + ".json", "w"), indent=1)
    print("dino_build: wrote %s (%d clips)" % (OUT_GLB, len(CLIPS)))
    if opt["--render"]: review_renders(opt["--render"], int(opt["--samples"]))
    for f in os.listdir(workdir): os.remove(os.path.join(workdir, f))
    os.rmdir(workdir)
