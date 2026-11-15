# Blender (4.2) headless: tiny test dinosaurs for tools/animcheck.c and
# tests/test_dino_ik.c, so both have models without the real assets.
#   blender -b --factory-startup --disable-autoexec --python-exit-code 1 \
#       --python ocean/cretaceous/tools/blender/make_fixture.py -- [OUTDIR] [WORKDIR]
# OUTDIR (default ocean/cretaceous/tests/fixtures) receives the .dino files;
# WORKDIR (default build/cretaceous/fixtures) the glTF files and the
# converter's textures, which are not committed.
#
# Two skeletons, named like the real rigs so render/dino_ik.c finds the legs:
#   - biped: raptor-sized (3.0 m, the raptor's leg names: upperleg, lowerleg,
#     ankle, toe), a digitigrade leg with the knee forward;
#   - quadruped: sauropelta-sized (5.2 m, the stegosaur's leg names: upperleg,
#     lowerleg, backfoot behind, upperarm, lowerarm, foot in front).
# Each is a box per bone (a few hundred triangles), rigidly weighted.
# Clips are in place, posed by an exact two-bone solve per frame: a planted
# foot moves straight back at the gait speed and keeps its rest orientation.
#   - fixture_biped.dino: idle, walk (2.2 m/s) and trot (4.6 m/s), the
#     raptor's sim gaits;
#   - fixture_quad.dino: idle and walk (1.3 m/s), the sauropelta's walk;
#   - fixture_biped_slide.dino, fixture_quad_slide.dino: only a walk, whose
#     planted feet slip: their speed swings 60% either side of the gait speed
#     over the stance and they skid 4 cm sideways.
# Each file is then converted with tools/dino_convert.py (--tex 256).
import bpy, sys, os, math, subprocess, shutil
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
HERE = os.path.dirname(os.path.abspath(__file__))
CRET = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.abspath(argv[0]) if len(argv) > 0 else os.path.join(CRET, "tests", "fixtures")
WORK = os.path.abspath(argv[1]) if len(argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(CRET)), "build", "cretaceous", "fixtures")
CONVERT = os.path.join(CRET, "tools", "dino_convert.py")
FPS = 30

# Blender axes: x forward, y to the animal's left, z up (the glTF export
# turns z up into y up and y into -z).
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = FPS

def two_bone(H, T, a, b, bend):
    """Knee position for hip H, ankle target T, bone lengths a and b, in the
    x-z plane; the knee points to +x (bend 1) or -x (bend -1)."""
    d = T - H; d.y = 0; L = d.length
    L = min(max(L, abs(a - b) + 1e-4), (a + b) * 0.9999)
    u = d.normalized()
    perp = Vector((-u.z, 0, u.x)) * bend
    if perp.x * bend < 0: perp = -perp
    ca = (a * a + L * L - b * b) / (2 * a * L); sa = math.sqrt(max(0.0, 1 - ca * ca))
    K = H + (u * ca + perp * sa) * a
    K.y = H.y
    return K

