# Blender (headless) inspection of Mixamo character FBXs: skeleton, height,
# materials/textures, and a front + three-quarter Cycles thumbnail per file.
#   blender -b -P inspect_characters.py -- IN_DIR OUT_DIR
import bpy, sys, os, json, math
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, out = argv[0], argv[1]
os.makedirs(out, exist_ok=True)
report = {}

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def bounds(objs):
    lo = Vector((1e9, 1e9, 1e9)); hi = -lo
    for o in objs:
        if o.type != 'MESH': continue
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
    return lo, hi

for name in sorted(os.listdir(src)):
    if not name.lower().endswith(".fbx"): continue
    reset()
    bpy.ops.import_scene.fbx(filepath=os.path.join(src, name))
    objs = list(bpy.context.scene.objects)
    arm = next((o for o in objs if o.type == 'ARMATURE'), None)
    meshes = [o for o in objs if o.type == 'MESH']
    lo, hi = bounds(meshes)
    mats, tex = set(), set()
    for m in meshes:
        for s in m.material_slots:
            if s.material:
                mats.add(s.material.name)
                if s.material.use_nodes:
                    for n in s.material.node_tree.nodes:
                        if n.type == 'TEX_IMAGE' and n.image: tex.add(n.image.name)
    bones = [b.name for b in arm.data.bones] if arm else []
    report[name] = {"height": round(hi.z - lo.z, 3), "bones": len(bones),
        "mixamo": all(b.startswith("mixamorig") for b in bones) if bones else False,
        "meshes": [m.name for m in meshes], "verts": sum(len(m.data.vertices) for m in meshes),
        "materials": sorted(mats), "textures": sorted(tex)}
    # Render: neutral studio light, front and three-quarter.
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'; scene.cycles.samples = 24; scene.cycles.device = 'CPU'
    scene.render.resolution_x = 360; scene.render.resolution_y = 560
    world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.25, 0.27, 0.3, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.2
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN'))
    sun.data.energy = 3; sun.rotation_euler = (math.radians(50), 0, math.radians(30)); scene.collection.objects.link(sun)
    center = (lo + hi) / 2; h = hi.z - lo.z
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
    cam.data.lens = 50
    for tag, ang in (("front", 0), ("quarter", 40)):
        a = math.radians(ang); d = h * 2.1
        cam.location = (center.x + d * math.sin(a), center.y - d * math.cos(a), center.z + h * 0.05)
        direction = center - cam.location
        cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = os.path.join(out, f"{os.path.splitext(name)[0]}_{tag}.png")
        bpy.ops.render.render(write_still=True)
    print("INSPECT", name, json.dumps(report[name]))

json.dump(report, open(os.path.join(out, "report.json"), "w"), indent=1)
