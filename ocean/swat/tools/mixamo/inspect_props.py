# Blender (headless): report and thumbnail a folder of prop FBXs (weapons,
# kitbash parts): object names, bounding size in metres, triangle count,
# materials, and a Workbench render per file.
#   blender -b -P inspect_props.py -- IN_DIR OUT_DIR
import bpy, sys, os
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, out = argv[0], argv[1]
os.makedirs(out, exist_ok=True)
report = open(os.path.join(out, "props_report.txt"), "w")
for name in sorted(os.listdir(src)):
    stem, ext = os.path.splitext(name)
    if ext.lower() != ".fbx": continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=os.path.join(src, name))
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    if not meshes: report.write(f"{name}: no meshes\n"); continue
    lo = Vector((1e9,)*3); hi = Vector((-1e9,)*3); tris = 0; mats = set()
    for o in meshes:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c); lo = Vector(map(min, lo, w)); hi = Vector(map(max, hi, w))
        tris += sum(len(p.vertices) - 2 for p in o.data.polygons)
        mats.update(m.name for m in o.data.materials if m)
    size = hi - lo
    report.write(f"{name}: {len(meshes)} meshes, {tris} tris, size {size.x:.3f} x {size.y:.3f} x {size.z:.3f}\n")
    report.write("  objects: " + ", ".join(sorted(o.name for o in meshes))[:600] + "\n")
    report.write("  materials: " + ", ".join(sorted(mats))[:400] + "\n")
    report.flush()
    centre = (lo + hi) / 2; radius = max(size.length / 2, 1e-3)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); bpy.context.scene.collection.objects.link(cam)
    d = Vector((1.0, -1.6, 0.7)).normalized()
    cam.location = centre + d * radius * 3.2
    cam.rotation_euler = (centre - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens = 50; cam.data.clip_end = radius * 20
    sc = bpy.context.scene; sc.camera = cam
    sc.render.engine = 'BLENDER_WORKBENCH'; sc.display.shading.light = 'STUDIO'
    sc.display.shading.color_type = 'MATERIAL'; sc.display.shading.show_cavity = True
    sc.render.resolution_x = 960; sc.render.resolution_y = 600
    sc.render.filepath = os.path.join(out, stem + ".png")
    bpy.ops.render.render(write_still=True)
    print("PROP", name, tris, tuple(round(v, 3) for v in size))
report.close()