class Rig:
    """Bones as (name, parent, head, tail); legs as (hip, knee, ankle, foot
    bones, bend sign, phase offset)."""
    def __init__(self, name, bones, legs):
        self.name, self.bones, self.legs = name, bones, legs
        arm = bpy.data.armatures.new(name); ob = bpy.data.objects.new(name, arm)
        bpy.context.scene.collection.objects.link(ob); bpy.context.view_layer.objects.active = ob; ob.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        for nm, par, h, t in bones:
            eb = arm.edit_bones.new(nm); eb.head = h; eb.tail = t; eb.roll = 0
            if par: eb.parent = arm.edit_bones[par]
        bpy.ops.object.mode_set(mode="OBJECT")
        self.ob = ob
        self.rest = {b.name: b.matrix_local.copy() for b in arm.bones}
        self.extent = self.mesh()

    def mesh(self):
        """A box around each bone, weighted to it alone."""
        verts, faces, groups = [], [], []
        for nm, par, h, t in self.bones:
            h, t = Vector(h), Vector(t); ax = t - h; L = ax.length
            w = max(0.05, min(0.35, 0.28 * L)) if "tail" not in nm else 0.22 * L
            if nm in ("pelvis", "chest", "spine"): w = 0.35
            if any(k in nm for k in ("toe", "foot", "ankle")): w = 0.09
            y = ax.normalized(); x = y.cross(Vector((0, 1, 0))) if abs(y.y) < 0.9 else y.cross(Vector((1, 0, 0)))
            x.normalize(); z = x.cross(y)
            base = len(verts)
            for s in (0, 1):
                for i, j in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    verts.append(h + ax * s + x * (i * w * 0.5) + z * (j * w * 0.5))
            for f in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
                faces.append(tuple(base + k for k in f))
            groups.append((nm, list(range(base, base + 8))))
        me = bpy.data.meshes.new(self.name + "_mesh"); me.from_pydata([tuple(v) for v in verts], [], faces); me.update()
        mob = bpy.data.objects.new(self.name + "_body", me); bpy.context.scene.collection.objects.link(mob)
        for nm, idx in groups:
            g = mob.vertex_groups.new(name=nm); g.add(idx, 1.0, "REPLACE")
        mat = bpy.data.materials.new("skin"); me.materials.append(mat)
        bpy.context.view_layer.objects.active = mob; mob.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT"); bpy.ops.mesh.quads_convert_to_tris(); bpy.ops.object.mode_set(mode="OBJECT")
        mob.parent = self.ob; mod = mob.modifiers.new("arm", "ARMATURE"); mod.object = self.ob
        return max(v.x for v in verts) - min(v.x for v in verts)   # nose to tail, which the converter scales to --length

    def head(self, nm):
        return self.rest[nm].translation.copy()

    def set_world(self, nm, M):
        pb = self.ob.pose.bones[nm]; pb.matrix = M; bpy.context.view_layer.update()

    def key(self, nm, f):
        pb = self.ob.pose.bones[nm]
        pb.keyframe_insert("location", frame=f); pb.keyframe_insert("rotation_quaternion", frame=f)

    def clip(self, name, period, frames, foot_at, bob, sway=0.0):
        """One looping clip of `frames` + 1 keys. foot_at(leg, phase) gives a
        foot's offset (dx forward, dz up, dy left) from its rest ankle;
        bob(phase) the pelvis lift."""
        ob = self.ob; ob.animation_data_create()
        act = bpy.data.actions.new(name); ob.animation_data.action = act
        for pb in ob.pose.bones: pb.rotation_mode = "QUATERNION"
        for f in range(frames + 1):
            p = f / frames
            for pb in ob.pose.bones: pb.matrix_basis = Matrix.Identity(4)
            bpy.context.view_layer.update()
            root = self.bones[0][0]
            R = self.rest[root].copy(); R.translation.z += bob(p); self.set_world(root, R)
            for nm, par, h, t in self.bones:
                if "tail" in nm and sway:
                    pb = ob.pose.bones[nm]; pb.rotation_quaternion = Matrix.Rotation(sway * math.sin(2 * math.pi * p), 4, "Z").to_quaternion()
            bpy.context.view_layer.update()
            for li, (hip, knee, ankle, foot, bend, off) in enumerate(self.legs):
                H = ob.pose.bones[hip].head.copy()          # armature space, after the pelvis moved
                A0 = self.head(ankle); dx, dz, dy = foot_at(li, (p + off) % 1.0)
                A = A0 + Vector((dx, dy, dz))
                a = (self.head(knee) - self.head(hip)).length; b = (self.head(ankle) - self.head(knee)).length
                K = two_bone(H, A, a, b, bend)
                for bone, P0, P1, Q0, Q1 in ((hip, self.head(hip), self.head(knee), H, K), (knee, self.head(knee), self.head(ankle), K, A)):
                    d0 = (P1 - P0).normalized(); d1 = (Q1 - Q0).normalized()
                    rot = d0.rotation_difference(d1).to_matrix().to_4x4()
                    M = rot @ self.rest[bone]; M.translation = Q0; self.set_world(bone, M)
                M = self.rest[ankle].copy(); M.translation = A; self.set_world(ankle, M)   # the foot keeps its rest orientation
            for nm, par, h, t in self.bones: self.key(nm, f + 1)
        track = ob.animation_data.nla_tracks.new(); track.name = name
        track.strips.new(name, 1, act); ob.animation_data.action = None

