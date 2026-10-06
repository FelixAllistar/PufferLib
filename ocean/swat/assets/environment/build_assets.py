#!/usr/bin/env python3
"""Build the small SWAT runtime slice from the unmodified CC0 house-kit sources.

Requires Python 3.9+ and Pillow (reference build: Pillow 12.3.0). No Blender,
network access, external glTF package, or runtime dependency is required.
"""
import argparse
import copy
import hashlib
import io
import json
import math
from pathlib import Path
import struct

SOURCE_HASHES = {
    'modules/door_hinged_1_2m.glb': '0ed2ec9abf607036483c78eec1dc64e1847d17af8b9be8cc65a6ca02393b5b30',
    'architecture.py': '4bd9475770e47197a3cd2259e630281bdfd46dd0b7c35dfd03a50b766d0b8262',
    'refine_materials.py': 'e30c9f1f0117e9cde1991d49809f8f1d2b950e47865c02cf0f1f5c33dc4c634f',
    'LICENSE_ARCHITECTURE.txt': '2038f0e8297e16c6d033e6c318058fb8c7e62958e0aa131beee7f21c38961900',
    'textures/LICENSES.txt': '035efa8c50953732f41309162d296db726c91ad781897f0e13542eb74c434fb2',
    'textures/alder_faded_plaster_diff.png': 'ca713620c8442f713360209cd9ca6ae802f577862a511cbacdbc64bec5cca4de',
    'textures/wood_floor_worn_diff_1k.jpg': 'db386853009b92b9edd7255b35c9f7b0d4b7de16837a7a9da8422148754bc758',
    'textures/worn_plaster_wall_diff_1k.jpg': 'f769bee35504ff713e89e89d2b64c1eb3c373dd184973ac8706310f19fd63efe',
    'textures/provenance.json': '0674fb55934fc254f217a5359df33b2342e2a896da10af2ad5f1c1c004fb007e',
    'textures/alder_faded_plaster_provenance.json': 'dc32f6726e5d6ba48fc00c525abfec39cdf4ef6f480106d35a02bb39c128fc3e',
}
COMPONENTS = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
WIDTHS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_glb(path):
    data = Path(path).read_bytes()
    assert struct.unpack_from('<III', data) == (0x46546C67, 2, len(data))
    chunks, offset = {}, 12
    while offset < len(data):
        size, kind = struct.unpack_from('<II', data, offset)
        chunks[kind] = data[offset + 8:offset + 8 + size]
        offset += 8 + size
    assert offset == len(data)
    return json.loads(chunks[0x4E4F534A]), chunks[0x004E4942]


def accessor_values(doc, binary, index):
    accessor = doc['accessors'][index]
    assert 'sparse' not in accessor and not accessor.get('normalized', False)
    view = doc['bufferViews'][accessor['bufferView']]
    code, size = COMPONENTS[accessor['componentType']]
    width = WIDTHS[accessor['type']]
    start = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    stride = view.get('byteStride', size * width)
    return [struct.unpack_from('<' + code * width, binary, start + i * stride)
            for i in range(accessor['count'])]


def bounds(vertices):
    return ([min(p[a] for p in vertices) for a in range(3)],
            [max(p[a] for p in vertices) for a in range(3)])


def connected_components(positions, indices):
    """Weld identical seam positions, then find triangle-connected components."""
    parent = list(range(len(positions)))

    def root(index):
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index

    def union(a, b):
        parent[root(a)] = root(b)

    welded = {}
    for index, position in enumerate(positions):
        if position in welded:
            union(index, welded[position])
        else:
            welded[position] = index
    for i in range(0, len(indices), 3):
        union(indices[i], indices[i + 1])
        union(indices[i], indices[i + 2])
    groups = {}
    for i in range(len(positions)):
        groups.setdefault(root(i), []).append(i)
    return list(groups.values())


