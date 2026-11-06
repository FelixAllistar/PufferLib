# Blender (headless): Mixamo animation FBX -> GLB holding the skeleton and one
# clip named after the file (e.g. "death from the front"), for
# retarget_clips.py --mixamo. Meshes are dropped.
#   blender -b -P export_clips.py -- IN_DIR OUT_DIR
import bpy, sys, os

argv = sys.argv[sys.argv.index("--") + 1:]
src, out = argv[0], argv[1]
os.makedirs(out, exist_ok=True)
for name in sorted(os.listdir(src)):
    stem, ext = os.path.splitext(name)
    if ext.lower() != ".fbx": continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=os.path.join(src, name))
    for o in list(bpy.context.scene.objects):
        if o.type == 'MESH': bpy.data.objects.remove(o, do_unlink=True)
    arm = next((o for o in bpy.context.scene.objects if o.type == 'ARMATURE'), None)
    if not arm or not arm.animation_data or not arm.animation_data.action:
        print("NOCLIP", name); continue
    act = arm.animation_data.action; act.name = stem
    for a in list(bpy.data.actions):
        if a != act: bpy.data.actions.remove(a)
    bpy.context.scene.frame_start = int(act.frame_range[0]); bpy.context.scene.frame_end = int(act.frame_range[1])
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, stem + ".glb"), export_format='GLB', export_skins=False,
        export_animations=True, export_animation_mode='ACTIONS', export_force_sampling=True, export_frame_step=1,
        export_morph=False, export_materials='NONE', export_yup=True)
    print("CLIP", stem, act.frame_range[:])
