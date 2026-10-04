#!/usr/bin/env python3
"""Build six compact, render-only SWAT props from Household Clutter Batch 02.

Usage: python3 build_props.py --source /path/to/02_household_clutter [--out DIR]
Requires Python 3.9+ and Pillow. Reference encoder: Pillow 12.3.0. No Blender,
network, source mutation, geometry simplification or external glTF package.
The GLB geometry streams are copied byte-for-byte; only texture resolution,
material response and non-runtime metadata change. See props_catalog.json.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct

ASSET_IDS = (
    'chipped_coffee_mug', 'stacked_dinner_plates', 'folded_hand_towel',
    'television_remote', 'brass_eyeglasses', 'creased_leather_wallet',
)
SOURCE_HASHES = {
    'modules/chipped_coffee_mug.glb': 'd1fd3c6091cb1fdeba088c3dbb0f4ae0fa998f0a35d84d01cd1d33d52b39a4d0',
    'modules/stacked_dinner_plates.glb': 'e2f3a922846a5f4ee47e0955ae47d1a8c7b01ec500088136687b4483c3434527',
    'modules/folded_hand_towel.glb': 'f95b3ccdd98ea1805c5a1334222138d8bd3c1166a523e187abefa46a51eac754',
    'modules/television_remote.glb': '76f7219967cd28f9895d3e79bd265bc4e49f4e0c58c00d54e3eea20fb7eb0f60',
    'modules/brass_eyeglasses.glb': '67e9bdd4fa495cbe13c01d36422f17e424d7d7a57c8df1304ce022e9f0e57068',
    'modules/creased_leather_wallet.glb': 'adb0116489a06e5892f10a9178695282645cd7c16cafa4cbcd75232dd2b1b546',
    'manifest.json': 'b6d68cddd66916da9f02958ef61656464f6ab9290d1bb89a760fa6d7c65d8a91',
    'LICENSE.txt': 'f26e4507daa86f1a1ed8a2c666d18599cd4358447f6a2faadad054c30f5b9d5c',
    'README.md': '458ecde446fd723ea060ca1513d245b00799f5b1469677be361887bbe554935d',
    'build_clutter.py': '1148c179c7a8fd20d68b9bef7940135d30bdb358b4feefd8a96a1a3895b11aae',
    'refine_geometry.py': '6c4a8223033f119e4de48149f3694dae7eedb76a26474331b24a873fa948ee4e',
}
TEXTURE_SIZE = 128
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
COMPONENTS = {5123: ('H', 2), 5126: ('f', 4)}
WIDTHS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_glb(path):
    data = Path(path).read_bytes()
    require(len(data) >= 28, 'Truncated GLB: ' + str(path))
    require(struct.unpack_from('<III', data) == (0x46546C67, 2, len(data)), 'Invalid GLB header')
    chunks, offset = {}, 12
    while offset < len(data):
        require(offset + 8 <= len(data), 'Truncated GLB chunk')
        length, kind = struct.unpack_from('<II', data, offset)
        require(length % 4 == 0 and offset + 8 + length <= len(data), 'Invalid GLB chunk size')
        require(kind not in chunks, 'Duplicate GLB chunk')
        chunks[kind] = data[offset + 8:offset + 8 + length]
        offset += 8 + length
    require(list(chunks) == [0x4E4F534A, 0x004E4942], 'Expected JSON then BIN chunks only')
    return json.loads(chunks[0x4E4F534A]), chunks[0x004E4942]


def accessor_bytes(doc, binary, index):
    """Canonical tightly-packed bytes, retaining every float/index bit."""
    accessor = doc['accessors'][index]
    require('sparse' not in accessor and not accessor.get('normalized', False), 'Unsupported accessor')
    require(accessor['componentType'] in COMPONENTS and accessor['type'] in WIDTHS, 'Unsupported format')
    view = doc['bufferViews'][accessor['bufferView']]
    require(view['buffer'] == 0, 'Unexpected buffer')
    size = COMPONENTS[accessor['componentType']][1] * WIDTHS[accessor['type']]
    offset = accessor.get('byteOffset', 0)
    stride = view.get('byteStride', size)
    count = accessor['count']
    require(count > 0 and stride >= size, 'Invalid accessor count/stride')
    require(offset >= 0 and offset + (count - 1) * stride + size <= view['byteLength'], 'Accessor exceeds view')
    start = view.get('byteOffset', 0) + offset
    require(start >= 0 and start + (count - 1) * stride + size <= len(binary), 'Accessor exceeds buffer')
    return b''.join(binary[start + i * stride:start + i * stride + size] for i in range(count))


def accessor_values(doc, binary, index):
    accessor = doc['accessors'][index]
    code = COMPONENTS[accessor['componentType']][0] * WIDTHS[accessor['type']]
    return list(struct.iter_unpack('<' + code, accessor_bytes(doc, binary, index)))


def bounds(points):
    return {'min': [min(p[i] for p in points) for i in range(3)],
            'max': [max(p[i] for p in points) for i in range(3)]}


def geometry_records(doc, binary):
    """Hashes include ordered source streams, preventing unnoticed geometry loss."""
    result = []
    for primitive in doc['meshes'][0]['primitives']:
        streams = {name: accessor_bytes(doc, binary, primitive['attributes'][name])
                   for name in ('POSITION', 'NORMAL', 'TEXCOORD_0')}
        streams['indices'] = accessor_bytes(doc, binary, primitive['indices'])
        result.append({
            'material': primitive['material'],
            'vertices': doc['accessors'][primitive['attributes']['POSITION']]['count'],
            'triangles': doc['accessors'][primitive['indices']]['count'] // 3,
            'stream_sha256': {name: sha256(raw) for name, raw in streams.items()},
        })
    return result


class Writer:
    def __init__(self, asset_id):
        self.doc = {
            'asset': {'version': '2.0', 'generator': 'SWAT compact CC0 props build_props.py'},
            'scene': 0, 'scenes': [{'nodes': [0]}],
            'nodes': [{'name': 'prop_' + asset_id, 'mesh': 0, 'extras': {
                'asset_id': asset_id, 'license': 'CC0-1.0', 'units': 'metres',
                'up': '+Y', 'front': '+Z', 'pivot': 'XZ AABB centre; minimum Y=0',
                'render_only': True, 'collision': 'none',
            }}],
            'meshes': [{'name': 'prop_' + asset_id, 'primitives': []}],
            'materials': [], 'images': [], 'textures': [],
            'samplers': [{'magFilter': 9729, 'minFilter': 9729, 'wrapS': 10497, 'wrapT': 10497}],
            'accessors': [], 'bufferViews': [],
        }
        self.binary = bytearray()

    def view(self, raw, target=None):
        self.binary.extend(b'\0' * (-len(self.binary) % 4))
        view = {'buffer': 0, 'byteOffset': len(self.binary), 'byteLength': len(raw)}
        if target is not None:
            view['target'] = target
        self.doc['bufferViews'].append(view)
        self.binary.extend(raw)
        return len(self.doc['bufferViews']) - 1

    def copy_accessor(self, doc, binary, index, target):
        source = doc['accessors'][index]
        raw = accessor_bytes(doc, binary, index)
        accessor = {key: source[key] for key in ('componentType', 'count', 'type')}
        accessor['bufferView'] = self.view(raw, target)
        if source['type'] == 'VEC3' and 'min' in source:
            actual = bounds(list(struct.iter_unpack('<fff', raw)))
            accessor.update(actual)
        self.doc['accessors'].append(accessor)
        return len(self.doc['accessors']) - 1

    def write(self, path):
        self.doc['buffers'] = [{'byteLength': len(self.binary)}]
        encoded = json.dumps(self.doc, separators=(',', ':'), ensure_ascii=True).encode('utf-8')
        encoded += b' ' * (-len(encoded) % 4)
        binary = bytes(self.binary) + b'\0' * (-len(self.binary) % 4)
        data = (struct.pack('<III', 0x46546C67, 2, 28 + len(encoded) + len(binary))
                + struct.pack('<II', len(encoded), 0x4E4F534A) + encoded
                + struct.pack('<II', len(binary), 0x004E4942) + binary)
        Path(path).write_bytes(data)
        return data


def make_prop(source, output, asset_id, source_record):
    from PIL import Image
    doc, binary = read_glb(source)
    require(len(doc['nodes']) == len(doc['meshes']) == 1, 'Expected one mesh and identity node')
    require(not any(k in doc['nodes'][0] for k in ('translation', 'rotation', 'scale', 'matrix', 'children')),
            'Source transform changed')
    require(not any(k in doc for k in ('extensionsUsed', 'extensionsRequired', 'animations', 'skins')), 'Unexpected source feature')
    writer = Writer(asset_id)
    image_records = []
    for source_image in doc['images']:
        require('uri' not in source_image and source_image['mimeType'] == 'image/png', 'Non-embedded source image')
        view = doc['bufferViews'][source_image['bufferView']]
        png = binary[view['byteOffset']:view['byteOffset'] + view['byteLength']]
        image = Image.open(io.BytesIO(png))
        require(image.size == (256, 256) and image.mode == 'RGB', 'Source texture contract changed')
        small = image.resize((TEXTURE_SIZE, TEXTURE_SIZE), Image.Resampling.LANCZOS)
        stream = io.BytesIO()
        small.save(stream, format='PNG', optimize=False, compress_level=9)
        encoded = stream.getvalue()
        writer.doc['images'].append({'name': source_image['name'] + '_128', 'mimeType': 'image/png',
                                     'bufferView': writer.view(encoded)})
        image_records.append({'name': source_image['name'], 'source_size_pixels': [256, 256],
                              'source_sha256': sha256(png), 'source_bytes': len(png),
                              'size_pixels': [TEXTURE_SIZE, TEXTURE_SIZE], 'channels': 'RGB',
                              'sha256': sha256(encoded), 'bytes': len(encoded)})
    for texture in doc['textures']:
        require(set(texture) <= {'source', 'sampler'}, 'Unexpected source texture feature')
        writer.doc['textures'].append({'source': texture['source'], 'sampler': 0})
    for source_material in doc['materials']:
        pbr = source_material['pbrMetallicRoughness']
        material = {'name': source_material['name'], 'doubleSided': source_material.get('doubleSided', False),
                    'pbrMetallicRoughness': {'metallicFactor': 0, 'roughnessFactor': 1}}
        for key in ('baseColorFactor', 'baseColorTexture'):
            if key in pbr:
                material['pbrMetallicRoughness'][key] = pbr[key]
        writer.doc['materials'].append(material)
    for primitive in doc['meshes'][0]['primitives']:
        require(primitive.get('mode', 4) == 4, 'Source is not triangle topology')
        require(set(primitive['attributes']) == {'POSITION', 'NORMAL', 'TEXCOORD_0'}, 'Unexpected attributes')
        out = {'attributes': {name: writer.copy_accessor(doc, binary, primitive['attributes'][name], 34962)
                              for name in ('POSITION', 'NORMAL', 'TEXCOORD_0')},
               'indices': writer.copy_accessor(doc, binary, primitive['indices'], 34963),
               'material': primitive['material'], 'mode': 4}
        require(writer.doc['accessors'][out['indices']]['componentType'] == 5123, 'Raylib requires compact uint16 indices')
        writer.doc['meshes'][0]['primitives'].append(out)
    data = writer.write(output)
    actual, actual_binary = read_glb(output)
    geometry = geometry_records(actual, actual_binary)
    require(geometry == geometry_records(doc, binary), 'Geometry streams changed')
    points = [p for primitive in actual['meshes'][0]['primitives']
              for p in accessor_values(actual, actual_binary, primitive['attributes']['POSITION'])]
    aabb = bounds(points)
    require(aabb == source_record['aabb_gltf_y_up'] and aabb['min'][1] == 0, 'Surface origin changed')
    require(sum(g['triangles'] for g in geometry) == source_record['triangles_exported'], 'Triangle count changed')
    return {
        'id': asset_id, 'path': output.name, 'kind': 'glb_model', 'license': 'CC0-1.0',
        'source': 'modules/' + asset_id + '.glb', 'source_sha256': sha256(Path(source).read_bytes()),
        'source_manifest_record': source_record,
        'sha256': sha256(data), 'bytes': len(data), 'aabb_metres_xyz': aabb,
        'dimensions_metres_xyz': [aabb['max'][i] - aabb['min'][i] for i in range(3)],
        'source_to_runtime_matrix_column_major': IDENTITY,
        'mesh_count': 1, 'node_count': 1, 'material_count': len(actual['materials']),
        'primitive_count': len(geometry), 'raylib_mesh_count': len(geometry),
        'vertex_count': sum(g['vertices'] for g in geometry),
        'triangle_count': sum(g['triangles'] for g in geometry),
        'geometry': geometry, 'embedded_image_count': len(image_records), 'images': image_records,
        'external_uris': [], 'render_only': True, 'collision': 'none',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    from PIL import __version__ as pillow_version
    for path, expected in SOURCE_HASHES.items():
        require(sha256((args.source / path).read_bytes()) == expected, 'Source checksum mismatch: ' + path)
    manifest = json.loads((args.source / 'manifest.json').read_text())
    records = {a['asset_id']: a for a in manifest['assets']}
    args.out.mkdir(parents=True, exist_ok=True)
    assets = [make_prop(args.source / 'modules' / (asset_id + '.glb'),
                        args.out / ('prop_' + asset_id + '.glb'), asset_id, records[asset_id])
              for asset_id in ASSET_IDS]
    license_name = 'LICENSE_SOURCE_CLUTTER.txt'
    (args.out / license_name).write_bytes((args.source / 'LICENSE.txt').read_bytes())
    catalog = {
        'schema': 'swat_compact_props_v1', 'license': 'CC0-1.0',
        'license_url': 'https://creativecommons.org/publicdomain/zero/1.0/',
        'source_package': 'Quiet Evidence: Household Clutter Batch 02',
        'provenance_basis': 'Pinned local source files, supplied CC0 dedication and selected source manifest records. No new third-party license verification or authorship claim.',
        'source_files': [{'path': p, 'sha256': h} for p, h in SOURCE_HASHES.items()],
        'source_license_copy': {'path': license_name, 'source': 'LICENSE.txt',
                                'sha256': SOURCE_HASHES['LICENSE.txt']},
        'runtime_contract': {
            'target': 'PufferLib ocean/swat; raylib 5.5 core glTF importer and diffuse-only drawing',
            'units': 'metres', 'up': '+Y', 'front': '+Z',
            'pivot': 'Source XZ bounding-box centre; minimum Y exactly zero. Float32 centre residual under 1e-8 metre retained.',
            'transform': 'Identity. Source GLBs are already +Y up. Do not repeat Blender-to-glTF axis conversion.',
            'placement': 'Apply uniform scale and yaw, then translate the origin onto its supporting tabletop. Use catalog bounds for the scaled/rotated footprint.',
            'ownership': 'Decorative render-only children of authoritative support objects; no independent world, navigation, projectile or physics actors.',
            'collision': 'none',
            'materials': 'Core baseColorTexture/baseColorFactor only; neutral metallic=0 and roughness=1 constants; no PBR maps, normal/occlusion/emissive maps, extensions or external URIs.',
            'geometry': 'All source float32 positions/normals/UVs and uint16 triangle indices copied byte-for-byte, including primitive/material order. No decimation, welding or rebasing.',
            'raylib': 'Each triangle primitive becomes one raylib mesh; each has <65536 vertices and uint16 indices. Preserve source doubleSided flags in glTF; raylib drawing code must disable backface culling when two-sided display is needed.',
            'fallback': 'Skip unloaded props and retain the original unmodified procedural environment.',
        },
        'modifications': [
            'Resize each embedded original 256x256 RGB diffuse PNG to 128x128 with Pillow LANCZOS; PNG compress_level=9, optimize=False.',
            'Retain original base-color factors, UVs, image associations, material names and double-sided flags. Replace metallic/roughness response with neutral constants.',
            'Remove source-only metadata; correct source custom-property front label to the already-exported +Z glTF front. Keep one identity mesh node.',
            'Repack aligned GLB buffers and retain per-stream source geometry hashes for exact validation.',
        ],
        'assets': assets,
        'runtime_total_bytes': sum(a['bytes'] for a in assets),
        'total_triangles': sum(a['triangle_count'] for a in assets),
        'total_raylib_meshes': sum(a['raylib_mesh_count'] for a in assets),
        'rebuild': {'script': 'build_props.py', 'dependencies': ['Python >=3.9', 'Pillow'],
                    'reference_pillow_version': pillow_version, 'source_mutation': False,
                    'command': 'python3 build_props.py --source /path/to/02_household_clutter',
                    'independent_check': 'python3 build_props.py --source /path/to/02_household_clutter --out /tmp/swat-props-rebuild; python3 validate_props.py --assets /tmp/swat-props-rebuild --source /path/to/02_household_clutter',
                    'determinism': 'Byte-identical rebuilds with the same Python/Pillow/PNG encoder versions; no timestamps or absolute paths in outputs.'},
    }
    require(catalog['runtime_total_bytes'] < 1024 * 1024, 'Runtime pack exceeds 1 MiB budget')
    (args.out / 'props_catalog.json').write_text(json.dumps(catalog, indent=2, ensure_ascii=True) + '\n')
    print('Built %d props: %d bytes, %d triangles, %d raylib meshes' % (
        len(assets), catalog['runtime_total_bytes'], catalog['total_triangles'], catalog['total_raylib_meshes']))


if __name__ == '__main__':
    main()
