"""Entirely synthetic test bytes; no third-party geometry or animation."""
import importlib.util
import json
import struct
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location('probe', Path(__file__).parents[1] / 'scripts/check_animation_glb.py')
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


def fixture(mode='STEP', scale=(0., 0., 0., 1., 1., 1.), times_values=(0, 0.45), split_nodes=False, scale_float=True):
    doc = {'asset': {'version': '2.0'}, 'bufferViews': [], 'accessors': [], 'nodes': [{'name': 'prop:MagazineDropped'}] + [{'name': f'joint{i}'} for i in range(1, 7)] + [{'mesh': 0, 'skin': 0}], 'skins': [{'joints': list(range(7))}], 'scenes': [{'nodes': list(range(8))}], 'scene': 0}
    binary = bytearray()

    def add(values, code, component_type, width, kind):
        while len(binary) % 4:
            binary.append(0)
        encoded = struct.pack('<' + code * len(values), *values)
        vi = len(doc['bufferViews'])
        doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(encoded)})
        binary.extend(encoded)
        ai = len(doc['accessors'])
        doc['accessors'].append({'bufferView': vi, 'componentType': component_type, 'count': len(values) // width, 'type': kind})
        return ai

    attrs = {'POSITION': add([0, 0, 0], 'f', 5126, 3, 'VEC3')}
    attrs['JOINTS_0'] = add([0, 1, 2, 3], 'B', 5121, 4, 'VEC4')
    attrs['JOINTS_1'] = add([4, 5, 6, 0], 'B', 5121, 4, 'VEC4')
    attrs['WEIGHTS_0'] = add([1/7]*4, 'f', 5126, 4, 'VEC4')
    attrs['WEIGHTS_1'] = add([1/7]*3+[0], 'f', 5126, 4, 'VEC4')
    times = add(times_values, 'f', 5126, 1, 'SCALAR')
    scales = add(scale, 'f', 5126, 3, 'VEC3') if scale_float else add([int(v) for v in scale], 'H', 5123, 3, 'VEC3')
    positions = add([0, 0, 0, 0, 1, 0], 'f', 5126, 3, 'VEC3')
    doc['meshes'] = [{'primitives': [{'attributes': attrs, 'mode': 0}]}]
    doc['animations'] = [{'name': 'Synthetic test', 'samplers': [{'input': times, 'output': scales, 'interpolation': mode}, {'input': times, 'output': positions, 'interpolation': 'LINEAR'}], 'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'scale'}}, {'sampler': 1, 'target': {'node': 0, 'path': 'translation'}}]}]
    if split_nodes:
        doc['nodes'][6]['name'] = doc['nodes'][0]['name']
        doc['animations'][0]['channels'][1]['target']['node'] = 6
    doc['buffers'] = [{'byteLength': len(binary)}]
    encoded = json.dumps(doc).encode()
    encoded += b' ' * (-len(encoded) % 4)
    binary += b'\0' * (-len(binary) % 4)
    return struct.pack('<III', 0x46546C67, 2, 28 + len(encoded) + len(binary)) + struct.pack('<II', len(encoded), 0x4E4F534A) + encoded + struct.pack('<II', len(binary), 0x004E4942) + binary


class ContractTests(unittest.TestCase):
    def test_seven_influences_and_mixed_modes(self):
        result = PROBE.inspect(fixture(), expected=1)
        self.assertEqual(result['errors'], [])
        self.assertEqual(result['max_active_influences'], 7)
        self.assertEqual(len(result['mixed_interpolation_nodes']), 1)

    def test_four_influence_consumer_rejected(self):
        self.assertTrue(any('consumer limit is 4' in e for e in PROBE.inspect(fixture(), max_influences=4)['errors']))

    def test_linear_visibility_rejected(self):
        self.assertTrue(any('must use STEP' in e for e in PROBE.inspect(fixture('LINEAR'))['errors']))

    def test_fractional_visibility_rejected(self):
        self.assertTrue(any('binary uniform' in e for e in PROBE.inspect(fixture(scale=(0, 0, 0, .5, .5, .5)))['errors']))

    def test_wrong_animation_count_rejected(self):
        self.assertTrue(PROBE.inspect(fixture(), expected=2)['errors'])

    def test_truncation_rejected(self):
        with self.assertRaises(ValueError):
            PROBE.inspect(fixture()[:-1])

    def test_duplicate_time_rejected(self):
        with self.assertRaises(ValueError):
            PROBE.inspect(fixture(times_values=(0, 0)))

    def test_empty_visibility_rejected(self):
        with self.assertRaises(ValueError):
            PROBE.inspect(fixture(scale=()))

    def test_mismatched_visibility_count_rejected(self):
        with self.assertRaises(ValueError):
            PROBE.inspect(fixture(scale=(0, 0, 0)))

    def test_same_named_nodes_not_combined(self):
        result = PROBE.inspect(fixture(split_nodes=True))
        self.assertEqual(result['errors'], [])
        self.assertEqual(result['mixed_interpolation_nodes'], [])

    def test_integer_visibility_rejected(self):
        with self.assertRaises(ValueError):
            PROBE.inspect(fixture(scale_float=False))


if __name__ == '__main__':
    unittest.main()
