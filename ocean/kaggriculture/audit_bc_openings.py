#!/usr/bin/env python3
"""Audit teacher opening command timing, without claiming successful fills."""
import argparse
import collections
import gzip
import json
import pathlib
import statistics


def first_commands(actions):
    first = {}
    for turn, action in enumerate(actions):
        for command in action.get('market', []):
            if not command:
                continue
            op = command[0]
            if op in ('HIRE', 'BUY_LAND'):
                first.setdefault(op.lower(), turn)
            if op == 'BUY_ANIMAL' and len(command) > 1:
                first.setdefault('buy_' + command[1].lower(), turn)
        for command in [action.get('farmer', []), *action.get('hands', [])]:
            if not command:
                continue
            if command[0] == 'BUILD_PASTURE':
                first.setdefault('build_pasture', turn)
            if command[0] == 'PLACE' and len(command) > 1 and command[1] == 'COW':
                first.setdefault('place_cow', turn)
    return first


def audit(manifest, teacher):
    records = []
    for entry in manifest['cache']:
        with gzip.open(entry['path'], 'rt') as stream:
            tape = json.load(stream)
        names = tape['info']['TeamNames']
        if names.count(teacher) != 1:
            raise ValueError('teacher must identify exactly one seat')
        seat = names.index(teacher)
        records.append(dict(episode_id=entry['episode_id'], split=entry['split'],
                            first=first_commands(pair[seat] for pair in tape['actions'])))
    groups = {}
    for split in ('train', 'holdout'):
        selected = [r for r in records if r['split'] == split]
        events = collections.defaultdict(list)
        for record in selected:
            for event, turn in record['first'].items():
                events[event].append(turn)
        groups[split] = dict(games=len(selected), events={
            event: dict(games=len(turns), median_turn=statistics.median(turns),
                        min_turn=min(turns), max_turn=max(turns),
                        games_before_turn_168=sum(t < 168 for t in turns))
            for event, turns in sorted(events.items())})
    return dict(teacher=teacher, groups=groups, records=records,
                caveat='Zero-based command turns, NOT successful purchases/placements. PLACE COW may return a cow to the shed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=pathlib.Path)
    parser.add_argument('--teacher', default='Majkel1337')
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--portable', type=pathlib.Path)
    parser.add_argument('--remote-tapes', default='/workspace/PufferLib/qualification/bc_expansion_20260923/tapes')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    result = audit(manifest, args.teacher)
    with args.output.open('x') as stream:
        json.dump(result, stream, indent=2)
    if args.portable:
        entries = [dict(e, path=str(pathlib.PurePosixPath(args.remote_tapes) / pathlib.Path(e['path']).name))
                   for e in manifest['cache']]
        with args.portable.open('x') as stream:
            json.dump(dict(cache=entries, source_manifest=str(args.manifest)), stream, indent=2)
    print(json.dumps(result['groups'], indent=2))


if __name__ == '__main__':
    main()
