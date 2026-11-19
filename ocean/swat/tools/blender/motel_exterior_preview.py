"""Review render of the Briar Court building shell + exterior kit +
architecture overlay in Blender (Cycles CPU), for reference hill-climbing.

  blender -b --factory-startup --python-exit-code 1 \
    -P ocean/swat/tools/blender/motel_exterior_preview.py -- OUT.png VIEW [--no-arch] [--samples N] [--size WxH]

VIEW is a name from VIEWS below (engine coordinates: eye, target, vertical
fov in degrees), matching tools/exterior_capture.c --eye/--target/--fov.
Shell modules come from assets/environment/motel_v1 at the authoritative
instance transforms (assets/environment/motel_exterior/sections.json); their
MC15_* materials are swapped for the exterior kit's library materials, like
the engine does. Review only: writes nothing into the repository.
"""
import bpy, sys, os, json, math
from mathutils import Vector, Euler

argv = sys.argv[sys.argv.index('--') + 1:]
OUTPNG, VIEW = argv[0], argv[1]
ARCH = '--no-arch' not in argv
SAMPLES = int(argv[argv.index('--samples') + 1]) if '--samples' in argv else 24
W, H = (map(int, argv[argv.index('--size') + 1].split('x')) if '--size' in argv else (960, 540))
HERE = os.path.dirname(os.path.abspath(__file__))
ENV = os.path.join(HERE, '..', '..', 'assets', 'environment')
KIT = os.path.join(ENV, 'motel_exterior')
VIEWS = {
    'front':   ((13.5, 1.4, 6.5), (0.0, 1.7, 0.0), 46),
    'walkway': ((9.0, 1.5, 8.5), (-5.0, 1.7, 0.0), 48),
    'office':  ((-15.0, 1.5, 6.5), (-8.5, 2.3, -0.5), 60),
    'endwall': ((13.5, 1.7, -9.0), (8.0, 1.6, -2.5), 55),
    'rear':    ((-1.0, 1.6, -17.0), (-1.0, 1.7, -6.0), 55),
    'roof':    ((12.0, 4.6, 12.0), (-3.0, 2.8, -3.0), 50),
    'elev':    ((-1.0, 1.65, 11.0), (-1.0, 1.6, 0.0), 58),
}
eye, target, fov = VIEWS[VIEW]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
def E(p): return Vector((p[0], -p[2], p[1]))

def import_glb(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    return [o for o in bpy.data.objects if o not in before]

# Library materials from the exterior kit.
kit_objs = import_glb(os.path.join(KIT, 'exterior_kit.glb'))
LIB = {m.name[4:]: m for m in bpy.data.materials if m.name.startswith('LIB_')}
kit = {o.name: o for o in kit_objs if o.type == 'MESH'}
for o in kit_objs: o.hide_render = True

def swap(o):
    for slot in o.material_slots:
        if slot.material and slot.material.name.startswith('MC15_'):
            n = slot.material.name[5:].split('.')[0]
            if n == 'steel' and 'roof' in LIB: n = 'roof'
            if n in LIB: slot.material = LIB[n]

sec = json.load(open(os.path.join(KIT, 'sections.json')))
cache = {}
for inst in sec['instances']:
    f = os.path.join(ENV, 'motel_v1', inst['asset'])
    if not inst['asset'] or not os.path.exists(f): continue
    if inst['asset'] not in cache:
        objs = [o for o in import_glb(f) if o.type == 'MESH']
        for o in objs: o.hide_render = True
        cache[inst['asset']] = objs
    for src in cache[inst['asset']]:
        o = src.copy(); sc.collection.objects.link(o); o.hide_render = False
        o.parent = None
        o.matrix_world = (__import__('mathutils').Matrix.Translation(E(inst['origin'])) @
                          Euler((0, 0, inst['yaw'])).to_matrix().to_4x4() @
                          __import__('mathutils').Matrix.Diagonal((inst['scale'][0], inst['scale'][2], inst['scale'][1], 1)))
        swap(o)

def place(src, p):
    o = src.copy(); sc.collection.objects.link(o); o.hide_render = False; o.parent = None
    s = p.get('scale', [1, 1, 1])
    o.matrix_world = (__import__('mathutils').Matrix.Translation(E(p['origin'])) @ Euler((0, 0, p['yaw'])).to_matrix().to_4x4() @
                      __import__('mathutils').Matrix.Diagonal((s[0], s[2], s[1], 1)))
    swap(o)

suppress = set()
if ARCH and os.path.exists(os.path.join(KIT, 'architecture_placements.json')):
    am = json.load(open(os.path.join(KIT, 'architecture_placements.json')))
    suppress = set(am['suppress'])
    arch = {o.name: o for o in import_glb(os.path.join(KIT, 'architecture_kit.glb')) if o.type == 'MESH'}
    for o in arch.values(): o.hide_render = True
    for p in am['placements']: place(arch[p['piece']], p)
for p in json.load(open(os.path.join(KIT, 'placements.json')))['placements']:
    if p['piece'] not in suppress and p['piece'] in kit: place(kit[p['piece']], p)

# Ground: parking surface at -0.08.
bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.08))
gm = bpy.data.materials.new('ground'); gm.use_nodes = True
gm.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (.045, .045, .043, 1)
gm.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = .9
bpy.context.object.data.materials.append(gm)

# Sun as in lighting.c (direction toward the sun -0.45, 0.82, -0.35) + sky.
d = E((-.45, .82, -.35)).normalized()
sun = bpy.data.lights.new('sun', 'SUN'); sun.energy = 3.2; sun.angle = math.radians(1.0)
so = bpy.data.objects.new('sun', sun); sc.collection.objects.link(so)
so.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
world = bpy.data.worlds.new('w'); sc.world = world; world.use_nodes = True
sky = world.node_tree.nodes.new('ShaderNodeTexSky'); sky.sky_type = 'NISHITA'
sky.sun_elevation = math.asin(d.z); sky.sun_rotation = math.atan2(d.y, d.x) - math.pi / 2; sky.sun_intensity = .25
world.node_tree.links.new(sky.outputs['Color'], world.node_tree.nodes['Background'].inputs['Color'])
world.node_tree.nodes['Background'].inputs['Strength'].default_value = .25

cam = bpy.data.cameras.new('cam'); co = bpy.data.objects.new('cam', cam); sc.collection.objects.link(co)
cam.sensor_fit = 'VERTICAL'; cam.angle_y = math.radians(fov); cam.clip_start = .05
co.location = E(eye); co.rotation_euler = (E(target) - E(eye)).to_track_quat('-Z', 'Y').to_euler()
sc.camera = co
sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = SAMPLES
sc.cycles.use_denoising = True; sc.cycles.max_bounces = 4
sc.render.resolution_x, sc.render.resolution_y = W, H
sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast'
sc.render.filepath = OUTPNG; sc.render.image_settings.file_format = 'PNG'
bpy.ops.render.render(write_still=True)
print('rendered', OUTPNG)
