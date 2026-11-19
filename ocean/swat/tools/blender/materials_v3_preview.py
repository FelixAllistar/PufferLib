"""Hill-climb preview renders for materials_v3 (Blender 4.2, Cycles CPU).

    blender -b --factory-startup -P ocean/swat/tools/blender/materials_v3_preview.py -- MAPS_DIR VIEW OUT.png [--samples N] [--size WxH]

Builds a small real-scale stand-in for the Briar Court surface named by VIEW
(materials_v3_views.json: facade, gallery, lot, road, roof, trim, room, bath,
reception, laundry ...), applies the baked maps from MAPS_DIR with the same
metric repeat (tile_m) and macro variation as the engine (materials_v3.h,
lighting.c), places the camera/sun from the view entry (matched to the
reference photo) and renders. Deterministic: no random seeds, fixed samples.
Z is up here; the engine is Y-up.
"""
import json, math, os, sys
import bmesh, bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
RECIPE = json.load(open(os.path.join(HERE, 'materials_v3_recipe.json')))['materials']
VIEWS = json.load(open(os.path.join(HERE, 'materials_v3_views.json')))


def material(name, maps, cache={}):
    """Principled material from <name>_{basecolor,normal,roughness}.jpg."""
    if name in cache:
        return cache[name]
    m = RECIPE[name]
    mat = bpy.data.materials.new('v3_' + name); mat.use_nodes = True
    nt = mat.node_tree; N = nt.nodes; L = nt.links
    bsdf = N['Principled BSDF']
    uv = N.new('ShaderNodeUVMap')
    def tex(kind, color):
        t = N.new('ShaderNodeTexImage')
        t.image = bpy.data.images.load(os.path.join(maps, '%s_%s.jpg' % (name, kind)), check_existing=True)
        t.image.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
        t.interpolation = 'Cubic'
        L.new(uv.outputs['UV'], t.inputs['Vector'])
        return t
    base = tex('basecolor', True); rough = tex('roughness', False); nor = tex('normal', False)
    color = base.outputs['Color']
    macro, detail = m.get('macro', [0, 0, 0]), m.get('detail', [0, 0, 0])
    if macro[1] > 0:
        geo = N.new('ShaderNodeNewGeometry')
        if detail[0] > 0:
            mp = N.new('ShaderNodeMapping'); mp.inputs['Rotation'].default_value[2] = math.atan2(0.6, 0.8)
            mp.inputs['Scale'].default_value = (detail[1], detail[1], 1); mp.inputs['Location'].default_value = (0.37, 0.61, 0)
            L.new(uv.outputs['UV'], mp.inputs['Vector'])
            alt = N.new('ShaderNodeTexImage'); alt.image = base.image; alt.interpolation = 'Cubic'
            L.new(mp.outputs['Vector'], alt.inputs['Vector'])
            mask = N.new('ShaderNodeTexNoise'); mask.inputs['Scale'].default_value = macro[1] * 0.71; mask.inputs['Detail'].default_value = 0
            L.new(geo.outputs['Position'], mask.inputs['Vector'])
            mr = N.new('ShaderNodeMapRange'); mr.inputs['From Min'].default_value = 0.35; mr.inputs['From Max'].default_value = 0.65
            mr.inputs['To Max'].default_value = detail[0]
            L.new(mask.outputs['Fac'], mr.inputs['Value'])
            mix = N.new('ShaderNodeMix'); mix.data_type = 'RGBA'
            L.new(mr.outputs['Result'], mix.inputs['Factor']); L.new(color, mix.inputs[6]); L.new(alt.outputs['Color'], mix.inputs[7])
            color = mix.outputs[2]
        noise = N.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value = macro[1]; noise.inputs['Detail'].default_value = 2
        L.new(geo.outputs['Position'], noise.inputs['Vector'])
        amp = N.new('ShaderNodeMapRange'); amp.inputs['To Min'].default_value = 1 - macro[0]; amp.inputs['To Max'].default_value = 1 + macro[0]
        amp.inputs['From Min'].default_value = 0.3; amp.inputs['From Max'].default_value = 0.7
        L.new(noise.outputs['Fac'], amp.inputs['Value'])
        mul = N.new('ShaderNodeMix'); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'; mul.inputs['Factor'].default_value = 1
        # Linear-space multiply: scale the sRGB colour by amp^(1/2.2).
        pw = N.new('ShaderNodeMath'); pw.operation = 'POWER'; pw.inputs[1].default_value = 1 / 2.2
        L.new(amp.outputs['Result'], pw.inputs[0])
        comb = N.new('ShaderNodeCombineColor')
        for i in range(3):
            L.new(pw.outputs[0], comb.inputs[i])
        L.new(color, mul.inputs[6]); L.new(comb.outputs['Color'], mul.inputs[7])
        color = mul.outputs[2]
    L.new(color, bsdf.inputs['Base Color'])
    L.new(rough.outputs['Color'], bsdf.inputs['Roughness'])
    nm = N.new('ShaderNodeNormalMap'); L.new(nor.outputs['Color'], nm.inputs['Color']); L.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    cache[name] = mat
    return mat


