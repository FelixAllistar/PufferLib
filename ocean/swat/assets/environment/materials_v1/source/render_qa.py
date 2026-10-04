#!/usr/bin/env python3
"""Rebuild neutral material QA without running or changing the SWAT engine.

python3 source/render_qa.py [--samples 48] [--scene-out /tmp/swat-material-qa.blend]

Requires Pillow plus Blender 3.6+/4.x (CPU Cycles). All sheets use actual PNG
pixels, never an artist's concept render. The room comparison changes ONLY wall
and floor materials. Geometry, door/framing fixtures, UVs, camera, exposure, and
lighting are held constant. Door/framing fixtures use new basecolor-only maps in all rooms. Missing legacy roughness/normal maps are not invented:
legacy wall/floor use roughness 0.7 and a flat normal. New materials use supplied
roughness and tangent-space OpenGL normals without adjustment.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

FAMILIES = (
    ('plaster_painted', (1.0, 1.0), 'Painted plaster'),
    ('plaster_worn', (2.0, 2.0), 'Worn plaster'),
    ('floor_pine', (2.0, 2.0), 'Pine floor'),
    ('framing_pine', (0.4, 2.0), 'Pine framing'),
    ('door_paint', (0.6, 2.0), 'Door paint'),
    ('door_wood', (0.6, 2.0), 'Door wood'),
)


def parse_args():
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--samples', type=int, default=48)
    p.add_argument('--blender', default=shutil.which('blender') or '/usr/bin/blender')
    p.add_argument('--scene-out', type=Path, default=Path('/tmp/swat-material-qa.blend'))
    p.add_argument('--blender-stage', action='store_true', help=argparse.SUPPRESS)
    p.add_argument('--skip-render', action='store_true', help='Recompose from existing qa/_renders.')
    p.add_argument('--keep-intermediates', action='store_true')
    return p.parse_args(args)


def input_hashes(root):
    """Hash every real texture that the render and pixel sheets depend on."""
    inputs = [root / f'{name}_{channel}.png' for name, _, _ in FAMILIES for channel in ('basecolor', 'normal', 'roughness')]
    inputs += [root.parent / p for p in ('plaster_diffuse.png', 'wood_diffuse.png', 'painted_plaster_basecolor_v1.png')]
    missing = [str(p) for p in inputs if not p.is_file()]
    if missing:
        raise SystemExit('Missing required actual maps (no placeholders are rendered):\n' + '\n'.join(missing))
    return {str(p.relative_to(root.parent)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}


def verified_render_metadata(root, render_dir):
    """Fail closed on stale, partial, altered, or pre-provenance render caches."""
    path = render_dir / 'blender_metadata.json'
    try:
        meta = json.loads(path.read_text())
    except (OSError, ValueError) as exc:
        raise SystemExit('No valid render-time metadata. Run QA without --skip-render.') from exc
    if meta.get('render_script_sha256') != hashlib.sha256(Path(__file__).read_bytes()).hexdigest():
        raise SystemExit('Renderer script differs from the cached render. Run QA without --skip-render.')
    if meta.get('complete') is not True or meta.get('inputs_sha256') != input_hashes(root):
        raise SystemExit('Render-time input hashes do not match current maps. Run QA without --skip-render.')
    rendered = meta.get('rendered_images_sha256')
    expected = {f'closeup_{name}.png' for name, _, _ in FAMILIES}
    expected |= {f'room_{mode}.png' for mode in ('legacy', 'provisional', 'new', 'new_basecolor')}
    if not isinstance(rendered, dict) or set(rendered) != expected:
        raise SystemExit('Render cache is incomplete. Run QA without --skip-render.')
    for name, digest in rendered.items():
        path = render_dir / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise SystemExit(f'Render cache changed: {name}. Run QA without --skip-render.')
    return meta


def font(size):
    from PIL import ImageFont
    path = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    return ImageFont.truetype(path, size) if Path(path).exists() else ImageFont.load_default()


def basecolor_sheet(root, out):
    from PIL import Image, ImageDraw
    entries = [
        ('PRESERVED SOURCE / plaster', root.parent / 'plaster_diffuse.png', '1.8 x 1.8 m per tile; 256 px'),
        ('PRESERVED SOURCE / wood', root.parent / 'wood_diffuse.png', '2 x 2 m per tile; 256 px'),
        ('PROVISIONAL / painted plaster', root.parent / 'painted_plaster_basecolor_v1.png', '1 x 1 m per tile; 1254 px'),
    ] + [(f'NEW / {label}', root / f'{name}_basecolor.png', f'{u:g} x {v:g} m per tile; 512 px') for name, (u, v), label in FAMILIES]
    width, cell, pad, imgsize = 1140, 380, 24, 332
    sheet = Image.new('RGB', (width, 3 * 430 + 122), '#ededeb')
    d = ImageDraw.Draw(sheet)
    d.text((24, 19), 'Actual basecolor maps | no light, grading, or AO added', fill='#202322', font=font(25))
    d.text((24, 59), 'One complete UV tile each, shown square to inspect pixels. Metric tile proportions are labeled.', fill='#565b59', font=font(16))
    for i, (label, path, subtitle) in enumerate(entries):
        x, y = (i % 3) * cell + pad, (i // 3) * 430 + 104
        with Image.open(path) as im:
            sheet.paste(im.convert('RGB').resize((imgsize, imgsize), Image.Resampling.LANCZOS), (x, y))
        d.text((x, y + 344), label, fill='#202322', font=font(16))
        d.text((x, y + 373), subtitle, fill='#565b59', font=font(15))
    d.text((24, sheet.height - 26), 'New normal and roughness maps are evaluated separately in the neutral-lit sheets. Grain follows +V.', fill='#565b59', font=font(15))
    sheet.save(out / '01_basecolor_pixels.png', optimize=True)


def compose_sheet(out):
    from PIL import Image, ImageDraw
    temp = out / '_renders'
    sheet = Image.new('RGB', (1200, 988), '#ededeb')
    d = ImageDraw.Draw(sheet)
    d.text((24, 18), 'Neutral material closeups | supplied basecolor + normal + roughness', fill='#202322', font=font(24))
    d.text((24, 55), 'Fixed camera and light; 1 x 1 m face; OpenGL normals; no added color correction or surface noise.', fill='#565b59', font=font(16))
    for i, (name, (u, v), label) in enumerate(FAMILIES):
        x, y = (i % 3) * 400, 91 + (i // 3) * 421
        with Image.open(temp / f'closeup_{name}.png') as im:
            sheet.paste(im.convert('RGB').resize((400, 360), Image.Resampling.LANCZOS), (x, y))
        d.text((x + 20, y + 370), label, fill='#202322', font=font(19))
        d.text((x + 20, y + 397), f'Tile {u:g} x {v:g} m / identical 1 m crop', fill='#565b59', font=font(15))
    d.text((24, 950), 'Cycles CPU | Standard / None | exposure 0 | gamma 1 | world 0.35 + 5 m softbox | normal strength 1', fill='#565b59', font=font(15))
    sheet.save(out / '02_neutral_closeups.jpg', quality=93, subsampling=0, optimize=True)

    modes = [
        ('legacy', 'PRESERVED: plaster 1.8 m / wood 2 m'),
        ('provisional', 'PROVISIONAL: generated plaster 1 m / source wood 2 m'),
        ('new', 'NEW PBR: painted plaster 1 m / pine floor 2 m'),
        ('new_basecolor', 'NEW BASECOLOR ONLY: same new maps / flat normals'),
    ]
    sheet = Image.new('RGB', (1680, 1350), '#ededeb')
    d = ImageDraw.Draw(sheet)
    d.text((24, 20), 'Same-light room comparison | wall and floor only change', fill='#202322', font=font(28))
    d.text((24, 65), 'Door/framing use fixed new basecolor-only maps in every view. Legacy, provisional and basecolor-only roughness = 0.7.', fill='#565b59', font=font(18))
    d.text((24, 96), 'Same geometry, metric UV scales, camera, exposure and lights. Offline material test; these are not engine screenshots.', fill='#565b59', font=font(18))
    for i, (mode, title) in enumerate(modes):
        x, y = (i % 2) * 840, 148 + (i // 2) * 585
        d.text((x + 20, y), title, fill='#202322', font=font(20))
        with Image.open(temp / f'room_{mode}.png') as im:
            sheet.paste(im.convert('RGB').resize((840, 525), Image.Resampling.LANCZOS), (x, y + 37))
    sheet.save(out / '03_same_light_room_comparison.jpg', quality=93, subsampling=0, optimize=True)
    for mode, filename, title in [
        ('new', '04_new_materials_room.jpg', 'New materials | full PBR wall and floor test'),
        ('new_basecolor', '06_basecolor_only_room.jpg', 'New materials | basecolor-only wall, floor and fixtures'),
    ]:
        with Image.open(temp / f'room_{mode}.png') as im:
            room = im.convert('RGB')
        c = Image.new('RGB', (1120, 822), '#ededeb')
        c.paste(room, (0, 84))
        d = ImageDraw.Draw(c)
        d.text((24, 16), title, fill='#202322', font=font(25))
        d.text((24, 52), 'Wall 4 x 3 m / floor 4 x 4 m / door 0.90 x 2.00 m / framing 90 mm / rulers: 1 m divisions', fill='#565b59', font=font(16))
        d.text((24, 798), 'Wood sample: 0.6 x 1.8 m. Gray card and ruler are geometry. All room views use fixed basecolor-only fixtures.', fill='#565b59', font=font(15))
        c.save(out / filename, quality=94, subsampling=0, optimize=True)

    # Seam evidence at repeated UVs; explicit grid lines are not overlaid on maps.
    entries = [('plaster_painted', 'Painted plaster / 3 x 3 m'), ('plaster_worn', 'Worn plaster / 6 x 6 m'), ('floor_pine', 'Pine floor / 6 x 6 m')]
    tile = Image.new('RGB', (1200, 500), '#ededeb')
    d = ImageDraw.Draw(tile)
    d.text((20, 16), '3 x 3 repeat test | raw basecolor; no lines masking tile boundaries', fill='#202322', font=font(23))
    for i, (name, label) in enumerate(entries):
        with Image.open(out.parent / f'{name}_basecolor.png') as im:
            im = im.convert('RGB').resize((128, 128), Image.Resampling.LANCZOS)
        for y in range(3):
            for x in range(3):
                tile.paste(im, (i * 400 + 8 + x * 128, 65 + y * 128))
        d.text((i * 400 + 12, 465), label, fill='#202322', font=font(17))
    tile.save(out / '05_raw_tiling_test.jpg', quality=94, subsampling=0, optimize=True)


def render_blender(args):
    import bpy
    from mathutils import Vector

    root = args.assets.resolve()
    out = root / 'qa' / '_renders'
    out.mkdir(parents=True, exist_ok=True)
    metadata_path = out / 'blender_metadata.json'
    metadata_path.unlink(missing_ok=True)  # A failed render cannot reuse a prior success record.
    render_inputs = input_hashes(root)  # Capture BEFORE Blender loads any pixels.
    render_script_sha256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = args.samples
    scene.cycles.seed = 20261004
    scene.cycles.use_denoising = False
    scene.cycles.max_bounces = 6
    scene.cycles.diffuse_bounces = 3
    scene.cycles.glossy_bounces = 3
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.color_depth = '8'
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0
    world = bpy.data.worlds.new('Neutral studio world')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.78, 0.78, 0.78, 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.35
    scene.world = world

    def material(name, color=None, roughness=0.7, image=None, normal=None, rough=None):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nodes, links = m.node_tree.nodes, m.node_tree.links
        bs = nodes.get('Principled BSDF')
        bs.inputs['Roughness'].default_value = roughness
        if color:
            bs.inputs['Base Color'].default_value = (*color, 1)
        def tex(path, space):
            node = nodes.new('ShaderNodeTexImage')
            node.image = bpy.data.images.load(str(path), check_existing=True)
            node.image.colorspace_settings.name = space
            node.extension = 'REPEAT'
            node.interpolation = 'Linear'
            return node
        if image:
            links.new(tex(image, 'sRGB').outputs['Color'], bs.inputs['Base Color'])
        if rough:
            links.new(tex(rough, 'Non-Color').outputs['Color'], bs.inputs['Roughness'])
        if normal:
            n = nodes.new('ShaderNodeNormalMap')
            n.space = 'TANGENT'
            n.uv_map = 'UVMap'
            n.inputs['Strength'].default_value = 1.0
            links.new(tex(normal, 'Non-Color').outputs['Color'], n.inputs['Color'])
            links.new(n.outputs['Normal'], bs.inputs['Normal'])
        return m

    mats = {name: material(name, image=root / f'{name}_basecolor.png', normal=root / f'{name}_normal.png', rough=root / f'{name}_roughness.png') for name, _, _ in FAMILIES}
    dims = {name: tile for name, tile, _ in FAMILIES}
    base_only = {name: material(name + ' basecolor only', image=root / f'{name}_basecolor.png') for name, _, _ in FAMILIES}
    legacy_wall = material('preserved source plaster', image=root.parent / 'plaster_diffuse.png')
    legacy_floor = material('preserved source wood', image=root.parent / 'wood_diffuse.png')
    provisional_wall = material('provisional generated plaster', image=root.parent / 'painted_plaster_basecolor_v1.png')
    gray = material('18 percent neutral gray', color=(0.18, 0.18, 0.18))
    white = material('matte ruler', color=(0.63, 0.63, 0.63))
    dark = material('ruler markings', color=(0.035, 0.035, 0.035))
    brass = material('fixed neutral knob', color=(0.20, 0.20, 0.20), roughness=0.42)

    def mesh(name, verts, faces, uvs, mat):
        data = bpy.data.meshes.new(name)
        data.from_pydata(verts, [], faces)
        data.update()
        uv = data.uv_layers.new(name='UVMap')
        for poly, coords in zip(data.polygons, uvs):
            for index, co in zip(poly.loop_indices, coords):
                uv.data[index].uv = co
        obj = bpy.data.objects.new(name, data)
        scene.collection.objects.link(obj)
        obj.data.materials.append(mat)
        return obj

    def face(name, w, h, tile, mat, loc=(0, 0, 0), floor=False):
        verts = [(-w/2, 0, 0), (w/2, 0, 0), (w/2, 0, h), (-w/2, 0, h)]
        if floor:
            verts = [(-w/2, -h/2, 0), (w/2, -h/2, 0), (w/2, h/2, 0), (-w/2, h/2, 0)]
        obj = mesh(name, verts, [(0, 1, 2, 3)], [[(0, 0), (w/tile[0], 0), (w/tile[0], h/tile[1]), (0, h/tile[1])]], mat)
        obj.location = loc
        return obj

    def box(name, size, loc, mat, tile=(1, 1), long_axis='z', bevel=0.002):
        x, y, z = (v/2 for v in size)
        verts = [(-x,-y,-z),(x,-y,-z),(x,y,-z),(-x,y,-z),(-x,-y,z),(x,-y,z),(x,y,z),(-x,y,z)]
        faces = [(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7),(0,3,2,1)]
        coords = []
        long_index = 0 if long_axis == 'x' else 2
        for f in faces:
            varying = [axis for axis in range(3) if len({verts[i][axis] for i in f}) > 1]
            v_axis = long_index if long_index in varying else varying[-1]
            u_axis = next(axis for axis in varying if axis != v_axis)
            # End faces merely show a transverse projection, not authored endgrain.
            coords.append([(verts[i][u_axis]/tile[0], verts[i][v_axis]/tile[1]) for i in f])
        obj = mesh(name, verts, faces, coords, mat)
        obj.location = loc
        if bevel:
            mod = obj.modifiers.new('Small physical edge bevel', 'BEVEL')
            mod.width, mod.segments = bevel, 2
        return obj

    def point_at(obj, target):
        obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()

    cam_data = bpy.data.cameras.new('Fixed neutral camera')
    cam = bpy.data.objects.new('Fixed neutral camera', cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam.data.type = 'ORTHO'
    light_data = bpy.data.lights.new('Large neutral softbox', 'AREA')
    light_data.energy = 420
    light_data.shape = 'DISK'
    light_data.size = 5.0
    light = bpy.data.objects.new('Large neutral softbox', light_data)
    scene.collection.objects.link(light)
    light.location = (-3.5, -4.5, 6.0)
    point_at(light, (0, 0.0, 1.0))

    # One camera and an identical flat 1 m panel for all six closeups.
    panel = face('1 m square closeup', 1.0, 1.0, (1, 1), mats['plaster_painted'])
    cam.location = (0.12, -3.8, 0.67)
    point_at(cam, (0, 0, 0.5))
    cam.data.ortho_scale = 1.16
    scene.render.resolution_x, scene.render.resolution_y = 400, 360
    for name, tile, _ in FAMILIES:
        panel.data.materials[0] = mats[name]
        for li, co in zip(panel.data.polygons[0].loop_indices, [(0,0),(1/tile[0],0),(1/tile[0],1/tile[1]),(0,1/tile[1])]):
            panel.data.uv_layers[0].data[li].uv = co
        scene.render.filepath = str(out / f'closeup_{name}.png')
        bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(panel, do_unlink=True)

    wall = face('4 x 3 m wall', 4, 3, dims['plaster_painted'], mats['plaster_painted'], (0, 2, 0))
    floor = face('4 x 4 m floor', 4, 4, dims['floor_pine'], mats['floor_pine'], floor=True)
    # Fixtures remain identical in every room render. These are material test
    # shapes, not a replacement for, or modification of, door_leaf.glb.
    box('painted door 0.90 x 2.00 m', (0.9,0.044,2), (0.94,1.955,1), base_only['door_paint'], dims['door_paint'], bevel=0.004)
    for x in (0.94-0.50, 0.94+0.50):
        box('90 mm pine casing', (0.09,0.085,2.08), (x,1.918,1.04), base_only['framing_pine'], dims['framing_pine'])
    box('90 mm pine header', (1.09,0.085,0.09), (0.94,1.918,2.125), base_only['framing_pine'], dims['framing_pine'], long_axis='x')
    for x in (0.63,1.25):
        box('painted raised door stile', (0.06,0.02,1.78), (x,1.920,1.0), base_only['door_paint'], dims['door_paint'])
    for z in (0.18,1.08,1.85):
        box('painted door rail', (0.61,0.02,0.065), (0.94,1.918,z), base_only['door_paint'], dims['door_paint'], long_axis='x')
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.034, location=(0.59,1.850,1.03))
    bpy.context.object.name = 'fixed gray knob'
    bpy.context.object.data.materials.append(brass)
    box('door wood witness board 0.6 x 1.8 m', (0.6,0.04,1.8), (-0.90,1.955,0.93), base_only['door_wood'], dims['door_wood'])
    box('pine witness stud 90 mm', (0.09,0.09,2.4), (-1.72,1.91,1.2), base_only['framing_pine'], dims['framing_pine'])
    # Physically separate 18% card; lets reviewers recognize tone changes.
    box('18 percent gray reference', (0.23,0.02,0.23), (-1.40,1.968,2.57), gray, bevel=0)
    # Rulers outside the sample; metre ticks are geometry, not texture marks.
    box('4 metre horizontal ruler', (4,0.09,0.025), (0,-2.11,0.018), white, bevel=0)
    for i in range(9):
        box('half metre floor tick', (0.015,0.075 if i%2==0 else 0.040,0.006), (-2+i*0.5,-2.11,0.034), dark, bevel=0)
    box('3 metre wall ruler', (0.075,0.028,3), (-2.10,2,1.5), white, bevel=0)
    for i in range(7):
        box('half metre wall tick', (0.065 if i%2==0 else 0.035,0.008,0.015), (-2.10,1.98,i*0.5), dark, bevel=0)

    cam.location = (4.7, -7.1, 4.5)
    point_at(cam, (0,0.20,1.02))
    scene.render.resolution_x, scene.render.resolution_y = 1120, 700
    # Fit complete metric fixture bounds, including rulers and the gray card.
    # Freeze this framing before any material swaps.
    bpy.context.view_layer.update()
    inv = cam.matrix_world.inverted()
    points = [inv @ (obj.matrix_world @ vertex.co) for obj in scene.objects if obj.type == 'MESH' for vertex in obj.data.vertices]
    xmin, xmax = min(p.x for p in points), max(p.x for p in points)
    ymin, ymax = min(p.y for p in points), max(p.y for p in points)
    cam.location += cam.rotation_euler.to_matrix() @ Vector(((xmin+xmax)/2, (ymin+ymax)/2, 0))
    cam.data.ortho_scale = max(xmax-xmin, (ymax-ymin)*1120/700) * 1.09
    for mode, wm, fm, wt, ft in [
        ('legacy', legacy_wall, legacy_floor, (1.8,1.8), (2,2)),
        ('provisional', provisional_wall, legacy_floor, (1,1), (2,2)),
        ('new', mats['plaster_painted'], mats['floor_pine'], (1,1), (2,2)),
        ('new_basecolor', base_only['plaster_painted'], base_only['floor_pine'], (1,1), (2,2)),
    ]:
        wall.data.materials[0], floor.data.materials[0] = wm, fm
        for obj, w, h, tile in [(wall,4,3,wt),(floor,4,4,ft)]:
            for li, co in zip(obj.data.polygons[0].loop_indices, [(0,0),(w/tile[0],0),(w/tile[0],h/tile[1]),(0,h/tile[1])]):
                obj.data.uv_layers[0].data[li].uv = co
        scene.render.filepath = str(out / f'room_{mode}.png')
        bpy.ops.render.render(write_still=True)
    if input_hashes(root) != render_inputs:
        raise SystemExit('Inputs changed during the Blender render. Render cache is invalid; rerun QA.')
    scene_packed = False
    if args.scene_out:
        # Embed file textures so the saved scene can move to another machine.
        bpy.ops.file.pack_all()
        scene_packed = all(image.packed_file is not None for image in bpy.data.images if image.source == 'FILE')
        if not scene_packed:
            raise SystemExit('Could not pack all scene textures; no success metadata written.')
        args.scene_out.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(args.scene_out.resolve()))
    if input_hashes(root) != render_inputs:
        raise SystemExit('Inputs changed before final metadata was written; rerun QA.')
    if hashlib.sha256(Path(__file__).read_bytes()).hexdigest() != render_script_sha256:
        raise SystemExit('Renderer script changed during execution; rerun QA.')
    rendered = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.glob('*.png'))}
    metadata_path.write_text(json.dumps({'complete': True, 'blender': bpy.app.version_string, 'engine': 'Cycles CPU', 'samples': args.samples, 'seed': 20261004, 'denoising': False, 'inputs_sha256': render_inputs, 'rendered_images_sha256': rendered, 'scene_textures_packed': scene_packed, 'render_script_sha256': render_script_sha256}, indent=2) + '\n')


def main():
    args = parse_args()
    args.assets = args.assets.resolve()
    if args.blender_stage:
        render_blender(args)
        return
    out = args.assets / 'qa'
    out.mkdir(parents=True, exist_ok=True)
    before = input_hashes(args.assets)
    if not args.skip_render:
        subprocess.run([args.blender, '--background', '--threads', '8', '--python-exit-code', '1', '--python', str(Path(__file__).resolve()), '--', '--blender-stage', '--assets', str(args.assets), '--samples', str(args.samples), '--scene-out', str(args.scene_out)], check=True)
    # Validate the original Blender snapshot BEFORE writing any user-facing sheet.
    # --skip-render must never relabel old pixels with hashes captured only now.
    meta = verified_render_metadata(args.assets, out / '_renders')
    if meta['inputs_sha256'] != before:
        raise SystemExit('Input maps changed during QA. Run QA again for consistent captured inputs.')
    basecolor_sheet(args.assets, out)
    compose_sheet(out)
    if input_hashes(args.assets) != meta['inputs_sha256']:
        raise SystemExit('Inputs changed during composition; rerun QA. No report was updated.')
    meta.update({'purpose': 'Offline actual-map QA, not an in-engine screenshot.', 'view_transform': 'Standard', 'look': 'None', 'exposure': 0, 'gamma': 1, 'normal_convention': 'Tangent OpenGL +Y/+V; strength 1', 'basecolor_space': 'sRGB', 'data_map_space': 'Non-Color', 'legacy_assumptions': 'Absent roughness uses 0.7; absent normals are flat.', 'comparison_invariant': 'Room geometry, camera, lighting, exposure, gray card, and new basecolor-only door/framing fixtures are identical across all four. Only wall/floor material and their catalogued metric UV repeat change.', 'room_dimensions_m': {'wall':[4,3], 'floor':[4,4], 'door':[0.90,2.00], 'framing_width':0.09}, 'closeup_dimensions_m':[1,1], 'outputs': [p.name for p in sorted(out.glob('*')) if p.is_file() and p.name != 'render_report.json']})
    (out / 'render_report.json').write_text(json.dumps(meta, indent=2) + '\n')
    if not args.keep_intermediates:
        shutil.rmtree(out / '_renders')
    print('Neutral QA written to', out)
    print('Reusable packed Blender scene:', args.scene_out)


if __name__ == '__main__':
    main()
