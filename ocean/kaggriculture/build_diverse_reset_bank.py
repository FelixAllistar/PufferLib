#!/usr/bin/env python3
"""One-pass, bounded-memory replay banks with temporal coverage and holdouts.

ZIPs are never extracted. Each day is a resumable atomic shard. Every frame of
every accepted episode is checked, including frames after selected snapshots.
The entire episode is discarded on any mismatch. No simulator/training edits.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures
import csv
import ctypes
import hashlib
import json
import pathlib
import random
import shutil
import tempfile
import time
import zipfile

try:
    import orjson
    decode = orjson.loads
except ImportError:
    decode = json.loads

import build_replay_state_bank as old
from index_replay_states import episode_rows, validate_episode, version_tuple
from replay_native import load_core, replay_config, first_difference

GROUPS = {
    'market': {'sell_now', 'hold_for_later', 'buy_opportunity'},
    'maintenance': {'maintenance_profitable', 'maintenance_unprofitable', 'harvest_ready'},
    'investment': {'early_expansion', 'medium_investment', 'short_investment'},
    'recovery': {'recovery'},
}


def _write_json(path, value):
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8',
            dir=path.parent, prefix=f'.{path.name}.', delete=False) as stream:
        temporary = pathlib.Path(stream.name)
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')
    temporary.replace(path)


def stable_hash(value):
    return int.from_bytes(hashlib.sha256(str(value).encode()).digest()[:8], 'big')


def split_for(eid, seed, date, holdout_date):
    # Seed groups cannot leak across train/holdout, even on different dates.
    key = f'seed:{seed}' if str(seed) not in ('', 'None') else f'episode:{eid}'
    if date >= holdout_date:
        return 'future'
    return 'holdout' if stable_hash(key) % 10 == 0 else 'train'


def typed_equal(left, right):
    """Same equality/type contract as first_difference, without diagnostic paths."""
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(typed_equal(v, right[k]) for k, v in left.items())
    if isinstance(left, list):
        return len(left) == len(right) and all(typed_equal(a, b) for a, b in zip(left, right))
    return left == right


def assert_parity(lib, state, frame, label):
    observation = frame[0]['observation']
    official = {key: observation[key] for key in ('step', 'day', 'hour', 'farms', 'market', 'town')}
    official['done'] = all(record.get('status') == 'DONE' for record in frame)
    official['privates'] = [record['observation']['private'] for record in frame]
    pointer = lib.kg_snapshot_json(state)
    if not pointer:
        raise RuntimeError('kg_snapshot_json returned NULL')
    try:
        native = decode(ctypes.string_at(pointer))
    finally:
        lib.kg_free_string(pointer)
    if not typed_equal(official, native):
        raise AssertionError(f'{label}: {first_difference(official, native)}')


def select_rows(source, episode):
    rows_by_turn = collections.defaultdict(list)
    for row in episode_rows(source, episode):
        rows_by_turn[int(row['turn'])].append(row)
    eid = old.episode_id(episode)
    rng = random.Random(stable_hash(eid))
    end = len(episode['steps']) - 1
    selected = set()
    # Exactly one random state per eighth of the game, independent of expert action.
    for band in range(8):
        choices = list(range(band * end // 8, (band + 1) * end // 8))
        if choices:
            selected.add(rng.choice(choices))
    # At most four extra anchors; both seats are retained in each full state.
    for tags in GROUPS.values():
        choices = [turn for turn, rows in rows_by_turn.items()
                   if turn not in selected and any(tags.intersection(row['scenarios'].split(',')) for row in rows)]
        if choices:
            selected.add(rng.choice(choices))
    return {turn: rows_by_turn[turn] for turn in sorted(selected)}


class Writer:
    def __init__(self, path, version, size):
        self.path, self.version, self.size, self.count = path, version, size, 0
        self.temporary = path.with_suffix('.kgb.part')
        self.manifest = pathlib.Path(f'{path}.manifest.tsv')
        self.manifest_temporary = pathlib.Path(f'{self.manifest}.part')
        self.bank = self.temporary.open('wb+')
        self.bank.write(old.BANK_HEADER.pack(old.BANK_MAGIC, 1, version, size, 0, 0))
        self.stream = self.manifest_temporary.open('w', newline='')
        self.writer = csv.DictWriter(self.stream, fieldnames=old.MANIFEST_FIELDS, delimiter='\t')
        self.writer.writeheader()

    def append(self, payload, row):
        if len(payload) != self.size or hashlib.sha256(payload).hexdigest() != row['sha256']:
            raise ValueError('snapshot length/checksum mismatch')
        row = dict(row, record_index=self.count, byte_offset=self.bank.tell())
        self.bank.write(payload)
        self.writer.writerow(row)
        self.count += 1

    def close(self):
        self.bank.seek(0)
        self.bank.write(old.BANK_HEADER.pack(old.BANK_MAGIC, 1, self.version, self.size, self.count, 0))
        self.bank.close()
        self.stream.close()
        self.temporary.replace(self.path)
        self.manifest_temporary.replace(self.manifest)


def process_archive(task):
    archive_path, options = task
    started = time.monotonic()
    archive_path = pathlib.Path(archive_path)
    date = archive_path.stem[-10:]
    output = pathlib.Path(options['output']) / 'shards' / date
    output.mkdir(parents=True, exist_ok=True)
    summary_path = output / 'summary.json'
    signature = {key: options[key] for key in ('episodes_per_day', 'holdout_date', 'lib_sha256')}
    signature.update(archive_size=archive_path.stat().st_size, pipeline_version=1)
    if options.get('episode_start', 0):
        signature['episode_start'] = options['episode_start']
    if summary_path.exists():
        summary = json.loads(summary_path.read_text())
        if summary['signature'] != signature:
            raise ValueError(f'shard configuration changed: {output}')
        return summary
    if shutil.disk_usage(output).free < options['reserve_gib'] * 2**30:
        raise RuntimeError('disk reserve reached')
    lib = load_core(pathlib.Path(options['lib']))
    size, version = int(lib.kg_state_serialized_size()), int(lib.kg_state_serialization_version())
    writers = {split: Writer(output / f'{split}.kgb', version, size) for split in ('train', 'holdout', 'future')}
    counts, skips, examples = collections.Counter(), collections.Counter(), []
    # Existing helper's resume proof uses the exact same optimized parity checks.
    old.assert_parity = assert_parity
    with zipfile.ZipFile(archive_path) as archive:
        names = [name for name in archive.namelist() if name.endswith('.json')]
        names.sort(key=lambda name: stable_hash(f'{date}:{name}'))
        for name in names[options.get('episode_start', 0):options['episodes_per_day']]:
            source = f'{archive_path}:{name}'
            episode = decode(archive.read(name))  # ZIP CRC checked by Python here.
            reason = validate_episode(episode, version_tuple('1.32.0'))
            if reason:
                skips[reason] += 1
                continue
            eid = old.episode_id(episode)
            cfg = replay_config(episode)
            seed = int(cfg.seed)
            split = split_for(eid, seed, date, options['holdout_date'])
            targets = select_rows(source, episode)
            state = lib.kg_create(ctypes.byref(cfg))
            if not state:
                raise RuntimeError('kg_create failed')
            snapshots = []
            try:
                steps = episode['steps']
                for turn, frame in enumerate(steps):
                    assert_parity(lib, state, frame, f'{eid}:{turn}')
                    if turn in targets:
                        payload = old.serialize_state(lib, state, size)
                        old.verify_resume(lib, cfg, payload, frame, steps[turn + 1], f'{eid}:{turn}')
                        actions, _ = old.action_pair(steps[turn + 1])
                        rows = targets[turn]
                        row = dict(record_index=0, byte_offset=0, byte_size=size,
                                   sha256=hashlib.sha256(payload).hexdigest(), episode_id=eid,
                                   source=source, module_version=episode['module_version'], seed=seed,
                                   turn=turn, players='0,1', state_keys=','.join(row['state_key'] for row in rows),
                                   expert_actions=json.dumps(actions, separators=(',', ':')),
                                   index_rows=json.dumps(rows, separators=(',', ':')))
                        snapshots.append((payload, row))
                    if turn + 1 < len(steps):
                        _, actions = old.action_pair(steps[turn + 1])
                        lib.kg_step(state, actions)
            except AssertionError as error:
                skips['parity_incompatible_episode'] += 1
                if len(examples) < 5:
                    examples.append(str(error))
                continue
            finally:
                lib.kg_destroy(state)
            for payload, row in snapshots:
                writers[split].append(payload, row)
            counts[f'{split}_episodes'] += 1
            counts[f'{split}_states'] += len(snapshots)
            counts['parity_frames'] += len(steps)
            counts['resume_checks'] += len(snapshots)
            if sum(counts[f'{s}_episodes'] for s in writers) % 25 == 0:
                print(json.dumps(dict(date=date, progress=dict(counts), seconds=time.monotonic()-started)), flush=True)
    for writer in writers.values():
        writer.close()
    summary = dict(date=date, signature=signature, counts=dict(counts), skipped=dict(skips),
                   incompatible_examples=examples, seconds=time.monotonic()-started,
                   native_state_size=size, native_state_version=version)
    _write_json(summary_path, summary)
    return summary


def merge(options, summaries):
    output = pathlib.Path(options['output'])
    first = summaries[0]
    size, version = first['native_state_size'], first['native_state_version']
    writers = {name: Writer(output / f'{name}.kgb', version, size)
               for name in ('full', 'holdout', 'future', *GROUPS)}
    seen, split_keys = set(), collections.defaultdict(set)
    distributions = {key: collections.Counter() for key in ('dates', 'time_bands', 'agents', 'farm_types')}
    # First chronological occurrence wins when Kaggle republishes an episode.
    for summary in sorted(summaries, key=lambda value: value['date']):
        for split in ('train', 'holdout', 'future'):
            shard_dir = pathlib.Path(summary.get('shard_directory', output / 'shards' / summary['date']))
            bank_path = shard_dir / f'{split}.kgb'
            with bank_path.open('rb') as bank, pathlib.Path(f'{bank_path}.manifest.tsv').open() as manifest:
                header = old.BANK_HEADER.unpack(bank.read(old.BANK_HEADER.size))
                if header[:4] != (old.BANK_MAGIC, 1, version, size):
                    raise ValueError(f'incompatible shard: {bank_path}')
                for row in csv.DictReader(manifest, delimiter='\t'):
                    key = (row['episode_id'], row['turn'])
                    if key in seen:
                        continue
                    # Future holdout must not contain any training/regular-holdout seed.
                    seed_key = row['seed'] or row['episode_id']
                    if split != 'future' and seed_key in options.get('reserved_future_seeds', set()):
                        continue
                    if split == 'future' and (seed_key in split_keys['train'] or seed_key in split_keys['holdout']):
                        continue
                    if split == 'train' and seed_key in split_keys['holdout'] or split == 'holdout' and seed_key in split_keys['train']:
                        raise AssertionError('seed leakage')
                    seen.add(key)
                    split_keys[split].add(seed_key)
                    bank.seek(int(row['byte_offset']))
                    payload = bank.read(size)
                    writers['full' if split == 'train' else split].append(payload, row)
                    if split != 'train':
                        continue
                    index_rows = json.loads(row['index_rows'])
                    tags = set(','.join(item['scenarios'] for item in index_rows).split(','))
                    # Hash gate plus cap keeps scenario banks small and date-distributed.
                    for name, requested in GROUPS.items():
                        if requested.intersection(tags) and stable_hash(f'{name}:{key}') % 40 == 0 and writers[name].count < 4096:
                            writers[name].append(payload, row)
                    distributions['dates'][summary['date']] += 1
                    distributions['time_bands'][int(row['turn']) // 90] += 1
                    for item in index_rows:
                        distributions['agents'][item['agent']] += 1
                        plants, animals = int(item['plants']), int(item['animals'])
                        distributions['farm_types']['mixed' if plants and animals else 'crop' if plants else 'animal' if animals else 'empty'] += 1
    for writer in writers.values():
        writer.close()
    report = dict(banks={name: dict(path=str(writer.path), states=writer.count,
                                   bytes=writer.path.stat().st_size) for name, writer in writers.items()},
                  distributions={key: dict(value) for key, value in distributions.items()},
                  seed_groups={key: len(value) for key, value in split_keys.items()}, shards=summaries,
                  guarantees=['full-episode frame parity', 'snapshot roundtrip and next-step parity',
                              'seed-separated holdout', 'latest-date holdout excludes previously seen seeds',
                              'at most 12 states per episode', 'eight uniform time anchors plus four scenario anchors'])
    _write_json(output / 'summary.json', report)
    return report


def reusable_shards(directory, options):
    """Reference previously verified shards without copying or modifying them."""
    directory = pathlib.Path(directory).resolve()
    if directory == pathlib.Path(options['output']).resolve():
        raise ValueError('reuse bank and output must be different directories')
    audit = json.loads((directory / 'audit.json').read_text())
    if audit.get('passed') is not True:
        raise ValueError('reuse bank has not passed its final audit')
    previous = json.loads((directory / 'summary.json').read_text())
    summaries = []
    for summary in previous['shards']:
        signature = summary['signature']
        if signature['lib_sha256'] != options['lib_sha256'] or signature['holdout_date'] != options['holdout_date']:
            raise ValueError('reuse bank native library or holdout boundary differs')
        if signature['episodes_per_day'] > options['episode_start']:
            raise ValueError('new episode interval overlaps reused shards')
        copied = dict(summary)
        copied['shard_directory'] = str(pathlib.Path(summary.get(
            'shard_directory', directory / 'shards' / summary['date'])).resolve())
        summaries.append(copied)
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archives', nargs='+', type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--lib', default=str(old.DEFAULT_LIB))
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--episodes-per-day', type=int, default=300)
    parser.add_argument('--episode-start', type=int, default=0,
                        help='Skip this many hash-ordered episodes per day (incremental builds)')
    parser.add_argument('--reuse-bank', type=pathlib.Path,
                        help='Reuse audited shards from this bank without reconstructing them')
    parser.add_argument('--min-full-states', type=int, default=0,
                        help='Fail the build if the merged training bank is smaller than this')
    parser.add_argument('--holdout-date', default='2026-09-12')
    parser.add_argument('--reserve-gib', type=float, default=30)
    args = parser.parse_args()
    if args.jobs < 1 or args.episodes_per_day < 1:
        parser.error('jobs and episodes-per-day must be positive')
    if not 0 <= args.episode_start < args.episodes_per_day:
        parser.error('episode-start must be nonnegative and below episodes-per-day')
    args.output.mkdir(parents=True, exist_ok=True)
    options = vars(args).copy()
    options['output'] = str(args.output.resolve())
    options['lib'] = str(pathlib.Path(args.lib).resolve())
    options['lib_sha256'] = hashlib.sha256(pathlib.Path(args.lib).read_bytes()).hexdigest()
    reused = reusable_shards(args.reuse_bank, options) if args.reuse_bank else []
    if args.reuse_bank:
        with (args.reuse_bank / 'future.kgb.manifest.tsv').open() as stream:
            options['reserved_future_seeds'] = {row['seed'] or row['episode_id']
                                               for row in csv.DictReader(stream, delimiter='\t')}
    # Shards + merged full + holdouts + small scenario copies + manifests.
    new_episodes = 0
    for archive in args.archives:
        with zipfile.ZipFile(archive) as stream:
            count = sum(name.endswith('.json') for name in stream.namelist())
        new_episodes += max(0, min(count, args.episodes_per_day) - args.episode_start)
    estimate = new_episodes * 12 * 42000 * 2.3
    estimate += sum(sum(s['counts'].get(f'{split}_states', 0) for split in ('train', 'holdout', 'future'))
                    * s['native_state_size'] * 1.2 for s in reused)
    if shutil.disk_usage(args.output).free - estimate < args.reserve_gib * 2**30:
        raise RuntimeError('projected build would breach disk reserve')
    started = time.monotonic()
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
        summaries = list(pool.map(process_archive, [(str(path.resolve()), options) for path in args.archives]))
    report = merge(options, reused + summaries)
    print(json.dumps(dict(banks=report['banks'], seconds=time.monotonic()-started)), flush=True)
    if report['banks']['full']['states'] < args.min_full_states:
        raise RuntimeError(f"training bank has {report['banks']['full']['states']} states, below requested {args.min_full_states}")


if __name__ == '__main__':
    main()
