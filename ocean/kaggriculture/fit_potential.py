#!/usr/bin/env python3
"""Stream parity-checked replay features, then fit a frozen terminal-return ridge.

No BC action projection, actor/critic fitting, GPU, or PPO modifications.
Both seats are retained regardless of player identity or outcome. A seed group
and both seats stay in one train/validation/test partition. Compact per-game
caches are reusable; the official JSON remains compressed in its archive.
"""
import argparse
import collections
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import zipfile

import numpy as np

import prepare_bc_replays as prep
import replay_native as native
from refresh_daily_replays import _write_json
from scan_replay_identities import _expand


FEATURE_COUNT = 205
VERSION = 1


def feature_names():
    names = ['remaining', 'hour', 'remaining_squared', 'cash', 'opponent_cash',
        'plots', 'workers', 'empty', 'weeds', 'empty_coop', 'empty_pasture',
        'crops', 'animals', 'maintenance_due', 'carried_wheat', 'shed_wheat']
    for field in ['count', 'ready', 'age_sum', 'unwatered', 'neglect']:
        names += [f'crop_{crop}_{field}' for crop in native.CROPS]
    for field in ['count', 'yield', 'age_sum', 'unfed', 'neglect']:
        names += [f'animal_{animal}_{field}' for animal in native.ANIMALS]
    names += [f'seeds_{crop}' for crop in native.CROPS]
    names += [f'stock_{item}' for item in native.ITEMS]
    for field in ['price', 'inventory']:
        names += [f'market_{item}_{field}' for item in list(native.ITEMS)[:9]]
    names += ['opponent_plots', 'opponent_workers', 'opponent_crops', 'opponent_animals']
    assert len(names) == 95
    names += [f'{name}_x_remaining' for name in names[3:95]]
    for plots in range(1, 5):
        names += [f'plots_equal_{plots}', f'plots_equal_{plots}_x_remaining']
    for animal in native.ANIMALS:
        names += [f'{animal}_feed_support', f'{animal}_feed_support_x_remaining']
    names += ['maintenance_per_worker', 'maintenance_x_workers', 'plots_x_cash', 'empty_x_workers']
    assert len(names) == FEATURE_COUNT
    return names


class Model(C.Structure):
    _fields_ = [('magic', C.c_char * 8), ('version', C.c_uint32),
        ('features', C.c_uint32), ('state_version', C.c_uint32), ('reserved', C.c_uint32),
        ('gamma', C.c_double), ('intercept', C.c_float),
        ('mean', C.c_float * FEATURE_COUNT), ('inverse_scale', C.c_float * FEATURE_COUNT),
        ('weights', C.c_float * FEATURE_COUNT)]


def load_library(path):
    lib = native.load_core(path)
    lib.kag_potential_features.argtypes = [C.c_void_p, C.c_int, C.c_void_p]
    lib.kag_potential_features.restype = None
    lib.kag_potential_predict.argtypes = [C.POINTER(Model), C.c_void_p, C.c_int, C.c_float]
    lib.kag_potential_predict.restype = C.c_float
    assert lib.kag_potential_count() == FEATURE_COUNT
    assert lib.kag_potential_size() == C.sizeof(Model)
    return lib


def group_split(seed, split_seed=73):
    value = int(hashlib.sha256(f'{split_seed}:{seed}'.encode()).hexdigest()[:8], 16) / 2**32
    return 'train' if value < .7 else 'validation' if value < .85 else 'test'


def extract(lib, episode, stride):
    assert episode['module_version'] == '1.32.7'
    tape = prep.compact_tape(episode)
    config = native.replay_config(tape)
    expected = native.CConfig()
    lib.kg_config_default(C.byref(expected))
    prefix = native.CConfig.seed.offset
    assert bytes(config)[:prefix] == bytes(expected)[:prefix]
    selected = sorted(set(range(0, tape['frames'] - 1, stride)) | {tape['frames'] - 2})
    x = np.zeros((len(selected) * 2, FEATURE_COUNT), np.float32)
    turns, players = [], []
    state = lib.kg_create(C.byref(config))
    assert state
    row = 0
    try:
        for turn, frame in enumerate(episode['steps']):
            difference = native.first_difference(
                native.canonical_replay_frame(frame), native.c_snapshot(lib, state))
            if difference:
                raise ValueError(f'parity at turn {turn}: {difference}')
            if turn in selected:
                for player in range(2):
                    lib.kag_potential_features(state, player, x[row].ctypes.data)
                    turns.append(turn)
                    players.append(player)
                    row += 1
            if turn < tape['frames'] - 1:
                actions = (native.CAction * 2)(*(native.c_action(a) for a in tape['actions'][turn]))
                lib.kg_step(state, actions)
        money = [lib.kg_player_money(state, p) for p in range(2)]
        assert lib.kg_done(state) and money == tape['rewards']
    finally:
        lib.kg_destroy(state)
    assert np.isfinite(x).all()
    return dict(x=x, turn=np.array(turns), player=np.array(players),
        final_cash=np.array(money), starting_money=np.array(config.starting_money),
        frames=np.array(tape['frames']), seed=np.array(str(config.seed)))


