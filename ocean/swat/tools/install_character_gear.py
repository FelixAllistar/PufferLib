#!/usr/bin/env python3
"""Install the private F geometry with the six unchanged gameplay motion banks.

Usage: install_character_gear.py EXTRACTED_F_FIXTURE
Source files stay untouched. Runtime reconstructs the two new anatomical elbow
carriers after sampling and IK; the source review motion is not a gameplay clip.
"""
from pathlib import Path
import copy
import hashlib
import json
import struct
import sys

GEOMETRY_SHA = '5dd68cf3010b4ba24ec719fb949e1d8f62053c5761142c37da41079848f00835'
MOTION_SHA = {
    'ready': '3a20375ec72f106925ef96718da0931e172f21908c6720cff4d792c173c0cc81',
    'walk': '82dd05c813a017b23ee205c2cb43a98edc56b062f4dd567741099d3e6a040d69',
    'walk_left': '4fe97ec1997bb4799440216ddc3a74d858154c0d8e51bf863e2d86082b1a1996',
    'walk_right': 'edcb227870a0483d06c99e89df491e24f6eb22c879e35833e597f3d6a196b899',
    'crouch_ready': '54957d0769b6ecaaa164969f0c0315316f6834714044a2b376364f2a32fd6d66',
    'crouch_walk': 'ea828e53f305de77b35ba55abdf67470ae98e871911c43fae5505c5022d47dd1',
}


def read_glb(path, expected):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError(f'{path.name}: uncalibrated source hash')
    magic, version, length = struct.unpack_from('<III', raw)
    if magic != 0x46546c67 or version != 2 or length != len(raw):
        raise ValueError('Invalid GLB header')
    size, kind = struct.unpack_from('<II', raw, 12)
    if kind != 0x4e4f534a:
        raise ValueError('Missing JSON chunk')
    doc = json.loads(raw[20:20+size])
    size2, kind2 = struct.unpack_from('<II', raw, 20+size)
    if kind2 != 0x004e4942 or 28+size+size2 != len(raw) or len(doc['buffers']) != 1:
        raise ValueError('Expected one embedded binary buffer')
    return doc, raw[28+size:28+size+size2]


def join_motion(geometry, geometry_bytes, motion, motion_bytes):
    result = copy.deepcopy(geometry)
    names = {node['name']: i for i, node in enumerate(result['nodes']) if 'name' in node}
    joint_names = {motion['nodes'][i]['name'] for i in motion['skins'][0]['joints']}
    if len(joint_names) != 70 or len(result['skins'][0]['joints']) != 72:
        raise ValueError('Expected original 70 joints and F revision 72 joints')
    # Identical native rest frames and hierarchy are required before reusing
    # local animation bytes. Name matching alone is not a retargeting proof.
    for i in motion['skins'][0]['joints']:
        node = motion['nodes'][i]
        target = result['nodes'][names[node['name']]]
        for field, default in [('translation', [0, 0, 0]), ('rotation', [0, 0, 0, 1]),
                               ('scale', [1, 1, 1]), ('matrix', None)]:
            if node.get(field, default) != target.get(field, default):
                raise ValueError(f'{node["name"]}: incompatible rest {field}')
        children = {motion['nodes'][c]['name'] for c in node.get('children', [])}
        target_children = {result['nodes'][c]['name'] for c in target.get('children', [])}
        if children != target_children & joint_names:
            raise ValueError(f'{node["name"]}: incompatible native hierarchy')
    offset = len(geometry_bytes)
    views = len(result['bufferViews'])
    accessors = len(result['accessors'])
    for view in motion['bufferViews']:
        view = copy.deepcopy(view)
        view['buffer'] = 0
        view['byteOffset'] = offset + view.get('byteOffset', 0)
        result['bufferViews'].append(view)
    for accessor in motion['accessors']:
        accessor = copy.deepcopy(accessor)
        if 'sparse' in accessor:
            raise ValueError('Sparse source accessors require an explicit adapter')
        if 'bufferView' in accessor:
            accessor['bufferView'] += views
        result['accessors'].append(accessor)
    result['animations'] = copy.deepcopy(motion['animations'])
    for animation in result['animations']:
        for sampler in animation['samplers']:
            sampler['input'] += accessors
            sampler['output'] += accessors
        for channel in animation['channels']:
            channel['target']['node'] = names[motion['nodes'][channel['target']['node']]['name']]
    binary = geometry_bytes + motion_bytes
    result['buffers'][0]['byteLength'] = len(binary)
    result.setdefault('extras', {})['swat_geometry_revision'] = 'upper-gear-f-v1'
    result['extras']['swat_runtime_elbow_carriers'] = True
    text = json.dumps(result, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary += b'\0' * (-len(binary) % 4)
    return (struct.pack('<III', 0x46546c67, 2, 28+len(text)+len(binary)) +
            struct.pack('<II', len(text), 0x4e4f534a) + text +
            struct.pack('<II', len(binary), 0x004e4942) + binary)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    fixture = Path(sys.argv[1]).resolve()
    geometry, binary = read_glb(fixture/'swat_upper_gear_remake_f_v1.glb', GEOMETRY_SHA)
    root = Path(__file__).resolve().parents[3]/'build/swat/assets/characters'
    outputs = {}
    for name, sha in MOTION_SHA.items():
        motion, motion_bytes = read_glb(root/(name+'.glb'), sha)
        outputs[name] = join_motion(geometry, binary, motion, motion_bytes)
    # Validate every bank before changing the ignored installation.
    destination = root/'upper_gear_f'
    destination.mkdir(exist_ok=True)
    for name, raw in outputs.items():
        temporary = destination/(name+'.glb.tmp')
        temporary.write_bytes(raw)
        temporary.replace(destination/(name+'.glb'))
        print('Installed F geometry / original motion:', name, hashlib.sha256(raw).hexdigest())
    (destination/'provenance.json').write_text(json.dumps({
        'geometry_source_sha256': GEOMETRY_SHA, 'original_motion_sha256': MOTION_SHA,
        'carrier_policy': 'runtime anatomical frames after source sampling and arm IK',
    }, indent=2)+'\n')


if __name__ == '__main__':
    main()
