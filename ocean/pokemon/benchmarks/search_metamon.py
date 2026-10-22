#!/usr/bin/env python3
"""Screen saved league policies against TaurosV0, then confirm team leaders.

Runs on the evaluation host, sequentially, alongside the independent trainer.
The discovery manifest freezes the candidate pool; reruns resume completed jobs.
"""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def identity(checkpoint):
    config = checkpoint.with_name('config.ini')
    ini = configparser.ConfigParser()
    with config.open() as stream:
        ini.read_file(stream)
    return (ini['env']['learner_team'], ini['env']['learner_lead']), config


def discover(league):
    candidates = {}
    team_names = {}
    skipped = []

    def add(checkpoint, name, source):
        checkpoint = checkpoint.resolve(strict=True)
        team, config = identity(checkpoint)
        sha = digest(checkpoint)
        key = hashlib.sha256((sha + repr(team)).encode()).hexdigest()[:20]
        if key not in candidates:
            candidates[key] = dict(id=key, checkpoint=str(checkpoint),
                sha256=sha, config_sha256=digest(config), team_key=list(team),
                policy=name, sources=[])
        candidates[key]['sources'].append(source)

    for member in sorted((league / 'members').iterdir()):
        if not member.is_dir():
            continue
        for checkpoint in sorted(member.glob('*/weights.bin')):
            team, _ = identity(checkpoint)
            team_names[team] = member.name
            add(checkpoint, member.name, 'league:' + checkpoint.parent.name)

    # Oak is only a cost-saving shortlist for the many historical sweep trials.
    # Every published league snapshot is included regardless of its Oak score.
    reports = list((ROOT / 'logs/pokemon').glob('*.ini.oak/summary.json'))
    for experiment in (ROOT / 'reports/pokemon_experiments').glob('*continue*'):
        reports.extend((experiment / 'logs/pokemon').glob('*.ini.oak/summary.json'))
    groups = {}
    for path in reports:
        try:
            report = json.loads(path.read_text())
            checkpoint = Path(report['checkpoint'])
            if not checkpoint.is_absolute():
                checkpoint = ROOT / checkpoint
            team, _ = identity(checkpoint)
            if team not in team_names:
                continue
            if digest(checkpoint) != report['sha256']:
                raise ValueError('checkpoint hash mismatch')
            groups.setdefault(team, []).append((report['scores']['argmax']['score'],
                                                str(checkpoint), str(path)))
        except (OSError, ValueError, KeyError, configparser.Error) as exc:
            skipped.append(dict(report=str(path), reason=str(exc)))
    for team, rows in groups.items():
        chosen = set()
        for _, checkpoint, report in sorted(rows, reverse=True):
            sha = digest(Path(checkpoint))
            if sha in chosen:
                continue
            chosen.add(sha)
            add(Path(checkpoint), team_names[team], 'sweep_report:' + report)
            if len(chosen) == 20:
                break
    return dict(selection='All published league snapshots plus top 20 distinct Oak-scored '
                'historical sweep checkpoints per team; fixed at discovery time',
                candidates=list(candidates.values()), skipped_reports=skipped,
                created_unix=time.time(), league=str(league))


def shortlist(rows, per_team, global_count):
    ordered = sorted(rows, key=lambda r: (-r['score'], r['id']))
    counts = {}
    selected = {}
    for row in ordered:
        team = tuple(row['team_key'])
        if counts.get(team, 0) < per_team:
            selected[row['id']] = row
            counts[team] = counts.get(team, 0) + 1
    for row in ordered[:global_count]:
        selected[row['id']] = row
    return list(selected.values())


