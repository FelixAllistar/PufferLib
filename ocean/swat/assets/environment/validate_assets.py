#!/usr/bin/env python3
"""Validate the shipped runtime slice without Blender, Pillow, network or GPU."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from build_assets import accessor_values, bounds, read_glb


def check(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--source', type=Path, help='Also verify original source hashes and triangle count')
    args = parser.parse_args()
    catalog = json.loads((args.assets / 'catalog.json').read_text())
    check(catalog['schema'] == 'swat_environment_runtime_assets_v1', 'Unexpected catalog schema')
    expected_paths = {'plaster_diffuse.png', 'wood_diffuse.png', 'door_leaf.glb'}
    check({a['path'] for a in catalog['assets']} == expected_paths, 'Runtime path contract changed')
    total = 0
    for asset in catalog['assets']:
        data = (args.assets / asset['path']).read_bytes()
        check(len(data) == asset['bytes'], 'Byte size mismatch: ' + asset['path'])
        check(hashlib.sha256(data).hexdigest() == asset['sha256'], 'SHA256 mismatch: ' + asset['path'])
        check(asset['license'] == 'CC0-1.0', 'License metadata missing')
        total += len(data)
        if asset['kind'] == 'diffuse_texture':
            check(data[:8] == b'\x89PNG\r\n\x1a\n', 'Not a PNG')
            width, height, depth, color_type = struct.unpack_from('>IIBB', data, 16)
            check((width, height, depth, color_type) == (256, 256, 8, 2), 'Expected 256px 8-bit RGB PNG')
    check(total == catalog['runtime_total_bytes'] and total < 1024 * 1024, 'Runtime size budget exceeded')
    door = next(a for a in catalog['assets'] if a['id'] == 'door_leaf')
    doc, binary = read_glb(args.assets / door['path'])
    check(doc['asset']['version'] == '2.0', 'Not glTF 2.0')
    check(len(doc['nodes']) == len(doc['meshes']) == len(doc['scenes']) == 1, 'Expected a single leaf node')
    check(doc['scenes'][0]['nodes'] == [0] and doc['nodes'][0]['mesh'] == 0, 'Unreachable leaf mesh')
    check(not any(k in doc for k in ['animations', 'skins', 'cameras', 'extensionsRequired']), 'Unexpected glTF features')
    node = doc['nodes'][0]
    check(not any(k in node for k in ('matrix', 'translation', 'rotation', 'scale', 'children')), 'Unbaked transform or hidden child')
    check(node['extras']['owner'] == 'active_swat_door_object', 'Unbound node owner')
    check(node['extras']['logical_slab_bounds'] == door['logical_slab_bounds'], 'Slab metadata mismatch')
    check(node['extras']['axes'] == {'thickness': 'X', 'height': 'Y', 'width': 'Z'}, 'Wrong local frame')
    check(len(doc['buffers']) == 1 and 'uri' not in doc['buffers'][0], 'External buffer')
    check(doc['buffers'][0]['byteLength'] <= len(binary), 'Truncated buffer')
    for view in doc['bufferViews']:
        check(view['buffer'] == 0, 'Unexpected buffer index')
        check(view.get('byteOffset', 0) % 4 == 0, 'Unaligned buffer view')
        check(view.get('byteOffset', 0) + view['byteLength'] <= len(binary), 'Out-of-range buffer view')
    check(len(doc['images']) == len(doc['textures']) == 1, 'Expected one embedded diffuse image')
    image = doc['images'][0]
    check(image['mimeType'] == 'image/png' and 'uri' not in image, 'External or unexpected image')
    view = doc['bufferViews'][image['bufferView']]
    image_bytes = binary[view['byteOffset']:view['byteOffset'] + view['byteLength']]
    check(hashlib.sha256(image_bytes).hexdigest() == door['embedded_image_sha256'], 'Embedded image mismatch')
    check(struct.unpack_from('>IIBB', image_bytes, 16) == (256, 256, 8, 2), 'Embedded image not 256px RGB')
    for material in doc['materials']:
        check(not any(k in material for k in ('normalTexture', 'occlusionTexture', 'emissiveTexture')), 'Unneeded image map')
        check('metallicRoughnessTexture' not in material['pbrMetallicRoughness'], 'Unexpected PBR map')
        check(material['pbrMetallicRoughness']['metallicFactor'] == 0, 'Unexpected metallic response')
    primitives = doc['meshes'][0]['primitives']
    check(len(primitives) == 5, 'Unexpected leaf primitive count')
    check([p['extras']['role'] for p in primitives] == door['primitive_roles'], 'Primitive roles mismatch')
    all_positions, vertices, triangles = [], 0, 0
    for primitive in primitives:
        check(primitive['mode'] == 4, 'Non-triangle topology')
        check(primitive['extras']['owner'] == 'active_swat_door_object', 'Unbound child geometry')
        attrs = primitive['attributes']
        check(set(attrs) == {'POSITION', 'NORMAL', 'TEXCOORD_0'}, 'Unexpected vertex attribute')
        positions = accessor_values(doc, binary, attrs['POSITION'])
        normals = accessor_values(doc, binary, attrs['NORMAL'])
        uvs = accessor_values(doc, binary, attrs['TEXCOORD_0'])
        indices = [i[0] for i in accessor_values(doc, binary, primitive['indices'])]
        check(len(positions) == len(normals) == len(uvs), 'Inconsistent attribute lengths')
        check(all(math.isfinite(v) for row in positions + normals + uvs for v in row), 'Non-finite vertex data')
        check(all(abs(sum(v*v for v in n) - 1) < 1e-5 for n in normals), 'Non-unit normal')
        check(len(indices) % 3 == 0 and all(0 <= i < len(positions) for i in indices), 'Invalid index range')
        for i in range(0, len(indices), 3):
            check(len(set(indices[i:i+3])) == 3, 'Degenerate triangle indices')
        low, high = bounds(positions)
        accessor = doc['accessors'][attrs['POSITION']]
        check(low == accessor['min'] and high == accessor['max'], 'Incorrect accessor bounds')
        if primitive['extras']['role'] == 'slab':
            check(low == [-0.5] * 3 and high == [0.5] * 3, 'Slab is not exactly a centered unit cube')
        all_positions.extend(positions)
        vertices += len(positions)
        triangles += len(indices) // 3
    low, high = bounds(all_positions)
    check(door['visual_bounds'] == {'min': low, 'max': high}, 'Visual bounds mismatch')
    check(vertices == door['vertex_count'] == 2520, 'Source vertices dropped')
    check(triangles == door['triangle_count'] == 1236, 'Source triangles dropped')
    if args.source:
        for source in catalog['source_files']:
            data = (args.source / source['path']).read_bytes()
            check(hashlib.sha256(data).hexdigest() == source['sha256'], 'Source modified: ' + source['path'])
        source_doc, _ = read_glb(args.source / door['source'])
        source_triangles = sum(source_doc['accessors'][p['indices']]['count'] // 3
                               for mesh in source_doc['meshes'] for p in mesh['primitives'])
        check(source_triangles == triangles, 'Geometry not preserved')
    print('PASS: 3 runtime assets, %s bytes; identity leaf; exact unit slab; %s vertices; %s triangles; 1 embedded 256px image; finite bounds/normals; hashes/provenance.' % (total, vertices, triangles))


if __name__ == '__main__':
    main()