def gait(duty, stride, lift, slip=0.0, skid=0.0, side=None):
    """Foot path over a cycle: stance (0..duty) straight back over `stride`,
    swing forward with a lift. slip > 0 makes the stance speed swing by that
    share either side of the mean; skid moves it sideways (m)."""
    def at(li, q):
        if q < duty:
            s = q / duty
            g = s + slip * math.sin(2 * math.pi * s) / (2 * math.pi)
            dy = skid * math.sin(math.pi * s) * (side(li) if side else 1)
            return stride * (0.5 - g), 0.0, dy
        s = (q - duty) / (1 - duty)
        e = 0.5 - 0.5 * math.cos(math.pi * s)
        return stride * (-0.5 + e), lift * math.sin(math.pi * s), 0.0
    return at

def biped():
    s = 0.18
    bones = [("pelvis", None, (0, 0, 1.0), (0.3, 0, 1.03)), ("spine", "pelvis", (0.3, 0, 1.03), (0.65, 0, 1.12)),
             ("neck", "spine", (0.65, 0, 1.12), (0.9, 0, 1.38)), ("head", "neck", (0.9, 0, 1.38), (1.25, 0, 1.33)),
             ("tail.001", "pelvis", (0, 0, 1.0), (-0.55, 0, 0.97)), ("tail.002", "tail.001", (-0.55, 0, 0.97), (-1.1, 0, 0.94)),
             ("tail.003", "tail.002", (-1.1, 0, 0.94), (-1.65, 0, 0.92))]
    legs = []
    for side, y in (("l", s), ("r", -s)):
        H = Vector((0.0, y, 0.98)); A = Vector((-0.06, y, 0.13)); K = two_bone(H, A, 0.5, 0.5, 1)
        T = Vector((0.08, y, 0.035)); E = Vector((0.24, y, 0.035))
        bones += [("upperleg." + side, "pelvis", tuple(H), tuple(K)), ("lowerleg." + side, "upperleg." + side, tuple(K), tuple(A)),
                  ("ankle." + side, "lowerleg." + side, tuple(A), tuple(T)), ("toe." + side, "ankle." + side, tuple(T), tuple(E))]
        legs.append(("upperleg." + side, "lowerleg." + side, "ankle." + side, "toe." + side, 1, 0.0 if side == "l" else 0.5))
    return bones, legs

