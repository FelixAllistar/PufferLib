#!/usr/bin/env python3
"""Read-only GLB contract probe, not a complete glTF validator or runtime test.

No assets are copied, uploaded, rewritten, or bundled. Dense accessors and a
single embedded GLB 2.0 buffer are supported. Run on privately acquired assets.
"""
import argparse
import json
import math
import mmap
import struct
from pathlib import Path

FORMATS = {5121: ('B', 255), 5123: ('H', 65535), 5125: ('I', 4294967295), 5126: ('f', None)}
WIDTHS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def inspect(data, expected=None, max_influences=None):
    if len(data) < 20 or struct.unpack_from('<III', data) != (0x46546C67, 2, len(data)):
        raise ValueError('Invalid GLB header/version/length')
    chunks, offset = {}, 12
    while offset < len(data):
        size, kind = struct.unpack_from('<II', data, offset)
        offset += 8
        if size % 4 or offset + size > len(data) or kind in chunks:
            raise ValueError('Malformed, duplicate, or truncated chunk')
        chunks[kind] = (offset, size)
        offset += size
    start, size = chunks[0x4E4F534A]
    doc = json.loads(data[start:start + size])
    bin_start, bin_size = chunks[0x004E4942]
    buffers = doc.get('buffers', [])
    if len(buffers) != 1 or 'uri' in buffers[0] or buffers[0]['byteLength'] > bin_size:
        raise ValueError('Only one embedded buffer is supported')

    def accessor(index):
        a = doc['accessors'][index]
        if 'sparse' in a:
            raise ValueError('Sparse accessors require a full glTF validator')
        view = doc['bufferViews'][a['bufferView']]
        code, divisor = FORMATS[a['componentType']]
        unpack = struct.Struct('<' + code * WIDTHS[a['type']])
        stride, count = view.get('byteStride', unpack.size), a['count']
        relative, base = a.get('byteOffset', 0), view.get('byteOffset', 0)
        extent = relative + (count - 1) * stride + unpack.size if count else relative
        if view.get('buffer', 0) != 0 or count < 0 or stride < unpack.size or min(relative, base) < 0 or extent > view['byteLength'] or base + view['byteLength'] > buffers[0]['byteLength']:
            raise ValueError('Accessor bounds or layout unsupported')
        if a.get('normalized') and divisor is None:
            raise ValueError('FLOAT accessor cannot be normalized')
        values = [unpack.unpack_from(data, bin_start + base + relative + i * stride) for i in range(count)]
        return [tuple(v / divisor for v in row) for row in values] if a.get('normalized') else values

    errors, primitives, clips, mixed = [], [], [], []
    largest = 0
    for mi, mesh in enumerate(doc.get('meshes', [])):
        for pi, primitive in enumerate(mesh['primitives']):
            attrs = primitive['attributes']
            ids = sorted(int(k.split('_')[1]) for k in attrs if k.startswith('WEIGHTS_'))
            if not ids:
                continue
            if ids != list(range(len(ids))) or any(f'JOINTS_{i}' not in attrs for i in ids):
                raise ValueError('Incomplete skin attribute sets')
            if any(doc['accessors'][attrs[f'{kind}_{i}']]['type'] != 'VEC4' for i in ids for kind in ('JOINTS', 'WEIGHTS')):
                raise ValueError('Skin attribute sets must be VEC4')
            sets = [accessor(attrs[f'WEIGHTS_{i}']) for i in ids]
            joints = [accessor(attrs[f'JOINTS_{i}']) for i in ids]
            count = doc['accessors'][attrs['POSITION']]['count']
            if any(len(x) != count for x in sets + joints):
                raise ValueError('Skin/position accessor counts disagree')
            active, sum_error = 0, 0.0
            for vertex in range(count):
                weights = [w for rows in sets for w in rows[vertex]]
                if any(not math.isfinite(w) or w < 0 for w in weights):
                    raise ValueError('Invalid skin weight')
                active = max(active, sum(w > 0 for w in weights))
                sum_error = max(sum_error, abs(sum(weights) - 1))
            largest = max(largest, active)
            if sum_error > 1e-4:
                errors.append(f'Mesh {mi}/{pi}: weights not normalized across all sets')
            primitives.append({'mesh': mi, 'primitive': pi, 'weight_sets': len(ids), 'max_active': active, 'max_sum_error': sum_error})
    if max_influences is not None and largest > max_influences:
        errors.append(f'Asset requires {largest} influences; consumer limit is {max_influences}')
    names = set()
    for animation in doc.get('animations', []):
        name = animation.get('name', '')
        if not name or name in names:
            errors.append('Missing or duplicate animation name')
        names.add(name)
        duration, modes, prop_count = 0.0, {}, 0
        for channel in animation['channels']:
            sampler = animation['samplers'][channel['sampler']]
            times = [x[0] for x in accessor(sampler['input'])]
            if not times or any(not math.isfinite(t) or t < 0 for t in times) or any(b <= a for a, b in zip(times, times[1:])):
                raise ValueError('Invalid animation time keys')
            duration = max(duration, times[-1])
            node_index = channel['target']['node']
            node = doc['nodes'][node_index].get('name', '')
            mode = sampler.get('interpolation', 'LINEAR')
            modes.setdefault(node_index, set()).add(mode)
            if node.startswith('prop:Magazine') and channel['target']['path'] == 'scale':
                prop_count += 1
                if mode != 'STEP':
                    errors.append(f'{name}: {node} visibility must use STEP')
                output = doc['accessors'][sampler['output']]
                if output['type'] != 'VEC3' or output['componentType'] != 5126 or output.get('normalized') or output['count'] != len(times):
                    raise ValueError('Magazine visibility requires one FLOAT/VEC3 output per input key')
                for row in accessor(sampler['output']):
                    if not (all(abs(v) < 1e-5 for v in row) or all(abs(v - 1) < 1e-5 for v in row)):
                        errors.append(f'{name}: {node} visibility must be binary uniform scale')
                        break
        clips.append({'name': name, 'duration_s': duration, 'prop_visibility_channels': prop_count})
        mixed.extend({'clip': name, 'node_index': i, 'node': doc['nodes'][i].get('name', ''), 'modes': sorted(m)} for i, m in modes.items() if len(m) > 1)
    if expected is not None and len(names) != expected:
        errors.append(f'Expected {expected} animations; found {len(names)}')
    return {'max_active_influences': largest, 'skin_joint_counts': [len(s['joints']) for s in doc.get('skins', [])], 'skinned_primitives': primitives, 'animations': clips, 'mixed_interpolation_nodes': mixed, 'errors': errors, 'scope': 'Structure only; no render, deformation, ownership, root-motion, material, or gameplay certification'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('glb', type=Path)
    parser.add_argument('--expected-animations', type=int)
    parser.add_argument('--max-influences', type=int)
    args = parser.parse_args()
    try:
        with args.glb.open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
            result = inspect(data, args.expected_animations, args.max_influences)
    except (ValueError, KeyError, IndexError, TypeError, struct.error, OSError) as exc:
        print(json.dumps({'errors': [str(exc)]}, indent=2))
        return 2
    print(json.dumps(result, indent=2))
    return 1 if result['errors'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