def build(args):
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    cache = root / 'games'
    cache.mkdir(exist_ok=True)
    paths = _expand(args.inputs)
    rows, inventory_counts = prep.catalog(paths, version='1.32.7')
    unique = {row['episode_id']: row for row in rows}
    selected = sorted(unique.values(), key=lambda r: hashlib.sha256(
        f'potential:{args.seed}:{r["episode_id"]}'.encode()).digest())
    if args.limit:
        selected = selected[:args.limit]
    assert selected, 'No exact-version replay games found'
    library = args.lib.resolve()
    digest = hashlib.sha256(library.read_bytes()).hexdigest()
    lib = load_library(library)
    manifest = dict(version=VERSION, library_sha256=digest, stride=args.stride,
        feature_names=feature_names(), split_seed=args.seed, selection='outcome-independent hash',
        archive_inventory=inventory_counts, available_games=len(unique), records=[], rejected=[])
    start = time.monotonic()
    for index, row in enumerate(selected):
        key = f'{row["episode_id"]}_{row["crc32"]}_{digest[:16]}_s{args.stride}'
        destination = cache / f'{key}.npz'
        try:
            if destination.exists():
                with np.load(destination, allow_pickle=False) as saved:
                    seed = str(saved['seed'])
                    sample_count = len(saved['x'])
            else:
                with zipfile.ZipFile(row['archive']) as archive:
                    member = archive.getinfo(row['member'])
                    assert f'{member.CRC:08x}' == row['crc32']
                    episode = json.loads(archive.read(member))
                assert str(episode['info']['EpisodeId']) == row['episode_id']
                values = extract(lib, episode, args.stride)
                seed, sample_count = str(values['seed']), len(values['x'])
                del episode
                with tempfile.NamedTemporaryFile(dir=cache, delete=False) as stream:
                    temporary = Path(stream.name)
                    np.savez_compressed(stream, **values)
                try:
                    os.link(temporary, destination)
                finally:
                    temporary.unlink()
            manifest['records'].append(dict(episode_id=row['episode_id'], seed=seed,
                split=group_split(seed, args.seed), cache=str(destination.relative_to(root)),
                samples=sample_count, source_archive=row['archive'], source_member=row['member'],
                crc32=row['crc32']))
        except (AssertionError, ValueError, KeyError, TypeError) as error:
            manifest['rejected'].append(dict(episode_id=row['episode_id'], reason=str(error)))
            print(json.dumps(manifest['rejected'][-1]), flush=True)
        if (index + 1) % 25 == 0 or index + 1 == len(selected):
            manifest['elapsed_seconds'] = time.monotonic() - start
            manifest['complete'] = index + 1 == len(selected)
            _write_json(root / 'dataset.json', manifest)
            progress = dict(processed=index+1, accepted=len(manifest['records']),
                rejected=len(manifest['rejected']), seconds=manifest['elapsed_seconds'])
            print(json.dumps(progress), flush=True)
    assert manifest['records'], 'All replay games failed parity'


def statistics(root, manifest, gamma):
    n = FEATURE_COUNT + 1
    sums = {(split, phase): dict(xx=np.zeros((n, n)), xy=np.zeros(n), yy=0., weight=0.)
        for split in ['train', 'validation', 'test'] for phase in ['all', 'early', 'mid', 'late']}
    for record in manifest['records']:
        with np.load(root / record['cache'], allow_pickle=False) as saved:
            x = np.column_stack((np.ones(len(saved['x'])), saved['x'])).astype(np.float64)
            exponent = int(saved['frames']) - 2 - saved['turn']
            y = gamma ** exponent * (saved['final_cash'][saved['player']] /
                float(saved['starting_money']) - 1)
            fraction = saved['turn'] / (int(saved['frames']) - 1)
        phase_id = np.minimum(2, (fraction * 3).astype(int))
        weight = 1 / len(x)  # Each complete game (both seats together) has unit weight.
        for label, chosen in [('all', np.ones(len(x), bool)),
                *[(phase, phase_id == i) for i, phase in enumerate(['early', 'mid', 'late'])]]:
            value = sums[record['split'], label]
            a, b = x[chosen], y[chosen]
            value['xx'] += weight * (a.T @ a)
            value['xy'] += weight * (a.T @ b)
            value['yy'] += weight * (b @ b)
            value['weight'] += weight * len(a)
    return sums


def ridge(stats, indices, alpha):
    indices = np.asarray(indices) + 1
    xx, xy, weight = stats['xx'], stats['xy'], stats['weight']
    mean = xx[0, indices] / weight
    scale = np.sqrt(np.maximum(0, xx[indices, indices] / weight - mean**2))
    scale[scale < 1e-6] = 1
    covariance = (xx[np.ix_(indices, indices)] - weight * np.outer(mean, mean))
    covariance /= np.outer(scale, scale)
    rhs = (xy[indices] - mean * xy[0]) / scale
    weights = np.linalg.solve(covariance + alpha * np.eye(len(indices)), rhs)
    coefficients = np.zeros(FEATURE_COUNT + 1)
    coefficients[indices] = weights / scale
    coefficients[0] = xy[0] / weight - np.dot(mean, weights / scale)
    return coefficients, mean, scale, weights


