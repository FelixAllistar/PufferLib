#!/usr/bin/env python3
"""Validate compact SWAT props without Blender, Pillow, network or GPU.

python3 validate_props.py [--assets DIR] [--source /path/to/02_household_clutter]
                         [--header ../../environment_props.h]
Source mode checks every pinned source file, exact geometry and material/image
associations. Header mode also proves C placement bounds enclose the real mesh.
This is structural/import-contract QA; an actual raylib graphics test is separate.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct
import zlib
from build_props import (ASSET_IDS, IDENTITY, SOURCE_HASHES, TEXTURE_SIZE,
                         accessor_bytes, accessor_values, bounds,
                         geometry_records, read_glb, require, sha256)

EXPECTED_TRIANGLES = [2149, 3600, 2456, 1674, 1400, 1428]
EXPECTED_PRIMITIVES = [4, 3, 3, 7, 3, 5]


def validate_png(data):
    require(data[:8] == b'\x89PNG\r\n\x1a\n', 'Expected PNG signature')
    offset, chunks, compressed = 8, [], bytearray()
    while offset < len(data):
        require(offset + 12 <= len(data), 'Truncated PNG chunk')
        size, = struct.unpack_from('>I', data, offset)
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        require(offset + 12 + size <= len(data), 'PNG chunk exceeds image')
        crc, = struct.unpack_from('>I', data, offset + 8 + size)
        require(crc == zlib.crc32(kind + payload) & 0xffffffff, 'PNG checksum mismatch')
        if kind == b'IHDR':
            require(not chunks and size == 13, 'Invalid PNG header')
            require(struct.unpack('>IIBBBBB', payload) == (TEXTURE_SIZE, TEXTURE_SIZE, 8, 2, 0, 0, 0),
                    'Expected noninterlaced 128px RGB PNG')
        elif kind == b'IDAT':
            compressed.extend(payload)
        elif kind == b'IEND':
            require(size == 0 and offset + 12 == len(data), 'Invalid PNG end')
        else:
            require(False, 'Unexpected PNG chunk')
        chunks.append(kind)
        offset += 12 + size
    require(chunks[0] == b'IHDR' and chunks[-1] == b'IEND' and b'IDAT' in chunks, 'Incomplete PNG')
    decoded = zlib.decompress(compressed)
    row_size = 1 + TEXTURE_SIZE * 3
    require(len(decoded) == TEXTURE_SIZE * row_size, 'Invalid PNG decoded size')
    require(all(decoded[i] <= 4 for i in range(0, len(decoded), row_size)), 'Unknown PNG filter')


def forbid_external_features(value):
    if isinstance(value, dict):
        require(not any(k in value for k in ('uri', 'extensions', 'extensionsUsed', 'extensionsRequired')),
                'External URI or glTF extension')
        for child in value.values():
            forbid_external_features(child)
    elif isinstance(value, list):
        for child in value:
            forbid_external_features(child)


def validate_model(directory, asset, expected_triangles, expected_primitives, source=None):
    path = directory / asset['path']
    data = path.read_bytes()
    require(len(data) == asset['bytes'] and sha256(data) == asset['sha256'], 'Output checksum: ' + path.name)
    require(asset['license'] == 'CC0-1.0' and asset['render_only'] and asset['collision'] == 'none', 'Prop is not decorative CC0')
    require(asset['source'] == 'modules/' + asset['id'] + '.glb', 'Unexpected source path')
    require(asset['source_sha256'] == SOURCE_HASHES[asset['source']], 'Wrong pinned source')
    require(asset['source_to_runtime_matrix_column_major'] == IDENTITY, 'Non-identity baked transform')
    require(asset['external_uris'] == [], 'External resource metadata')
    doc, binary = read_glb(path)
    forbid_external_features(doc)
    require(doc['asset']['version'] == '2.0', 'Not glTF 2.0')
    require(not any(k in doc for k in ('animations', 'skins', 'cameras')), 'Unexpected scene content')
    require(len(doc['scenes']) == len(doc['nodes']) == len(doc['meshes']) == 1, 'Expected one scene, mesh and node')
    require(doc['scene'] == 0 and doc['scenes'][0]['nodes'] == [0], 'Unexpected active scene')
    node = doc['nodes'][0]
    require(node['mesh'] == 0 and not any(k in node for k in ('matrix', 'translation', 'rotation', 'scale', 'children', 'weights', 'skin')),
            'Node must be identity and static')
    require(node['extras']['asset_id'] == asset['id'] and node['extras']['up'] == '+Y' and
            node['extras']['front'] == '+Z' and node['extras']['units'] == 'metres' and
            node['extras']['render_only'] and node['extras']['collision'] == 'none', 'Incorrect node contract')
    require(asset['mesh_count'] == asset['node_count'] == 1, 'Mesh/node metadata mismatch')
    require(len(doc['buffers']) == 1, 'Expected one embedded buffer')
    used_length = doc['buffers'][0]['byteLength']
    require(0 <= len(binary) - used_length <= 3 and not any(binary[used_length:]), 'Invalid BIN padding')
    for view in doc['bufferViews']:
        offset = view.get('byteOffset', 0)
        require(view['buffer'] == 0 and offset >= 0 and offset % 4 == 0 and view['byteLength'] > 0,
                'Unaligned/invalid buffer view')
        require(offset + view['byteLength'] <= used_length, 'Buffer view exceeds declared buffer')
    require(len(doc['images']) == len(doc['textures']) == asset['embedded_image_count'] == len(asset['images']), 'Image count mismatch')
    for image, record in zip(doc['images'], asset['images']):
        require(image['mimeType'] == 'image/png', 'Unsupported image type')
        view = doc['bufferViews'][image['bufferView']]
        png = binary[view['byteOffset']:view['byteOffset'] + view['byteLength']]
        require(sha256(png) == record['sha256'] and len(png) == record['bytes'], 'Image hash/size mismatch')
        require(record['size_pixels'] == [TEXTURE_SIZE, TEXTURE_SIZE] and record['channels'] == 'RGB', 'Image record mismatch')
        validate_png(png)
    require(doc['samplers'] == [{'magFilter': 9729, 'minFilter': 9729, 'wrapS': 10497, 'wrapT': 10497}], 'Unexpected texture sampler')
    for index, texture in enumerate(doc['textures']):
        require(texture == {'source': index, 'sampler': 0}, 'Image association changed')
    materials = doc['materials']
    require(len(materials) == asset['material_count'] == expected_primitives, 'Material count mismatch')
    used_textures = set()
    for material in materials:
        require(set(material) == {'name', 'doubleSided', 'pbrMetallicRoughness'}, 'Unexpected material feature')
        require(material['doubleSided'] is True, 'Source two-sided flag lost')
        pbr = material['pbrMetallicRoughness']
        require(set(pbr) <= {'baseColorFactor', 'baseColorTexture', 'metallicFactor', 'roughnessFactor'}, 'Unneeded PBR map')
        require(pbr['metallicFactor'] == 0 and pbr['roughnessFactor'] == 1, 'Non-neutral material response')
        factor = pbr.get('baseColorFactor', [1, 1, 1, 1])
        require(len(factor) == 4 and all(math.isfinite(f) and 0 <= f <= 1 for f in factor) and factor[3] == 1,
                'Invalid opaque base-color factor')
        if 'baseColorTexture' in pbr:
            tex = pbr['baseColorTexture']
            require(set(tex) == {'index'} and 0 <= tex['index'] < len(doc['textures']), 'Invalid diffuse texture binding')
            used_textures.add(tex['index'])
    require(used_textures == set(range(len(doc['textures']))), 'Unused embedded texture')
    primitives = doc['meshes'][0]['primitives']
    require(len(primitives) == asset['primitive_count'] == asset['raylib_mesh_count'] == expected_primitives, 'Primitive count mismatch')
    all_positions = []
    for material_index, primitive in enumerate(primitives):
        require(set(primitive) == {'attributes', 'indices', 'material', 'mode'}, 'Unexpected primitive feature')
        require(primitive['mode'] == 4 and primitive['material'] == material_index, 'Topology/material order changed')
        attributes = primitive['attributes']
        require(set(attributes) == {'POSITION', 'NORMAL', 'TEXCOORD_0'}, 'Unexpected attributes')
        for name, kind in [('POSITION', 'VEC3'), ('NORMAL', 'VEC3'), ('TEXCOORD_0', 'VEC2')]:
            accessor = doc['accessors'][attributes[name]]
            require(accessor['componentType'] == 5126 and accessor['type'] == kind, 'Unsupported raylib attribute type')
            require(doc['bufferViews'][accessor['bufferView']]['target'] == 34962, 'Wrong attribute buffer target')
        indices_accessor = doc['accessors'][primitive['indices']]
        require(indices_accessor['componentType'] == 5123 and indices_accessor['type'] == 'SCALAR', 'Expected uint16 indices')
        require(doc['bufferViews'][indices_accessor['bufferView']]['target'] == 34963, 'Wrong index buffer target')
        positions = accessor_values(doc, binary, attributes['POSITION'])
        normals = accessor_values(doc, binary, attributes['NORMAL'])
        uvs = accessor_values(doc, binary, attributes['TEXCOORD_0'])
        indices = [row[0] for row in accessor_values(doc, binary, primitive['indices'])]
        require(0 < len(positions) == len(normals) == len(uvs) < 65536, 'Invalid or oversized raylib vertex stream')
        require(all(math.isfinite(v) for row in positions + normals + uvs for v in row), 'Non-finite geometry')
        require(all(abs(sum(v * v for v in n) - 1) < 1e-5 for n in normals), 'Non-unit normal')
        require(len(indices) % 3 == 0 and all(0 <= i < len(positions) for i in indices), 'Invalid triangle indices')
        for i in range(0, len(indices), 3):
            a, b, c = (positions[j] for j in indices[i:i + 3])
            u, v = [b[j] - a[j] for j in range(3)], [c[j] - a[j] for j in range(3)]
            cross = [u[1]*v[2] - u[2]*v[1], u[2]*v[0] - u[0]*v[2], u[0]*v[1] - u[1]*v[0]]
            require(sum(x*x for x in cross) > 0, 'Zero-area triangle')
        actual_bounds = bounds(positions)
        accessor = doc['accessors'][attributes['POSITION']]
        require(actual_bounds == {'min': accessor['min'], 'max': accessor['max']}, 'Incorrect position accessor bounds')
        all_positions.extend(positions)
    actual_bounds = bounds(all_positions)
    source_record = asset['source_manifest_record']
    require(actual_bounds == asset['aabb_metres_xyz'] == source_record['aabb_gltf_y_up'], 'Actual/source/catalog AABB mismatch')
    require(actual_bounds['min'][1] == 0 and 0 < actual_bounds['max'][1] < .1, 'Wrong tabletop origin or oversize prop')
    require(all(abs(actual_bounds['max'][i] + actual_bounds['min'][i]) < 2e-8 for i in (0, 2)), 'XZ pivot is not centred')
    require(asset['dimensions_metres_xyz'] == [actual_bounds['max'][i] - actual_bounds['min'][i] for i in range(3)], 'Dimension mismatch')
    geometry = geometry_records(doc, binary)
    require(geometry == asset['geometry'], 'Geometry-stream checksum mismatch')
    require(sum(g['triangles'] for g in geometry) == asset['triangle_count'] == expected_triangles == source_record['triangles_exported'], 'Triangles dropped')
    require(sum(g['vertices'] for g in geometry) == asset['vertex_count'], 'Vertex count mismatch')
    require(source_record['sha256'] == asset['source_sha256'], 'Source manifest hash mismatch')
    if source:
        original, original_binary = read_glb(source / asset['source'])
        require(geometry == geometry_records(original, original_binary), 'Source geometry changed')
        for output_material, original_material in zip(materials, original['materials']):
            require(output_material['name'] == original_material['name'] and
                    output_material['doubleSided'] == original_material['doubleSided'], 'Material identity changed')
            for key in ('baseColorTexture', 'baseColorFactor'):
                require(output_material['pbrMetallicRoughness'].get(key) == original_material['pbrMetallicRoughness'].get(key), 'Base-color binding changed')
        for image, record in zip(original['images'], asset['images']):
            view = original['bufferViews'][image['bufferView']]
            png = original_binary[view['byteOffset']:view['byteOffset'] + view['byteLength']]
            require(sha256(png) == record['source_sha256'] and len(png) == record['source_bytes'], 'Source image checksum mismatch')
    return len(data)


def validate_header(header, assets):
    """Parse only the literal filename/dimensions table, independent of enum order."""
    text = header.read_text()
    pattern = r'\{\s*"(prop_[a-z_]+\.glb)"\s*,\s*\{([^}]+)\}\s*\}'
    entries = re.findall(pattern, text)
    require(len(entries) == len(assets) and len({name for name, _ in entries}) == len(assets), 'C prop table missing/duplicate entry')
    specs = {name: [float(v.strip().rstrip('fF')) for v in values.split(',')] for name, values in entries}
    require(set(specs) == {a['path'] for a in assets}, 'C prop paths differ from shipped pack')
    for asset in assets:
        size = specs[asset['path']]
        require(len(size) == 3 and all(math.isfinite(v) and v > 0 for v in size), 'Invalid C placement size')
        aabb = asset['aabb_metres_xyz']
        require(-size[0]/2 <= aabb['min'][0] <= aabb['max'][0] <= size[0]/2 and
                0 == aabb['min'][1] <= aabb['max'][1] <= size[1] and
                -size[2]/2 <= aabb['min'][2] <= aabb['max'][2] <= size[2]/2,
                'C conservative placement bounds miss geometry: ' + asset['path'])


def validate(directory, source=None, header=None):
    catalog = json.loads((directory / 'props_catalog.json').read_text())
    require(catalog['schema'] == 'swat_compact_props_v1' and catalog['license'] == 'CC0-1.0', 'Unexpected catalog')
    require(catalog['source_files'] == [{'path': p, 'sha256': h} for p, h in SOURCE_HASHES.items()], 'Pinned source list changed')
    license_copy = catalog['source_license_copy']
    require(license_copy == {'path': 'LICENSE_SOURCE_CLUTTER.txt', 'source': 'LICENSE.txt',
                            'sha256': SOURCE_HASHES['LICENSE.txt']}, 'License record mismatch')
    require(sha256((directory / license_copy['path']).read_bytes()) == SOURCE_HASHES['LICENSE.txt'], 'Supplied license changed')
    assets = catalog['assets']
    require([a['id'] for a in assets] == list(ASSET_IDS), 'Wrong six-prop selection/order')
    require([a['path'] for a in assets] == ['prop_' + i + '.glb' for i in ASSET_IDS], 'Runtime paths changed')
    if source:
        for path, expected in SOURCE_HASHES.items():
            require(sha256((source / path).read_bytes()) == expected, 'Source checksum mismatch: ' + path)
        manifest = json.loads((source / 'manifest.json').read_text())
        records = {a['asset_id']: a for a in manifest['assets']}
        require(all(a['source_manifest_record'] == records[a['id']] for a in assets), 'Source manifest record changed')
    total = sum(validate_model(directory, asset, triangles, primitives, source)
                for asset, triangles, primitives in zip(assets, EXPECTED_TRIANGLES, EXPECTED_PRIMITIVES))
    require(total == catalog['runtime_total_bytes'] < 1024 * 1024, 'Runtime byte budget/count mismatch')
    require(catalog['total_triangles'] == sum(EXPECTED_TRIANGLES) == 12707, 'Total triangle count mismatch')
    require(catalog['total_raylib_meshes'] == sum(EXPECTED_PRIMITIVES) == 25, 'Total raylib mesh count mismatch')
    if header:
        validate_header(header, assets)
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--header', type=Path, help='Also verify conservative placement bounds in environment_props.h')
    args = parser.parse_args()
    total = validate(args.assets, args.source, args.header)
    print('PASS: 6 render-only props; %d bytes; 12707 unchanged triangles; 25 raylib meshes; '
          '19 embedded 128px RGB images; +Y-up metre surface pivots; hashes, geometry, PNGs, '
          'materials and CC0 provenance%s%s.' % (total, '; source streams identical' if args.source else '',
                                               '; C placement bounds conservative' if args.header else ''))


if __name__ == '__main__':
    main()
