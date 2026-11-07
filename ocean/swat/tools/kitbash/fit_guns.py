# Blender (headless): put Gun Kitbash Vol 3 guns into the measured rigid-rifle
# frame (rifle_geometry.h: +X to the muzzle, +Y up, +Z right; Blender +X, +Z,
# -Y) and either render calibration side views or export fitted GLBs.
#
#   measure: blender -b -P fit_guns.py -- measure FBX_DIR RIFLE_GLB OUT_DIR
#     Orthographic right-side view at 1 mm/px, the officer rifle overlaid in
#     red. Pixel (px,py) maps to rifle frame x=px/1000-.15, y=.30-py/1000.
#   export:  blender -b -P fit_guns.py -- export FBX_DIR RIFLE_GLB OUT_DIR GUN:SHIFT...
#     FIT_JSON lists, per gun, its measured pistol grip and muzzle in the
#     measure frame. A two-point fit (uniform scale, no rotation) puts both on
#     the rifle's grip and muzzle landmarks, so hand IK and the authoritative
#     muzzle are unchanged.
import bpy, bmesh, sys, os, json, math
from mathutils import Vector, Matrix

argv = sys.argv[sys.argv.index("--") + 1:]
mode = argv[0]
UNIT = .11  # pack units to metres: G36 1.00 m, AUG .79 m, MP5 .71 m
GRIP = Vector((.26177320, -.07099672))   # rifle frame x, y (rifle_geometry.h)
MUZZLE = Vector((.89999995, .04362635))

def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    for o in bpy.context.scene.objects: o.select_set(o in meshes)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1: bpy.ops.object.join()
    gun = bpy.context.view_layer.objects.active
    for o in list(bpy.context.scene.objects):
        if o is not gun: bpy.data.objects.remove(o)
    gun.data.transform(gun.matrix_world); gun.matrix_world = Matrix.Identity(4)
    return gun

def bounds(gun):
    vs = [v.co for v in gun.data.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    return vs, lo, hi

def normalize(gun):
    # Longest axis to +X, tallest remaining axis to +Z (up).
    vs, lo, hi = bounds(gun); size = hi - lo
    order = sorted(range(3), key=lambda i: -size[i]); long, tall = order[0], order[1]
    side = 3 - long - tall
    basis = Matrix.Identity(3)
    rows = [Vector((0, 0, 0)) for _ in range(3)]
    rows[0][long] = 1; rows[2][tall] = 1; rows[1][side] = 1
    rot = Matrix((rows[0], rows[1], rows[2]))
    if rot.determinant() < 0: rot[1] = -rot[1]
    gun.data.transform(rot.to_4x4())
    vs, lo, hi = bounds(gun)
    # Muzzle end: the end whose last 4% is shorter (stocks are tall).
    span = hi.x - lo.x
    def end_height(at_max):
        sel = [v for v in vs if (v.x > hi.x - .04 * span if at_max else v.x < lo.x + .04 * span)]
        return max(v.z for v in sel) - min(v.z for v in sel)
    if end_height(False) < end_height(True):  # muzzle at -X: turn 180 about up
        gun.data.transform(Matrix.Rotation(math.pi, 4, 'Z'))
    # Upside down? Magazines and grips hang below the bore line.
    vs, lo, hi = bounds(gun)
    mid = [v for v in vs if lo.x + .3 * (hi.x - lo.x) < v.x < lo.x + .6 * (hi.x - lo.x)]
    centre = (max(v.z for v in mid) + min(v.z for v in mid)) / 2
    muzzle_z = [v.z for v in vs if v.x > hi.x - .02 * (hi.x - lo.x)]
    if sum(muzzle_z) / len(muzzle_z) < centre:
        gun.data.transform(Matrix.Rotation(math.pi, 4, 'X'))
    vs, lo, hi = bounds(gun)
    # Butt at x=0, bore end centred on the rifle's muzzle height, centreline z=0.
    tip = [v for v in vs if v.x > hi.x - .01 * (hi.x - lo.x)]
    bore = (max(v.z for v in tip) + min(v.z for v in tip)) / 2
    gun.data.transform(Matrix.Translation(Vector((-lo.x, -(lo.y + hi.y) / 2, -bore))))
    gun.data.transform(Matrix.Scale(UNIT, 4))
    gun.data.transform(Matrix.Translation(Vector((0, 0, MUZZLE.y))))
    return gun

def material(name, colour, metal, rough):
    m = bpy.data.materials.new(name); m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*colour, 1)
    p.inputs["Metallic"].default_value = metal; p.inputs["Roughness"].default_value = rough
    m.diffuse_color = (*colour, 1)
    return m

fbx_dir = argv[1]
names = sorted(f for f in os.listdir(fbx_dir) if f.lower().endswith(".fbx"))

