"""Independent structural, metadata, attachment, and optical replay from GLB bytes.

Uses only NumPy and the previously independent replay_unit.py, never fixture writer code.
Run: python inspect_binary.py --package ../package --output structure_validation.json
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import numpy as np
from replay_unit import GLB, trs


def matrix(values):
    return np.asarray(values, dtype=float).reshape(4, 4).T


def structure(g):
    raw = g.path.read_bytes()
    off = 12
    chunks = []
    while off < len(raw):
        length, kind = struct.unpack_from('<II', raw, off)
        assert length % 4 == 0 and off + 8 + length <= len(raw)
        chunks.append({'kind': kind, 'offset': off, 'bytes': length})
        off += 8 + length
    assert off == len(raw) and len(chunks) == 2
    assert [c['kind'] for c in chunks] == [0x4e4f534a, 0x004e4942]
    doc = g.doc
    assert len(doc['buffers']) == 1 and 'uri' not in doc['buffers'][0]
    assert doc['buffers'][0]['byteLength'] <= len(g.bin)
    assert len(g.bin) - doc['buffers'][0]['byteLength'] <= 3
    views = []
    for i, view in enumerate(doc['bufferViews']):
        start = view.get('byteOffset', 0)
        assert view.get('buffer', 0) == 0
        assert start >= 0 and start + view['byteLength'] <= doc['buffers'][0]['byteLength']
        views.append({'buffer_view': i, 'offset': start, 'bytes': view['byteLength']})
    ar = []
    widths = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
    sizes = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
    for i, a in enumerate(doc['accessors']):
        assert 'sparse' not in a
        v = doc['bufferViews'][a['bufferView']]
        size = sizes[a['componentType']]
        width = widths[a['type']] * size
        stride = v.get('byteStride', width)
        offset = a.get('byteOffset', 0)
        assert stride >= width and stride % size == 0
        assert offset >= 0 and (v.get('byteOffset', 0) + offset) % size == 0
        assert offset + max(a['count'] - 1, 0) * stride + width <= v['byteLength']
        val = g.acc(i)
        assert np.all(np.isfinite(val))
        if 'min' in a: assert np.allclose(val.min(axis=0), a['min'], rtol=1e-6, atol=1e-7)
        if 'max' in a: assert np.allclose(val.max(axis=0), a['max'], rtol=1e-6, atol=1e-7)
        ar.append({'accessor': i, 'view': a['bufferView'], 'byte_offset': offset,
                   'count': a['count'], 'type': a['type'], 'component_type': a['componentType'], 'stride': stride})
    children = [c for n in doc['nodes'] for c in n.get('children', [])]
    assert len(children) == len(set(children))
    assert all(0 <= c < len(doc['nodes']) for c in children)
    for n in range(len(doc['nodes'])):
        seen = set()
        while n in g.parents:
            assert n not in seen
            seen.add(n)
            n = g.parents[n]
    assert len(g.joints) == len(set(g.joints)) == 70
    assert len(g.joint_names) == len(set(g.joint_names))
    meshes = []
    for ni, node in enumerate(g.nodes):
        if 'mesh' not in node: continue
        assert node['skin'] == 0
        for pi, pr in enumerate(doc['meshes'][node['mesh']]['primitives']):
            a = pr['attributes']
            n = len(g.acc(a['POSITION']))
            assert all(len(g.acc(acc)) == n for acc in a.values())
            assert pr.get('mode', 4) == 4
            ids = g.acc(pr['indices']).ravel()
            assert len(ids) % 3 == 0 and ids.min() >= 0 and ids.max() < n
            jw = sorted(k[7:] for k in a if k.startswith('JOINTS_'))
            ww = sorted(k[8:] for k in a if k.startswith('WEIGHTS_'))
            assert jw == ww and jw == [str(i) for i in range(len(jw))]
            item = next(p for p in g.primitives if p['node'] == ni and p['primitive'] == pi)
            count = np.count_nonzero(item['w'], axis=1)
            meshes.append({'name': node['name'], 'node': ni, 'primitive': pi,
                           'vertices': n, 'triangles': len(ids) // 3, 'influence_sets': len(jw),
                           'max_influences': int(count.max()), 'seven_influence_vertices': int(np.sum(count == 7)),
                           'raw_weight_sum_error': item['raw_weight_sum_error']})
    bnode = g.names.index('Prop_Magazine_B')
    _, bv = g.channels[(bnode, 'scale')]
    assert np.count_nonzero(bv) == 0
    W, D, _ = g.world(np.linspace(0, 1, 961))
    bslot = g.joints.index(bnode)
    assert np.count_nonzero(W[:, bslot, :3, :3]) == 0
    assert np.count_nonzero(D[:, bslot, :3, :3]) == 0
    return {'status': 'PASS', 'chunks': chunks, 'buffer_views': views, 'accessors': ar,
            'meshes': meshes, 'total_exported_vertices': sum(p['vertices'] for p in meshes),
            'B_scale_all_keys_and_tangents_exact_zero': True,
            'B_world_linear_transform_and_derivative_exact_zero_at_961_phases': True}


def mappings(g, package):
    b = json.loads((package / 'bindings.json').read_text())
    assert b['glb_sha256'] == g.sha
    assert len(b['joint_map']) == len(g.joints)
    for slot, row in enumerate(b['joint_map']):
        node = g.joints[slot]
        assert row['skin_joint_slot'] == slot and row['node'] == node and row['name'] == g.names[node]
        assert row['parent_node'] == g.parents.get(node)
        pa = g.parents.get(node)
        assert row['parent_joint_slot'] == (g.joints.index(pa) if pa in g.joints else None)
        assert np.array_equal(matrix(row['inverse_bind_column_major']), g.ibm[slot])
        assert row['default_node_trs'] == {k: v for k, v in g.nodes[node].items() if k in ['translation', 'rotation', 'scale', 'matrix']}
    rows = [b['scene_root']] + list(b['semantic_candidates'].values())
    rows += [entry for prop in b['props'] for entry in [prop['mesh'], prop['joint']]]
    for row in rows:
        node = row['node']
        assert row['name'] == g.names[node]
        assert row['skin_joint_slot'] == (g.joints.index(node) if node in g.joints else None)
    W, _, _ = g.world([0])
    errors = {}
    for name, values in b['phase_zero_scene_node_world_matrices'].items():
        errors[name] = float(np.max(np.abs(W[0, g.joint_names.index(name)] - matrix(values))))
    assert max(errors.values()) < 1e-10
    ach = json.loads((package / 'animation_channels.json').read_text())
    seen = set()
    for row in ach['channels']:
        key = (row['node'], row['path'])
        assert key not in seen and key in g.channels
        seen.add(key)
        t, v = g.channels[key]
        assert row['name'] == g.names[key[0]] and row['interpolation'] == 'CUBICSPLINE'
        assert row['key_count'] == len(t) and row['start_s'] == t[0] and row['end_s'] == t[-1]
    assert seen == set(g.channels)
    return {'status': 'PASS', 'all_joint_rows': len(b['joint_map']), 'semantic_prop_root_rows': len(rows),
            'all_animation_channel_rows': len(seen), 'phase_zero_world_matrix_errors': errors}


def bridge(g, package):
    b = json.loads((package / 'bridge_reference_samples.json').read_text())
    assert b['glb_sha256'] == g.sha
    slot = g.joint_names.index('Prop_Rifle')
    times = np.asarray(b['times_s'])
    W, _, _ = g.world(times)
    B = matrix(b['B_bind_mesh_column_major'])
    J = g.ibm[slot] @ B
    expected = np.asarray(b['source_model_from_rigid_column_major']).reshape(-1, 4, 4).transpose(0, 2, 1)
    correct = W[:, slot] @ g.ibm[slot] @ B
    via = W[:, slot] @ J
    points = np.column_stack([b['rigid_reference_vertices'], np.ones(len(b['rigid_reference_vertices']))])
    err = 0.0
    for start in range(0, len(times), 32):
        delta = correct[start:start+32] - expected[start:start+32]
        err = max(err, float(np.max(np.linalg.norm(np.einsum('tij,vj->tvi', delta[:, :3], points), axis=-1))))
    assert np.max(np.abs(correct - expected)) < 1e-5 and err < 1e-5
    assert np.max(np.abs(correct - via)) < 1e-12
    return {'status': 'PASS', 'sample_count': len(times), 'rigid_vertices': len(points),
            'resolved_rifle_node': g.joints[slot], 'resolved_rifle_joint_slot': slot,
            'max_matrix_error_vs_native': float(np.max(np.abs(correct - expected))),
            'max_vertex_error_vs_native_m': err,
            'two_correct_forms_max_error': float(np.max(np.abs(correct - via))),
            'missing_inverse_bind_max_matrix_error': float(np.max(np.abs(W[:, slot] @ B - expected)))}


def optical(g, package):
    b = json.loads((package / 'optical_proxy_bindings.json').read_text())
    assert b['glb_sha256'] == g.sha
    slot = g.joint_names.index('mixamorig:Head')
    assert b['head_node_index'] == g.joints[slot] and b['head_skin_joint_slot'] == slot
    assert np.array_equal(matrix(b['head_inverse_bind_column_major']), g.ibm[slot])
    B = matrix(b['optical_frame_to_bind_mesh_gltf_column_major'])
    J = matrix(b['optical_frame_to_head_node_local_gltf_column_major'])
    assert np.max(np.abs(g.ibm[slot] @ B - J)) < 1e-12
    controls = b['native_blender_evaluated_control_samples']
    W, _, _ = g.world(np.asarray([c['time_s'] for c in controls]))
    points = {}; errors = {}; count = 0
    for side, mapping in b['lens_vertex_mapping'].items():
        for match in mapping['matches']:
            primitive = next(pr for pr in g.primitives if pr['node'] == b['body_node'] and pr['primitive'] == match['primitive'])
            v = match['vertex']
            assert np.array_equal(primitive['p'][v, :3], match['position_bind_mesh_gltf'])
            weights = np.zeros(len(g.joints))
            np.add.at(weights, primitive['j'][v], primitive['w'][v])
            assert weights[slot] == 1 and np.count_nonzero(weights) == 1
            point = (W[:, slot] @ g.ibm[slot] @ primitive['p'][v])[:, :3]
            if side in points: assert np.array_equal(points[side], point)
            points[side] = point
            count += 1
        errors[side] = float(np.max(np.linalg.norm(points[side] - np.asarray([c['native_evaluated_lens_points_model_xyz_m'][side] for c in controls]), axis=-1)))
    frame = W[:, slot] @ g.ibm[slot] @ B
    midpoint = (points['left'] + points['right']) / 2
    midpoint_error = float(np.max(np.linalg.norm(midpoint - frame[:, :3, 3], axis=-1)))
    assert max(errors.values()) < 1e-5 and midpoint_error < 1e-7
    assert np.max(np.abs(frame - W[:, slot] @ J)) < 1e-12
    return {'status': 'PASS', 'classification': b['classification'], 'sample_count': len(controls),
            'lens_vertex_mapping_rows': count, 'native_lens_error_m': errors,
            'midpoint_frame_error_m': midpoint_error,
            'two_correct_forms_max_error': float(np.max(np.abs(frame - W[:, slot] @ J)))}


def static_preservation(g, basepath):
    base = GLB(basepath)
    checks = {k: g.doc[k] == base.doc[k] for k in ['nodes', 'skins', 'meshes', 'materials', 'scenes', 'scene']}
    ids = set()
    for mesh in base.doc['meshes']:
        for pr in mesh['primitives']:
            ids.update(pr['attributes'].values())
            ids.add(pr['indices'])
    ids.update(s['inverseBindMatrices'] for s in base.doc['skins'])
    checks['geometry_and_inverse_bind_arrays_exact'] = all(np.array_equal(base.acc(i), g.acc(i)) for i in ids)
    assert all(checks.values())
    return {'status': 'PASS', 'comparison_glb_sha256': base.sha, 'accessors_checked': len(ids), 'checks': checks}


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--package', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--static-reference', type=Path)
    args = ap.parse_args()
    path = args.package / 'walk_backward_shared_ready_n_c1_loop_a.glb'
    g = GLB(path)
    report = {'schema': 'independent-walk-backward-structure-validation/1', 'glb_sha256': g.sha,
              'method': 'Independent GLB bytes, full accessor bounds/types/strides, hierarchy and all influences. NumPy replay only; no imports of fixture writer or gltf_math.'}
    report['binary'] = structure(g)
    report['metadata_mappings'] = mappings(g, args.package)
    report['bridge'] = bridge(g, args.package)
    report['optical'] = optical(g, args.package)
    if args.static_reference:
        report['static_preservation'] = static_preservation(g, args.static_reference)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'binary'}, indent=2))