def errors(stats, coefficients):
    count = stats['weight']
    assert count > 0, 'Empty split; prepare more independent games'
    residual = max(0, stats['yy'] - 2 * coefficients @ stats['xy'] +
        coefficients @ stats['xx'] @ coefficients)
    total = max(1e-12, stats['yy'] - stats['xy'][0]**2 / count)
    return dict(rmse=float(np.sqrt(residual / count)), r2=float(1-residual/total),
        mean_target=float(stats['xy'][0] / count), game_equivalent_weight=count)


def fit(args):
    root = args.dataset.resolve()
    manifest = json.loads((root / 'dataset.json').read_text())
    assert manifest['complete'] and manifest['version'] == VERSION
    assert manifest['feature_names'] == feature_names()
    splits = collections.Counter(r['split'] for r in manifest['records'])
    assert all(splits[k] >= 2 for k in ['train', 'validation', 'test']), splits
    gamma = float(np.float32(args.gamma))  # Match the trainer's actual gamma.
    assert 0 < gamma <= 1
    stats = statistics(root, manifest, gamma)
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / 'dataset.json').write_text(json.dumps(manifest, indent=2) + '\n')
    report = dict(version=VERSION, gamma=gamma, games=dict(splits),
        dataset=str(root / 'dataset.json'), library_sha256=manifest['library_sha256'],
        dataset_sha256=hashlib.sha256((root/'dataset.json').read_bytes()).hexdigest(),
        target='discounted (final_cash - root_start_cash) / starting_money',
        reset_adjustment='subtract discounted (episode_start/root_start - 1)',
        feature_names=feature_names(), models={})
    models = [('cash_time', [0, 1, 2, 3, 95]), ('ridge', list(range(FEATURE_COUNT)))]
    for name, indices in models:
        candidates = []
        for alpha in args.alphas:
            assert alpha > 0
            coefficients, mean, scale, weights = ridge(stats['train', 'all'], indices, alpha)
            error = errors(stats['validation', 'all'], coefficients)
            candidates.append((error['rmse'], alpha, coefficients, mean, scale, weights))
        _, alpha, coefficients, mean, scale, weights = min(candidates, key=lambda x: x[0])
        report['models'][name] = dict(alpha=alpha,
            alpha_validation=[dict(alpha=c[1], rmse=c[0]) for c in candidates],
            scores={split:{phase:errors(stats[split,phase],coefficients)
                for phase in ['all','early','mid','late']}
                for split in ['train','validation','test']})
        if name == 'ridge':
            model = Model(magic=b'KGRIDGE1', version=VERSION, features=FEATURE_COUNT,
                state_version=1, gamma=gamma, intercept=stats['train','all']['xy'][0] /
                stats['train','all']['weight'])
            model.mean[:] = mean
            model.inverse_scale[:] = 1 / scale
            model.weights[:] = weights
            (args.output / 'potential.bin').write_bytes(bytes(model))
            report.update(intercept=model.intercept, mean=list(model.mean),
                inverse_scale=list(model.inverse_scale), weights=list(model.weights))
            # Evaluate actual exported float32 parameters too, not just the FP64 solve.
            exported = np.asarray(model.weights) * np.asarray(model.inverse_scale)
            exported = np.r_[model.intercept - exported @ np.asarray(model.mean), exported]
            report['exported_test'] = {p:errors(stats['test',p],exported)
                for p in ['all','early','mid','late']}
    payload = (args.output/'potential.bin').read_bytes()
    report['potential_sha256'] = hashlib.sha256(payload).hexdigest()
    _write_json(args.output / 'potential.json', report)
    summary = {k:report[k] for k in ['games','models','exported_test']}
    print(json.dumps(summary, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    dataset = commands.add_parser('build')
    dataset.add_argument('inputs', nargs='+')
    dataset.add_argument('--lib', type=Path, required=True)
    dataset.add_argument('--output', type=Path, required=True)
    dataset.add_argument('--limit', type=int, default=10000)
    dataset.add_argument('--stride', type=int, default=12)
    dataset.add_argument('--seed', type=int, default=73)
    model = commands.add_parser('fit')
    model.add_argument('--dataset', type=Path, required=True)
    model.add_argument('--output', type=Path, required=True)
    model.add_argument('--gamma', type=float, required=True)
    model.add_argument('--alphas', type=float, nargs='+', default=[.01,.1,1,10,100,1000])
    args = parser.parse_args()
    if args.command == 'build':
        assert args.stride > 0 and args.limit >= 0
        build(args)
    else:
        fit(args)


if __name__ == '__main__':
    main()