def flat(name, rgb, rough=0.6, emit=0.0):
    mat = bpy.data.materials.new(name); mat.use_nodes = True
    b = mat.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*rgb, 1); b.inputs['Roughness'].default_value = rough
    if emit:
        b.inputs['Emission Color'].default_value = (*rgb, 1); b.inputs['Emission Strength'].default_value = emit
    return mat


def box(name, lo, hi, mat, tile=None, faces=None):
    """Axis-aligned box with metric planar UVs (dominant axis), / tile."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector([lo[i] + (v.co[i] + 0.5) * (hi[i] - lo[i]) for i in range(3)])
    uvl = bm.loops.layers.uv.new()
    t = tile or 1.0
    for f in bm.faces:
        n = f.normal
        a = [abs(n.x), abs(n.y), abs(n.z)]
        for l in f.loops:
            p = l.vert.co
            if a[2] >= max(a[0], a[1]):
                u, v = p.x, p.y
            elif a[0] >= a[1]:
                u, v = p.y * (1 if n.x > 0 else -1), p.z
            else:
                u, v = p.x * (-1 if n.y > 0 else 1), p.z
            l[uvl].uv = (u / t, v / t)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me); bpy.context.collection.objects.link(ob)
    me.materials.append(mat)
    return ob


def surf(maps, name):
    return material(name, maps), RECIPE[name]['tile_m']


def building(maps, v, x0=None, x1=None, depth=6.2, h=2.8):
    x0 = v.get('x0', -12) if x0 is None else x0
    x1 = v.get('x1', 12) if x1 is None else x1
    stucco, ts = surf(maps, v.get('wall', 'stucco'))
    box('wall', (x0, -0.1, 0), (x1, 0.1, h), stucco, ts)
    box('rear', (x0, depth - 0.1, 0), (x1, depth + 0.1, h), stucco, ts)
    for x in (x0, x1):
        box('end', (x - 0.1, 0, 0), (x + 0.1, depth, h), stucco, ts)
    roof, tr = surf(maps, 'roof')
    box('roof', (x0 - 0.15, -0.15, h), (x1 + 0.15, depth + 0.15, h + 0.12), roof, tr)
    fascia, tf = surf(maps, 'fascia')
    box('fascia', (x0 - 0.2, -0.2, h - 0.1), (x1 + 0.2, -0.15, h + 0.25), fascia, tf)
    box('fascia_r', (x0 - 0.2, depth + 0.15, h - 0.1), (x1 + 0.2, depth + 0.2, h + 0.25), fascia, tf)
    plinth, tp = surf(maps, 'plinth')
    box('plinth', (x0, -0.12, 0), (x1, -0.1, 0.15), plinth, tp)
    trim, tt = surf(maps, 'trim')
    dark = flat('glass', (0.02, 0.025, 0.03), 0.1)
    door = flat('door', (0.05, 0.22, 0.22), 0.5)
    x = x0 + 2
    while x < x1 - 1:
        box('win', (x - 0.9, -0.13, 0.9), (x + 0.9, -0.11, 2.1), dark)
        box('wintrim', (x - 1.0, -0.12, 0.8), (x + 1.0, -0.105, 2.2), trim, tt)
        box('door', (x + 1.1, -0.13, 0), (x + 2.0, -0.11, 2.1), door)
        x += 4
    for xr in range(int(x0) + 2, int(x1) - 1, 4):
        box('rwin', (xr - 0.4, depth + 0.105, 1.5), (xr + 0.4, depth + 0.12, 2.1), dark)


def gallery(maps, v, x0=-12, x1=12):
    walk, tw = surf(maps, 'walkway')
    box('walk', (x0, -1.9, 0), (x1, -0.1, 0.12), walk, tw)
    curb, tc = surf(maps, 'concrete')
    box('curb', (x0, -1.95, -0.05), (x1, -1.9, 0.12), curb, tc)
    roof, tr = surf(maps, 'roof')
    box('canopy', (x0, -2.0, 2.55), (x1, -0.1, 2.65), roof, tr)
    trim, tt = surf(maps, 'trim')
    box('soffit', (x0, -2.0, 2.53), (x1, -0.1, 2.55), trim, tt)
    fascia, tf = surf(maps, 'fascia')
    box('cfascia', (x0, -2.05, 2.45), (x1, -2.0, 2.7), fascia, tf)
    gutter, tg = surf(maps, 'gutter')
    box('gutter', (x0, -2.15, 2.6), (x1, -2.05, 2.72), gutter, tg)
    post = flat('post', (0.05, 0.2, 0.19), 0.5)
    x = x0 + 2
    while x < x1:
        box('post', (x - 0.04, -1.85, 0.12), (x + 0.04, -1.77, 2.53), post)
        x += 4


def ground(maps, v):
    name = v.get('ground', 'gravel')
    g, tg = surf(maps, name)
    box('ground', (-80, -80, -0.3), (80, 80, -0.02), g, tg)
    if v.get('lot'):
        a, ta = surf(maps, 'lot')
        lo, hi = v['lot']
        box('lot', (lo[0], lo[1], -0.3), (hi[0], hi[1], 0.0), a, ta)
    if v.get('road'):
        r, tr = surf(maps, 'road')
        lo, hi = v['road']
        box('road', (lo[0], lo[1], -0.3), (hi[0], hi[1], 0.0), r, tr)
        stripe = flat('stripe', (0.75, 0.62, 0.2), 0.7)
        x = lo[0]
        while x < hi[0]:
            box('dash', (x, (lo[1] + hi[1]) / 2 - 0.06, 0.0), (x + 3, (lo[1] + hi[1]) / 2 + 0.06, 0.002), stripe)
            x += 6


def room(maps, v):
    W, D, H = v.get('room', [3.8, 4.6, 2.45])
    floor, tf = surf(maps, v['floor'])
    box('floor', (-W / 2, -D / 2, -0.1), (W / 2, D / 2, 0), floor, tf)
    wall, tw = surf(maps, v.get('walls', 'drywall'))
    for s in (-1, 1):
        box('wx', (s * W / 2 - 0.05 + (0.05 if s > 0 else 0), -D / 2, 0), (s * W / 2 + (0.05 if s > 0 else 0), D / 2, H), wall, tw)
        box('wy', (-W / 2, s * D / 2 - 0.05 + (0.05 if s > 0 else 0), 0), (W / 2, s * D / 2 + (0.05 if s > 0 else 0), H), wall, tw)
    if v.get('wainscot'):
        tile, tt = surf(maps, v['wainscot'])
        for s in (-1, 1):
            box('tx', (s * W / 2 - 0.06 + (0.05 if s > 0 else 0.0), -D / 2, 0), (s * W / 2 + 0.01 * (-s) + (0.05 if s > 0 else 0), D / 2, 1.06), tile, tt)
            box('ty', (-W / 2, s * D / 2 - 0.06 + (0.05 if s > 0 else 0.0), 0), (W / 2, s * D / 2 + 0.01 * (-s) + (0.05 if s > 0 else 0), 1.06), tile, tt)
    ceil, tc = surf(maps, v.get('ceiling', 'ceiling'))
    box('ceil', (-W / 2, -D / 2, H), (W / 2, D / 2, H + 0.05), ceil, tc)
    for p in v.get('props', []):
        box('prop', p[0], p[1], flat('prop', tuple(p[2]), 0.6))
    light = bpy.data.lights.new('lamp', 'POINT'); light.energy = v.get('lamp', 120); light.color = (1.0, 0.86, 0.68)
    light.shadow_soft_size = 0.15
    ob = bpy.data.objects.new('lamp', light); ob.location = v.get('lamp_at', (0.6, 0.8, 1.6)); bpy.context.collection.objects.link(ob)
    if v.get('window'):
        wl = bpy.data.lights.new('win', 'AREA'); wl.energy = v['window']; wl.size = 1.5; wl.color = (0.85, 0.9, 1.0)
        o = bpy.data.objects.new('win', wl); o.location = (0, -D / 2 + 0.06, 1.4); o.rotation_euler = (math.radians(-90), 0, 0)
        bpy.context.collection.objects.link(o)


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    maps, view_name, out = argv[0], argv[1], argv[2]
    samples, size = 24, (960, 540)
    if '--samples' in argv: samples = int(argv[argv.index('--samples') + 1])
    if '--size' in argv: size = tuple(int(x) for x in argv[argv.index('--size') + 1].split('x'))
    v = VIEWS[view_name]
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob)
    kind = v['kind']
    if kind in ('facade', 'gallery', 'lot', 'road', 'roof'):
        ground(maps, v)
        if v.get('building', True):
            building(maps, v)
        if kind == 'gallery' or v.get('gallery'):
            gallery(maps, v)
    else:
        room(maps, v)
    sc = bpy.context.scene
    world = bpy.data.worlds.new('w'); sc.world = world; world.use_nodes = True
    sky = world.node_tree.nodes.new('ShaderNodeTexSky'); sky.sky_type = 'NISHITA'
    elev, azim = v.get('sun', [55, 200])
    sky.sun_elevation = math.radians(elev); sky.sun_rotation = math.radians(azim); sky.air_density = 1.0; sky.dust_density = 1.5
    sky.sun_disc = False
    world.node_tree.links.new(sky.outputs['Color'], world.node_tree.nodes['Background'].inputs['Color'])
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = v.get('sky', 0.05 if kind in ('room', 'bath') else 0.35)
    if kind not in ('room', 'bath'):
        sun = bpy.data.lights.new('sun', 'SUN'); sun.energy = v.get('sun_energy', 3.2); sun.angle = math.radians(0.6)
        so = bpy.data.objects.new('sun', sun); bpy.context.collection.objects.link(so)
        so.rotation_euler = (math.radians(90 - elev), 0, math.radians(azim + 90))
    cam = bpy.data.cameras.new('cam'); cam.lens = v.get('lens', 28); cam.sensor_width = 36
    co = bpy.data.objects.new('cam', cam); bpy.context.collection.objects.link(co); sc.camera = co
    co.location = Vector(v['eye']); d = Vector(v['target']) - co.location
    co.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = samples
    sc.cycles.use_denoising = True; sc.cycles.seed = 0; sc.cycles.max_bounces = 4
    sc.render.resolution_x, sc.render.resolution_y = size; sc.render.resolution_percentage = 100
    sc.view_settings.view_transform = 'AgX'; sc.view_settings.exposure = v.get('exposure', 0.0)
    sc.render.image_settings.file_format = 'PNG'; sc.render.filepath = out
    bpy.ops.render.render(write_still=True)


main()