def evaluate(candidate, stage, games, seed, output):
    checkpoint = Path(candidate['checkpoint'])
    if digest(checkpoint) != candidate['sha256'] or \
            digest(checkpoint.with_name('config.ini')) != candidate['config_sha256']:
        raise ValueError('Candidate changed: ' + candidate['id'])
    directory = output / stage / candidate['id']
    summary = directory / 'summary.json'
    if not summary.exists():
        # Preserve interrupted games; never silently mix their seed sequences.
        if directory.exists():
            directory.rename(directory.with_name(directory.name + '.partial-' + str(time.time_ns())))
        directory.parent.mkdir(parents=True, exist_ok=True)
        command = [sys.executable, '-u', str(ROOT / 'ocean/pokemon/benchmarks/evaluate_private.py'),
                   str(checkpoint), '--opponent', 'TaurosV0', '--mode', 'argmax',
                   '--device', 'cuda', '--threads', '1', '--games', str(games),
                   '--seed', str(seed), '--output', str(directory)]
        print(f'{stage}: {candidate["policy"]} {candidate["id"]}, {games * 3} games', flush=True)
        with (directory.parent / (candidate['id'] + '.console.log')).open('a') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=43200)
    report = json.loads(summary.read_text())
    assert report['sha256'] == candidate['sha256']
    assert report['config_sha256'] == candidate['config_sha256']
    assert report['games'] == games * 3 and report['seed'] == seed
    assert report['opponent'] == 'TaurosV0' and report['mode'] == 'argmax'
    return {**candidate, **{k: report[k] for k in
            ('score', 'wins', 'draws', 'losses', 'games', 'timeouts')}, 'report': str(summary)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--league', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plan-only', action='store_true')
    parser.add_argument('--screen-games', type=int, default=8)
    parser.add_argument('--shortlist-games', type=int, default=16)
    parser.add_argument('--final-games', type=int, default=32)
    parser.add_argument('--await-summary', type=Path)
    parser.add_argument('--await-pid', type=int)
    args = parser.parse_args()
    for games in (args.screen_games, args.shortlist_games, args.final_games):
        if games < 2 or games % 2:
            parser.error('Each stage game count must be positive and even (per opponent team)')
    league = args.league.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    protocol = dict(screen_games=args.screen_games, shortlist_games=args.shortlist_games,
                    final_games=args.final_games, seeds=[20261021, 20261022, 20261023])
    protocol_path = output / 'protocol.json'
    if protocol_path.exists() and json.loads(protocol_path.read_text()) != protocol:
        raise ValueError('Stage counts changed; use a new output directory to avoid mixing protocols')
    if not protocol_path.exists():
        save(protocol_path, protocol)
    manifest_path = output / 'manifest.json'
    if not manifest_path.exists():
        save(manifest_path, discover(league))
    manifest = json.loads(manifest_path.read_text())
    candidates = manifest['candidates']
    teams = {tuple(c['team_key']) for c in candidates}
    print(f'Frozen pool: {len(candidates)} candidates, {len(teams)} teams. '
          f'{args.screen_games * 3}-game screen -> {args.shortlist_games * 3}-game shortlist '
          f'-> {args.final_games * 3}-game final comparison.', flush=True)
    if args.plan_only:
        return
    if args.await_summary:
        while not args.await_summary.exists():
            if not args.await_pid:
                raise ValueError('Waiting requires the current evaluation PID')
            try:
                os.kill(args.await_pid, 0)
            except ProcessLookupError:
                raise RuntimeError('Prior evaluation stopped without a summary')
            print('Waiting for current master evaluation to finish.', flush=True)
            time.sleep(30)
    os.environ.setdefault('METAMON_CACHE_DIR', str(ROOT / 'build/pokemon/metamon-cache'))
    os.environ.setdefault('TORCH_COMPILE_DISABLE', '1')
    rows = candidates
    for stage, games, seed, per_team, global_count in [
            ('screen', args.screen_games, 20261021, 3, 12),
            ('shortlist', args.shortlist_games, 20261022, 1, 3),
            ('final', args.final_games, 20261023, 1, 3)]:
        results = []
        errors = []
        for candidate in rows:
            try:
                result = evaluate(candidate, stage, games, seed, output)
                results.append(result)
                print(f'{stage}: {len(results)}/{len(rows)} complete; '
                      f'{result["policy"]} {result["score"]:.2%}', flush=True)
            except (OSError, ValueError, AssertionError, subprocess.SubprocessError) as exc:
                errors.append(dict(id=candidate['id'], error=str(exc)))
                print(f'ERROR {candidate["id"]}: {exc}', flush=True)
            save(output / (stage + '-ranking.json'), dict(
                stage=stage, planned=len(rows), completed=len(results), errors=errors,
                finished=len(results) + len(errors) == len(rows),
                rows=sorted(results, key=lambda r: -r['score'])))
            if len(errors) >= 3:
                raise RuntimeError('Three evaluation failures; inspect saved logs before resuming')
        if errors:
            raise RuntimeError('Stage incomplete; resume failed candidates before selecting winners')
        rows = shortlist(results, per_team, global_count)
    print('Confirmation complete. Final ranking is final-ranking.json. '
          'Small screening samples can miss strong candidates; this is best measured, not proof of optimality.',
          flush=True)


if __name__ == '__main__':
    main()