class GlbWriter:
    def __init__(self):
        self.doc = {'asset': {'version': '2.0', 'generator': 'SWAT CC0 runtime slice build_assets.py'},
                    'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [], 'meshes': [],
                    'materials': [], 'accessors': [], 'bufferViews': []}
        self.binary = bytearray()

    def view(self, data, target=None):
        self.binary.extend(b'\0' * (-len(self.binary) % 4))
        view = {'buffer': 0, 'byteOffset': len(self.binary), 'byteLength': len(data)}
        if target is not None:
            view['target'] = target
        self.binary.extend(data)
        self.doc['bufferViews'].append(view)
        return len(self.doc['bufferViews']) - 1

    def accessor(self, values, kind, component_type, target, with_bounds=False):
        code, _ = COMPONENTS[component_type]
        packed = b''.join(struct.pack('<' + code * len(value), *value) for value in values)
        accessor = {'bufferView': self.view(packed, target), 'componentType': component_type,
                    'count': len(values), 'type': kind}
        if with_bounds:
            # Re-read float32 values so min/max match the actual stored bytes exactly.
            width = WIDTHS[kind]
            actual = list(struct.iter_unpack('<' + code * width, packed))
            accessor['min'], accessor['max'] = bounds(actual)
        self.doc['accessors'].append(accessor)
        return len(self.doc['accessors']) - 1

    def write(self, path):
        self.doc['buffers'] = [{'byteLength': len(self.binary)}]
        encoded = json.dumps(self.doc, separators=(',', ':'), ensure_ascii=True).encode('utf-8')
        encoded += b' ' * (-len(encoded) % 4)
        binary = bytes(self.binary) + b'\0' * (-len(self.binary) % 4)
        data = (struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(encoded) + 8 + len(binary))
                + struct.pack('<II', len(encoded), 0x4E4F534A) + encoded
                + struct.pack('<II', len(binary), 0x004E4942) + binary)
        Path(path).write_bytes(data)


def srgb(linear):
    return 12.92 * linear if linear <= 0.0031308 else 1.055 * linear ** (1.0 / 2.4) - 0.055


def save_png(image, path=None):
    output = io.BytesIO()
    image.save(output, format='PNG', optimize=False, compress_level=9)
    data = output.getvalue()
    if path:
        Path(path).write_bytes(data)
    return data


