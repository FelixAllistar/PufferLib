#!/usr/bin/env python3
"""CPU validation of reset banks and leakage; writes an audit.json report."""
import argparse
import collections
import configparser
import csv
import ctypes
import hashlib
import json
import pathlib

from build_replay_state_bank import BANK_HEADER, BANK_MAGIC, DEFAULT_LIB
from replay_native import CConfig, load_core


def audit_bank(path, lib, expected, allow_empty=False):
    counts = collections.Counter()
    episodes, seeds, records, agents = set(), set(), set(), collections.Counter()
    cfg = CConfig()
    lib.kg_config_default(ctypes.byref(cfg))
    state = lib.kg_create(ctypes.byref(cfg))
    try:
        with path.open('rb') as bank, pathlib.Path(f'{path}.manifest.tsv').open() as manifest:
            magic, version, native_version, size, total, reserved = BANK_HEADER.unpack(bank.read(BANK_HEADER.size))
            if (magic, version, native_version, size, reserved) != (
                    BANK_MAGIC, 1, lib.kg_state_serialization_version(), lib.kg_state_serialized_size(), 0):
                raise ValueError(f'invalid bank header: {path}')
            if not total and not allow_empty:
                raise ValueError(f'empty bank: {path}')
            for index, row in enumerate(csv.DictReader(manifest, delimiter='\t')):
                if int(row['record_index']) != index or int(row['byte_offset']) != bank.tell() or int(row['byte_size']) != size:
                    raise ValueError(f'manifest offset/size mismatch: {path}:{index}')
                payload = bank.read(size)
                if len(payload) != size or hashlib.sha256(payload).hexdigest() != row['sha256']:
                    raise ValueError(f'payload checksum mismatch: {path}:{index}')
                snapshot_config = CConfig.from_buffer_copy(payload)
                for name, value in expected.items():
                    if getattr(snapshot_config, name) != value:
                        raise ValueError(f'config mismatch {path}:{index}:{name}')
                buffer = ctypes.create_string_buffer(payload, size)
                if not lib.kg_state_deserialize(state, buffer, size):
                    raise ValueError(f'native deserialize rejected {path}:{index}')
                if lib.kg_state_step(state) != int(row['turn']) or lib.kg_done(state):
                    raise ValueError(f'terminal/wrong-turn state: {path}:{index}')
                key = (row['episode_id'], row['turn'])
                if key in records:
                    raise ValueError(f'duplicate episode/turn: {key}')
                records.add(key)
                episodes.add(row['episode_id'])
                seeds.add(row['seed'])
                counts[f"time_band_{int(row['turn']) // 90}"] += 1
                for item in json.loads(row['index_rows']):
                    plants, animals = int(item['plants']), int(item['animals'])
                    kind = 'mixed' if plants and animals else 'crop' if plants else 'animal' if animals else 'empty'
                    counts[f'farm_{kind}'] += 1
                    agents[item['agent']] += 1
            if len(records) != total or bank.read(1):
                raise ValueError(f'bank length/count mismatch: {path}')
    finally:
        lib.kg_destroy(state)
    return dict(states=total, episodes=len(episodes), seed_groups=len(seeds),
                agent_names=len(agents), top_agents=agents.most_common(10), counts=dict(counts)), seeds, episodes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, type=pathlib.Path)
    parser.add_argument('--config', required=True, type=pathlib.Path)
    parser.add_argument('--lib', type=pathlib.Path, default=DEFAULT_LIB)
    parser.add_argument('--require-superset', type=pathlib.Path,
                        help='Require each split to retain all snapshots from this older bank')
    parser.add_argument('--min-full-states', type=int, default=0)
    args = parser.parse_args()
    config = configparser.ConfigParser()
    assert config.read(args.config), 'config file not found'
    lib = load_core(args.lib)
    default = CConfig()
    lib.kg_config_default(ctypes.byref(default))
    # The native environment initializes KGConfig defaults, not legacy rule overrides.
    expected = {name: getattr(default, name) for name, _ in CConfig._fields_ if name != 'seed'}
    for name, value in expected.items():
        if config.has_option('env', name) and config.getfloat('env', name) != value:
            raise ValueError(f'native trainer does not apply rule override: {name}')
    report, seed_groups, episode_groups = {}, {}, {}
    for path in sorted(args.directory.glob('*.kgb')):
        report[path.stem], seed_groups[path.stem], episode_groups[path.stem] = audit_bank(
            path, lib, expected, allow_empty=path.stem != 'full')
    for left, right in [('full', 'holdout'), ('full', 'future'), ('holdout', 'future')]:
        if seed_groups[left] & seed_groups[right] or episode_groups[left] & episode_groups[right]:
            raise AssertionError(f'holdout leakage: {left}/{right}')
    if report['full']['states'] < args.min_full_states:
        raise AssertionError('training bank is smaller than requested')
    if args.require_superset:
        for split in ('full', 'holdout', 'future'):
            def hashes(directory):
                with (directory / f'{split}.kgb.manifest.tsv').open() as stream:
                    return {(row['episode_id'], row['turn']): row['sha256']
                            for row in csv.DictReader(stream, delimiter='\t')}
            before, after = hashes(args.require_superset), hashes(args.directory)
            if any(after.get(key) != value for key, value in before.items()):
                raise AssertionError(f'expanded bank lost/changed a previous {split} snapshot')
    result = dict(passed=True, config_compatible=str(args.config), banks=report,
                  disjoint_splits=['full', 'holdout', 'future'],
                  superset_of=str(args.require_superset) if args.require_superset else None,
                  minimum_full_states=args.min_full_states)
    (args.directory / 'audit.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
