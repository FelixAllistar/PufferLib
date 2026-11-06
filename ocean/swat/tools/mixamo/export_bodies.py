# Blender (headless): Mixamo character FBX -> skinned GLB (rest pose, no
# animation, textures written as PNG beside it). The GLB feeds conform_body.py.
#   blender -b -P export_bodies.py -- IN_DIR OUT_DIR [NAME ...]
import bpy, sys, os

argv = sys.argv[sys.argv.index("--") + 1:]
src, out, only = argv[0], argv[1], set(argv[2:])
os.makedirs(out, exist_ok=True)
for name in sorted(os.listdir(src)):
    stem, ext = os.path.splitext(name)
    if ext.lower() != ".fbx" or (only and stem not in only): continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=os.path.join(src, name))
    for o in list(bpy.context.scene.objects):
        if o.animation_data: o.animation_data_clear()
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    target = os.path.join(out, stem)
    os.makedirs(target, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(target, "body.glb"), export_format='GLB',
        export_skins=True, export_animations=False, export_morph=False, export_yup=True,
        export_image_format='AUTO', export_materials='EXPORT', export_apply=False)
    # Also write every image as a standalone PNG (runtime loads textures by file).
    for img in bpy.data.images:
        if img.size[0] == 0: continue
        img.filepath_raw = os.path.join(target, bpy.path.clean_name(img.name) + ".png")
        img.file_format = 'PNG'
        try: img.save()
        except Exception as e: print("IMAGE_FAIL", img.name, e)
    print("EXPORTED", stem)