def quad():
    s = 0.32
    bones = [("pelvis", None, (-0.8, 0, 0.85), (0.0, 0, 0.9)), ("chest", "pelvis", (0.0, 0, 0.9), (0.8, 0, 0.85)),
             ("neck", "chest", (0.8, 0, 0.85), (1.5, 0, 0.75)), ("head", "neck", (1.5, 0, 0.75), (2.1, 0, 0.65)),
             ("tail.001", "pelvis", (-0.8, 0, 0.85), (-1.8, 0, 0.75)), ("tail.002", "tail.001", (-1.8, 0, 0.75), (-2.8, 0, 0.6))]
    legs = []
    names = {"hind": ("upperleg.%s", "lowerleg.%s", "backfoot.%s", "pelvis", -0.8, 1), "front": ("upperarm.%s", "lowerarm.%s", "foot.%s", "chest", 0.8, -1)}
    # Diagonal walk: left hind, left front, right hind, right front a quarter cycle apart.
    phase = {("hind", "l"): 0.0, ("front", "l"): 0.25, ("hind", "r"): 0.5, ("front", "r"): 0.75}
    for end in ("hind", "front"):
        hn, kn, an, par, x, bend = names[end]
        for side, y in (("l", s), ("r", -s)):
            H = Vector((x, y, 0.82)); A = Vector((x - 0.02 * bend, y, 0.1)); K = two_bone(H, A, 0.45, 0.45, bend)
            E = Vector((x + 0.14, y, 0.0))
            bones += [(hn % side, par, tuple(H), tuple(K)), (kn % side, hn % side, tuple(K), tuple(A)), (an % side, kn % side, tuple(A), tuple(E))]
            legs.append((hn % side, kn % side, an % side, an % side, bend, phase[(end, side)]))
    return bones, legs

def build(name, maker, clips, length):
    reset()
    bones, legs = maker()
    rig = Rig(name, bones, legs)
    k = rig.extent / length   # Blender metres per model metre: gait speeds are the model's after the converter's scaling
    for cname, period, duty, speed, lift, bobamp, slip, skid in clips:
        frames = int(round(period * FPS))
        if speed <= 0:   # idle: a slow breath of the pelvis, feet still
            rig.clip(cname, period, frames, lambda li, q: (0.0, 0.0, 0.0), lambda p: 0.012 * math.sin(2 * math.pi * p))
            continue
        stride = speed * k * period * duty
        side = lambda li: 1 if legs[li][0].endswith("l") else -1
        rig.clip(cname, period, frames, gait(duty, stride, lift, slip, skid, side), lambda p, a=bobamp: a * math.cos(4 * math.pi * p), sway=0.05)
    os.makedirs(WORK, exist_ok=True)
    glb = os.path.join(WORK, name + ".glb")
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB", export_animations=True, export_animation_mode="ACTIONS",
                              export_force_sampling=True, export_skins=True, export_yup=True, export_apply=False, export_materials="EXPORT")
    cmd = [bpy.app.binary_path, "-b", "--factory-startup", "--disable-autoexec", "--python-exit-code", "1", "--python", CONVERT, "--",
           glb, name, WORK, "--length", str(length), "--tex", "256", "--map", ",".join("%s=%s" % (c[0], c[0]) for c in clips)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    for line in r.stdout.splitlines():
        if line.startswith("dino:"): print(line)
    if r.returncode != 0: print(r.stdout[-3000:], r.stderr[-3000:]); raise SystemExit("make_fixture: converting %s failed" % name)
    os.makedirs(OUT, exist_ok=True)
    shutil.copy(os.path.join(WORK, name + ".dino"), os.path.join(OUT, name + ".dino"))

#   name, period s, duty, speed m/s, lift m, pelvis bob m, slip, skid m
IDLE = ("idle", 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0)
build("fixture_biped", biped, [IDLE, ("walk", 0.6, 0.6, 2.2, 0.12, 0.015, 0.0, 0.0), ("trot", 0.4, 0.4, 4.6, 0.14, 0.02, 0.0, 0.0)], 3.0)
build("fixture_biped_slide", biped, [("walk", 0.6, 0.6, 2.2, 0.12, 0.015, 0.6, 0.04)], 3.0)
build("fixture_quad", quad, [IDLE, ("walk", 1.0, 0.7, 1.3, 0.1, 0.01, 0.0, 0.0)], 5.2)
build("fixture_quad_slide", quad, [("walk", 1.0, 0.7, 1.3, 0.1, 0.01, 0.6, 0.04)], 5.2)
print("make_fixture: wrote", ", ".join(n + ".dino" for n in ("fixture_biped", "fixture_biped_slide", "fixture_quad", "fixture_quad_slide")), "to", OUT)