if mode == "measure":
    rifle_glb, out = argv[2], argv[3]; os.makedirs(out, exist_ok=True)
    report = open(os.path.join(out, "fit_measure.txt"), "w")
    for name in names:
        gun = normalize(load(os.path.join(fbx_dir, name)))
        gun.data.materials.clear(); gun.data.materials.append(material("kit", (.55, .58, .62), 0, .6))
        before = set(bpy.context.scene.objects)
        bpy.ops.import_scene.gltf(filepath=rifle_glb)
        red = material("rifle", (.9, .1, .1), 0, .6)
        for o in set(bpy.context.scene.objects) - before:
            if o.type == 'MESH':
                o.data.materials.clear(); o.data.materials.append(red)
                o.show_wire = True
        vs, lo, hi = bounds(gun)
        report.write(f"{name}: length {hi.x - lo.x:.3f} m, x {lo.x:.3f}..{hi.x:.3f}, y {-hi.y:.3f}..{-lo.y:.3f}, up {lo.z:.3f}..{hi.z:.3f}\n")
        sc = bpy.context.scene
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam)
        cam.data.type = 'ORTHO'; cam.data.ortho_scale = 1.3
        cam.location = Vector((.5, -3, -.025)); cam.rotation_euler = (math.pi / 2, 0, 0)
        sc.camera = cam
        sc.render.engine = 'BLENDER_WORKBENCH'; sc.display.shading.light = 'STUDIO'
        sc.display.shading.color_type = 'MATERIAL'; sc.display.shading.show_cavity = True
        sc.display.shading.show_xray = True; sc.display.shading.xray_alpha = .6
        sc.render.resolution_x = 1300; sc.render.resolution_y = 650
        sc.render.filepath = os.path.join(out, "fit_" + os.path.splitext(name)[0] + ".png")
        bpy.ops.render.render(write_still=True)
    report.close()

elif mode == "export":
    # export FBX_DIR RIFLE_GLB OUT_DIR GUN:SHIFT...: SHIFT (metres along +X)
    # moves the gun's pistol grip, measured on the measure-mode side view at
    # hand height (y=-.071), onto the rifle's grip landmark. Grip-only fit at the
    # pack's true scale, then a verification side view and fit.json with each
    # gun's own drawn muzzle (rifle frame) for tracers and flashes.
    rifle_glb, out = argv[2], argv[3]; os.makedirs(out, exist_ok=True)
    shifts = {g.split(":")[0]: float(g.split(":")[1]) for g in argv[4:]}
    fits = {}
    for name in names:
        stem = os.path.splitext(name)[0]
        if stem not in shifts: continue
        gun = normalize(load(os.path.join(fbx_dir, name)))
        vs, lo, hi = bounds(gun)
        dx = shifts[stem]
        gun.data.transform(Matrix.Translation(Vector((dx, 0, 0))))
        vs, lo, hi = bounds(gun)
        tip = [v for v in vs if v.x > hi.x - .01]
        muzzle = [hi.x, (max(v.z for v in tip) + min(v.z for v in tip)) / 2, 0]
        fits[stem] = {"grip_shift": dx, "muzzle": muzzle, "butt": lo.x}
        metal = material("kitbash_metal", (.045, .047, .050), .5, .45)
        gun.data.materials.clear(); gun.data.materials.append(metal)
        while len(gun.data.uv_layers) > 1: gun.data.uv_layers.remove(gun.data.uv_layers[-1])
        bm = bmesh.new(); bm.from_mesh(gun.data)
        bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.to_mesh(gun.data); bm.free()
        bpy.ops.object.select_all(action='DESELECT'); gun.select_set(True)
        bpy.context.view_layer.objects.active = gun
        bpy.ops.export_scene.gltf(filepath=os.path.join(out, stem + ".glb"), use_selection=True,
            export_format='GLB', export_normals=True, export_tangents=True, export_texcoords=True,
            export_materials='EXPORT', export_yup=True)
        gun.data.materials.clear(); gun.data.materials.append(material("kit", (.55, .58, .62), 0, .6))
        before = set(bpy.context.scene.objects)
        bpy.ops.import_scene.gltf(filepath=rifle_glb)
        red = material("rifle", (.9, .1, .1), 0, .6)
        for o in set(bpy.context.scene.objects) - before:
            if o.type == 'MESH': o.data.materials.clear(); o.data.materials.append(red)
        sc = bpy.context.scene
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam)
        cam.data.type = 'ORTHO'; cam.data.ortho_scale = 1.3
        cam.location = Vector((.5, -3, -.025)); cam.rotation_euler = (math.pi / 2, 0, 0); sc.camera = cam
        sc.render.engine = 'BLENDER_WORKBENCH'; sc.display.shading.light = 'STUDIO'
        sc.display.shading.color_type = 'MATERIAL'; sc.display.shading.show_xray = True; sc.display.shading.xray_alpha = .6
        sc.render.resolution_x = 1300; sc.render.resolution_y = 650
        sc.render.filepath = os.path.join(out, "verify_" + stem + ".png")
        bpy.ops.render.render(write_still=True)
        print("EXPORT", stem, f"shift {dx:+.3f}", "muzzle", [round(m, 3) for m in muzzle])
    json.dump(fits, open(os.path.join(out, "fit.json"), "w"), indent=1)
