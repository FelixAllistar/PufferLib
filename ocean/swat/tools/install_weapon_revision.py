#!/usr/bin/env python3
"""Install the private rifle R2 fixture, keeping the original for comparison.

Usage: install_weapon_revision.py EXTRACTED_R2_FIXTURE [--material-only]
No licensed mesh or image is written outside the ignored local asset install.
"""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import sys

ORIGINAL = 'daac57ca6bf40c9157808f88a1d1184c11f19ea4bada63cc54ca260b24392b48'
CANDIDATES = {
    'rifle7_visual_cleanup_r2.glb': '09d659865fb4d75aaed64b91f973e98ae178d12ffd9df42b8fc401cac25fce98',
    'rifle7_material_only_r2.glb': 'de255f8596a7bdd4b633abb7969865989c998c281ce89899bbd3820a18a62c0a',
}


def read(path, expected):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError(f'{path.name}: source hash mismatch')
    magic, version, length = struct.unpack_from('<III', raw)
    size, kind = struct.unpack_from('<II', raw, 12)
    if magic != 0x46546c67 or version != 2 or length != len(raw) or kind != 0x4e4f534a:
        raise ValueError('Invalid GLB container')
    doc = json.loads(raw[20:20+size])
    size2, kind2 = struct.unpack_from('<II', raw, 20+size)
    if kind2 != 0x004e4942 or 28+size+size2 != len(raw):
        raise ValueError('Expected one embedded binary chunk')
    return doc, raw[28+size:]


def accessor(doc, binary, index):
    a = doc['accessors'][index]
    widths = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
    sizes = {5121: 1, 5123: 2, 5125: 4, 5126: 4}
    width = widths[a['type']] * sizes[a['componentType']]
    v = doc['bufferViews'][a['bufferView']]
    if v.get('buffer', 0) != 0 or 'sparse' in a:
        raise ValueError('Unsupported fixture accessor')
    offset = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = v.get('byteStride', width)
    data = b''.join(binary[offset+i*stride:offset+i*stride+width] for i in range(a['count']))
    if len(data) != a['count']*width:
        raise ValueError('Truncated accessor')
    return a, data


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != '--material-only'):
        raise SystemExit(__doc__)
    fixture = Path(sys.argv[1]).resolve()
    name = 'rifle7_material_only_r2.glb' if len(sys.argv) == 3 else 'rifle7_visual_cleanup_r2.glb'
    root = Path(__file__).resolve().parents[3]/'build/swat/assets/weapons'
    installed = root/'rifle7_rigid_textured.glb'
    frozen = root/'rifle7_original_textured.glb'
    original_path = frozen if frozen.exists() else installed
    old, old_bytes = read(original_path, ORIGINAL)
    new, new_bytes = read(fixture/name, CANDIDATES[name])
    if new.get('skins') or new.get('animations') or len(new['meshes']) != 2 or new['nodes'] != old['nodes']:
        raise ValueError('Rigid body/magazine frame changed')
    for mesh, (before, after) in enumerate(zip(old['meshes'], new['meshes'])):
        if len(before['primitives']) != 1 or len(after['primitives']) != 1:
            raise ValueError('Expected body and magazine at their original mesh indices')
        p, q = before['primitives'][0], after['primitives'][0]
        for attribute in ('POSITION', 'NORMAL', 'TEXCOORD_0', 'TANGENT'):
            a, data = accessor(old, old_bytes, p['attributes'][attribute])
            b, revised = accessor(new, new_bytes, q['attributes'][attribute])
            if a['type'] != b['type'] or a['componentType'] != b['componentType'] or not revised.startswith(data):
                raise ValueError(f'{mesh}/{attribute}: protected original vertices changed')
            if attribute == 'POSITION' and (a.get('min') != b.get('min') or a.get('max') != b.get('max')):
                raise ValueError('Measured bounds changed')
            if mesh == 1 and data != revised:
                raise ValueError('Magazine geometry changed')
        if mesh == 1 or name == 'rifle7_material_only_r2.glb':
            if accessor(old, old_bytes, p['indices'])[1] != accessor(new, new_bytes, q['indices'])[1]:
                raise ValueError('Protected triangle indices changed')
    # All checks precede installation. Preserve the frozen baseline forever.
    if not frozen.exists():
        shutil.copy2(installed, frozen)
    temporary = root/'rifle7_rigid_textured.glb.tmp'
    shutil.copyfile(fixture/name, temporary)
    temporary.replace(installed)
    (root/'rifle7_revision.json').write_text(json.dumps({
        'original_sha256': ORIGINAL, 'installed_sha256': CANDIDATES[name],
        'revision': name, 'normal_scale': .65,
    }, indent=2)+'\n')
    print('Installed private R2:', name, CANDIDATES[name])


if __name__ == '__main__':
    main()