def make_door(source, target, wood):
    from PIL import Image
    doc, binary = read_glb(source)
    assert len(doc['nodes']) == len(doc['meshes']) == 1
    assert not any(k in doc['nodes'][0] for k in ('translation', 'rotation', 'scale', 'matrix'))
    source_primitives = doc['meshes'][0]['primitives']
    paint_id = next(i for i, mat in enumerate(doc['materials']) if mat['name'] == 'Old door paint')
    paint_primitive = next(p for p in source_primitives if p['material'] == paint_id)
    positions = accessor_values(doc, binary, paint_primitive['attributes']['POSITION'])
    indices = [v[0] for v in accessor_values(doc, binary, paint_primitive['indices'])]
    groups = connected_components(positions, indices)
    assert len(groups) == 17, 'Expected one slab and sixteen leaf-mounted panel rails/stiles'

    def volume(group):
        low, high = bounds([positions[i] for i in group])
        return math.prod(high[a] - low[a] for a in range(3))

    slab = set(max(groups, key=volume))
    low, high = bounds([positions[i] for i in slab])
    size = [high[a] - low[a] for a in range(3)]
    center = [(high[a] + low[a]) * 0.5 for a in range(3)]
    assert all(abs(a - b) < 1e-6 for a, b in zip(size, [1.12, 2.02, 0.044]))

    def position_map(p):
        # Positive determinant: original glTF +X width -> SWAT +Z width;
        # original glTF +Z thickness -> SWAT -X thickness; +Y stays up.
        return (-(p[2] - center[2]) / size[2],
                (p[1] - center[1]) / size[1],
                (p[0] - center[0]) / size[0])

    def normal_map(n):
        # Inverse transpose of the baked, anisotropic position transform.
        v = (-n[2] * size[2], n[1] * size[1], n[0] * size[0])
        length = math.sqrt(sum(x*x for x in v))
        return tuple(x / length for x in v)

    writer = GlbWriter()
    writer.doc['materials'] = copy.deepcopy(doc['materials'])
    for material in writer.doc['materials']:
        # Raylib 5.5 uses base color and no PBR lighting here.
        pbr = material['pbrMetallicRoughness']
        pbr['metallicFactor'] = 0.0
        pbr['roughnessFactor'] = 1.0
        for key in ('normalTexture', 'occlusionTexture', 'emissiveTexture'):
            material.pop(key, None)
    # A subtle wood-luminance modulation is baked into the source green paint.
    # This is one shared 256px RGB diffuse map, embedded in the GLB.
    color = doc['materials'][paint_id]['pbrMetallicRoughness']['baseColorFactor'][:3]
    paint = Image.new('RGB', (256, 256))
    pixels = []
    rgb_bytes = wood.tobytes()
    for i in range(0, len(rgb_bytes), 3):
        r, g, b = rgb_bytes[i:i + 3]
        luminance = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
        modulation = 0.72 + 0.28 * luminance
        pixels.append(tuple(round(255.0 * srgb(c) * modulation) for c in color))
    paint.putdata(pixels)
    paint_png = save_png(paint)
    writer.doc['images'] = [{'name': 'worn_green_door_paint_256', 'mimeType': 'image/png',
                             'bufferView': writer.view(paint_png)}]
    writer.doc['samplers'] = [{'magFilter': 9729, 'minFilter': 9987, 'wrapS': 10497, 'wrapT': 10497}]
    writer.doc['textures'] = [{'sampler': 0, 'source': 0}]
    writer.doc['materials'][paint_id]['pbrMetallicRoughness'].update({
        'baseColorFactor': [1, 1, 1, 1], 'baseColorTexture': {'index': 0}})

    primitives, all_positions = [], []

    def add_primitive(source_primitive, selected, role):
        original_positions = accessor_values(doc, binary, source_primitive['attributes']['POSITION'])
        original_normals = accessor_values(doc, binary, source_primitive['attributes']['NORMAL'])
        original_uvs = accessor_values(doc, binary, source_primitive['attributes']['TEXCOORD_0'])
        original_indices = [v[0] for v in accessor_values(doc, binary, source_primitive['indices'])]
        selected = set(range(len(original_positions))) if selected is None else selected
        chosen_indices = []
        for i in range(0, len(original_indices), 3):
            triangle = original_indices[i:i + 3]
            hits = sum(v in selected for v in triangle)
            assert hits in (0, 3), 'Selection cannot split a triangle'
            if hits:
                chosen_indices.extend(triangle)
        used = sorted(set(chosen_indices))
        remap = {old: new for new, old in enumerate(used)}
        out_positions = [position_map(original_positions[i]) for i in used]
        all_positions.extend(out_positions)
        primitive = {'attributes': {
            'POSITION': writer.accessor(out_positions, 'VEC3', 5126, 34962, True),
            'NORMAL': writer.accessor([normal_map(original_normals[i]) for i in used], 'VEC3', 5126, 34962),
            'TEXCOORD_0': writer.accessor([original_uvs[i] for i in used], 'VEC2', 5126, 34962)},
            'indices': writer.accessor([(remap[i],) for i in chosen_indices], 'SCALAR', 5123, 34963),
            'material': source_primitive['material'], 'mode': 4,
            'extras': {'role': role, 'owner': 'active_swat_door_object', 'damage_owner': 'same_as_slab'}}
        primitives.append(primitive)

    add_primitive(paint_primitive, slab, 'slab')
    add_primitive(paint_primitive, set(range(len(positions))) - slab, 'leaf_panel_trim')
    roles = {'Tarnished brass': 'knobs_escutcheons_and_hinges',
             'Door recessed': 'leaf_recessed_panels', 'Plaster exposed': 'leaf_paint_chips'}
    for primitive in source_primitives:
        if primitive is not paint_primitive:
            add_primitive(primitive, None, roles[doc['materials'][primitive['material']]['name']])
    logical_bounds = {'min': [-0.5, -0.5, -0.5], 'max': [0.5, 0.5, 0.5]}
    writer.doc['nodes'] = [{'name': 'door_leaf', 'mesh': 0, 'extras': {
        'owner': 'active_swat_door_object', 'pivot': 'slab_center',
        'axes': {'thickness': 'X', 'height': 'Y', 'width': 'Z'},
        'logical_slab_bounds': logical_bounds, 'slab_primitive': 0,
        'source_logical_size_metres_xyz': [0.044, 2.02, 1.12],
        'damage_binding': 'Every primitive follows the same active flag, health, position, yaw and pitch.'}}]
    writer.doc['meshes'] = [{'name': 'door_leaf', 'primitives': primitives}]
    writer.write(target)
    actual, binary = read_glb(target)
    actual_positions = [p for prim in actual['meshes'][0]['primitives']
                        for p in accessor_values(actual, binary, prim['attributes']['POSITION'])]
    visual_low, visual_high = bounds(actual_positions)
    return {'logical_slab_bounds': logical_bounds, 'visual_bounds': {'min': visual_low, 'max': visual_high},
            'source_slab_bounds_gltf': {'min': low, 'max': high},
            'source_slab_size_gltf': size,
            'vertex_count': len(all_positions),
            'triangle_count': sum(actual['accessors'][p['indices']]['count'] // 3
                                  for p in actual['meshes'][0]['primitives']),
            'primitive_roles': [p['extras']['role'] for p in primitives],
            'embedded_image_sha256': sha256(paint_png)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Path to original house_kit directory')
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    from PIL import Image, __version__ as pillow_version
    for relative, expected in SOURCE_HASHES.items():
        actual = sha256((args.source / relative).read_bytes())
        if actual != expected:
            raise SystemExit('Source checksum mismatch: ' + relative)
    args.out.mkdir(parents=True, exist_ok=True)
    textures = [('plaster_diffuse.png', 'textures/alder_faded_plaster_diff.png', 1.8),
                ('wood_diffuse.png', 'textures/wood_floor_worn_diff_1k.jpg', 2.0)]
    assets = []
    for name, source, tile_metres in textures:
        im = Image.open(args.source / source).convert('RGB').resize((256, 256), Image.Resampling.LANCZOS)
        data = save_png(im, args.out / name)
        assets.append({'id': name.removesuffix('.png'), 'path': name, 'kind': 'diffuse_texture',
                       'size_pixels': [256, 256], 'color_space': 'sRGB', 'channels': 'RGB',
                       'sha256': sha256(data), 'bytes': len(data), 'source': source,
                       'source_sha256': SOURCE_HASHES[source], 'license': 'CC0-1.0',
                       'recommended_tile_metres': tile_metres,
                       'modifications': 'RGB conversion and Lanczos resize from 1024 to 256 pixels; PNG encoding.'})
        if name == 'wood_diffuse.png':
            wood = im
    door_info = make_door(args.source / 'modules/door_hinged_1_2m.glb', args.out / 'door_leaf.glb', wood)
    data = (args.out / 'door_leaf.glb').read_bytes()
    assets.append({'id': 'door_leaf', 'path': 'door_leaf.glb', 'kind': 'glb_model',
                   'sha256': sha256(data), 'bytes': len(data), 'source': 'modules/door_hinged_1_2m.glb',
                   'source_sha256': SOURCE_HASHES['modules/door_hinged_1_2m.glb'], 'license': 'CC0-1.0',
                   'modifications': ['Preserve all source geometry: separate base slab from leaf-mounted trim by connected components.',
                                     'Bake centered normalized SWAT frame and inverse-transpose normals into vertices.',
                                     'Keep one identity-transform node, no casing/frame, no pre-open rotation.',
                                     'Embed one 256px RGB paint diffuse: source paint linear color converted to sRGB, multiplied by (0.72 + 0.28 * wood Rec.709 encoded RGB luminance).',
                                     'Remove metallic response; use diffuse-only material factors with no PBR texture maps.'],
                   **door_info})
    provenance = json.loads((args.source / 'textures/provenance.json').read_text())
    selected_provenance = [record for record in provenance if record['channel'] == 'Diffuse']
    derivative = json.loads((args.source / 'textures/alder_faded_plaster_provenance.json').read_text())
    licenses = [
        ('LICENSE_ARCHITECTURE.txt', 'LICENSE_SOURCE_ARCHITECTURE.txt'),
        ('textures/LICENSES.txt', 'LICENSE_SOURCE_TEXTURES.txt'),
    ]
    for source, destination in licenses:
        (args.out / destination).write_bytes((args.source / source).read_bytes())
    catalog = {'schema': 'swat_environment_runtime_assets_v1', 'license': 'CC0-1.0',
               'license_url': 'https://creativecommons.org/publicdomain/zero/1.0/',
               'provenance_basis': 'Verified source files and their supplied license/provenance records; no new authorship or external verification claims.',
               'source_package': 'house_kit / 318 Alder rundown three-bedroom house',
               'source_files': [{'path': p, 'sha256': h} for p, h in SOURCE_HASHES.items()],
               'source_texture_records': selected_provenance,
               'source_plaster_derivative_record': derivative,
               'source_license_copies': [d for s, d in licenses],
               'assets': assets,
               'runtime_contract': {
                   'target': 'PufferLib ocean/swat, Raylib 5.5 non-PBR',
                   'coordinate_system': 'Right-handed; X thickness, Y up/height, Z width; pivot at slab center.',
                   'source_gltf_mapping': 'x = -source_z / 0.044; y = (source_y - 1.01) / 2.02; z = (source_x - 0.56) / 1.12. Actual source float32 slab extrema determine the exact affine transform.',
                   'source_blender_mapping': 'Source (width x, thickness y, height z) -> centered (thickness y, height z, width x), each divided by its slab dimension.',
                   'slab_scale': 'Scale each local axis by 2 * o.half on the corresponding axis. Do not fit to whole-model visual bounds.',
                   'placement': 'Then apply the authoritative object yaw and pitch with the engine OBB convention, and translate by o.pos. No extra source door swing or hinge transform.',
                   'damage_ownership': 'All five primitives, including knobs, hinges, panels and chips, belong to one active/health SWAT door object. Draw and hide all together. No independent static door render or collision.',
                   'collision': 'Existing SWAT object OBB uses logical unit slab; knobs/hinges/trim are render-only protrusions.',
                   'environment_surfaces': 'Diffuse texture overrides only. Each wall/floor render remains owned by its existing active/health SWAT object.',
                   'fallback': 'The game must retain its current procedural draw when either texture or the model fails to load.',
                   'materials': 'Standard glTF baseColor textures/factors only; no normal, occlusion, roughness or metallic maps; image embedded in GLB.'},
               'rebuild': {'script': 'build_assets.py', 'dependencies': ['Python >=3.9', 'Pillow'],
                           'reference_pillow_version': pillow_version,
                           'command': 'python3 build_assets.py --source /path/to/house_kit',
                           'source_mutation': False},
               'runtime_total_bytes': sum(a['bytes'] for a in assets)}
    (args.out / 'catalog.json').write_text(json.dumps(catalog, indent=2, ensure_ascii=True) + '\n')
    print(json.dumps({'assets': [{'path': a['path'], 'bytes': a['bytes']} for a in assets],
                      'runtime_total_bytes': catalog['runtime_total_bytes'], 'door': door_info}, indent=2))


if __name__ == '__main__':
    main()
