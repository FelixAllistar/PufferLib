#!/usr/bin/env python3
"""Focused 20-policy comparison, preserving the earlier broad search results."""
import argparse
import csv
import io
import json
import os
from pathlib import Path

from search_metamon import ROOT, discover, evaluate, save


def build_manifest(league, broad):
    pool = discover(league)['candidates']
    by_path = {c['checkpoint']: c for c in pool}
    by_id = {c['id']: c for c in pool}
    oak = {}
    reports = list((league / 'evaluations').rglob('summary.json'))
    for c in pool:
        reports.extend(Path(s.removeprefix('sweep_report:')) for s in c['sources']
                       if s.startswith('sweep_report:'))
    for path in set(reports):
        report = json.loads(path.read_text())
        if 'scores' not in report or 'checkpoint' not in report:
            continue
        checkpoint = str(Path(report['checkpoint']).resolve())
        candidate = by_path.get(checkpoint)
        if candidate is None or report.get('sha256') != candidate['sha256']:
            continue
        row = dict(argmax=report['scores']['argmax']['score'],
                   sample=report['scores']['sample']['score'],
                   games=report['scores']['argmax']['games'], report=str(path))
        oak.setdefault(candidate['id'], []).append(row)
    picked = {}

    def add(c, reason):
        if c['id'] not in picked:
            picked[c['id']] = {**c, 'selection_reasons': [],
                                'oak_reports': oak.get(c['id'], [])}
        picked[c['id']]['selection_reasons'].append(reason)

    sweep = [c for c in pool if any(s.startswith('sweep_report:') for s in c['sources'])]
    sweep.sort(key=lambda c: max(r['argmax'] for r in oak[c['id']]), reverse=True)
    for candidate in sweep[:5]:
        add(candidate, 'Top five recorded Oak argmax sweep policies at selection time')
    baseline = league / 'members/master/r000026-a001/weights.bin'
    add(by_path[str(baseline)], 'Round-26 incumbent, previously 47.66% Metamon in 384 games')
    roster = json.loads((league / 'evaluations/roster-round13-128/summary.json').read_text())
    for row in roster['rows']:
        if row['policy'] != 'master' and row['status'] == 'complete':
            add(by_path[row['checkpoint']], 'Previously Oak-measured representative for this team')
    screening = json.loads((broad / 'screen-ranking.json').read_text())
    for row in sorted(screening['rows'], key=lambda r: -r['score']):
        if row['id'] not in picked:
            add(by_id[row['id']], 'Highest completed 24-game Metamon screen outside selected candidates')
            picked[row['id']]['prior_metamon_screen'] = {
                k: row[k] for k in ('score', 'games', 'report')}
            break
    assert len(picked) == 20, 'Expected five sweeps, incumbent, thirteen team representatives and screen leader'
    assert len({tuple(c['team_key']) for c in picked.values()}) == 14
    # Compare the already-promising external-bot candidate immediately after
    # the Oak leaders and incumbent, before the remaining team representatives.
    candidates = list(picked.values())
    candidates = candidates[:6] + candidates[-1:] + candidates[6:-1]
    return dict(selection='Focused comparison: top five Oak sweep checkpoints, '
                'round-26 incumbent, thirteen previously measured team representatives, '
                'and the leading broad Metamon screen candidate',
                games_per_opponent_team=32, total_games_per_policy=96, seed=20261023,
                candidates=candidates,
                limitations='Oak and Metamon scores are different benchmarks; Oak sample counts vary. '
                '96-game Metamon comparisons cannot resolve small differences reliably.')


def comparison_csv(rows):
    stream = io.StringIO()
    writer = csv.writer(stream)
    writer.writerow(['policy', 'checkpoint', 'oak_argmax_percent', 'oak_sample_percent',
                     'oak_games_per_mode', 'metamon_percent', 'metamon_games', 'wins', 'draws', 'losses'])
    for row in rows:
        oak = max(row['oak_reports'], key=lambda r: r['argmax']) if row['oak_reports'] else None
        writer.writerow([row['policy'], row['checkpoint'],
                         round(100 * oak['argmax'], 3) if oak else '',
                         round(100 * oak['sample'], 3) if oak else '',
                         oak['games'] if oak else '', round(100 * row['score'], 3),
                         row['games'], row['wins'], row['draws'], row['losses']])
    return stream.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--league', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plan-only', action='store_true')
    args = parser.parse_args()
    league = args.league.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / 'manifest.json'
    if not manifest_path.exists():
        save(manifest_path, build_manifest(league, league / 'evaluations/metamon-policy-search'))
    manifest = json.loads(manifest_path.read_text())
    candidates = manifest['candidates']
    print(f'{len(candidates)} policies; 96 Metamon games each; Oak leaders first.', flush=True)
    if args.plan_only:
        return
    os.environ.setdefault('METAMON_CACHE_DIR', str(ROOT / 'build/pokemon/metamon-cache'))
    os.environ.setdefault('TORCH_COMPILE_DISABLE', '1')
    rows = []
    for candidate in candidates:
        result = evaluate(candidate, 'comparison', 32, manifest['seed'], output)
        rows.append(result)
        ranking = sorted(rows, key=lambda r: -r['score'])
        save(output / 'ranking.json', dict(completed=len(rows), planned=len(candidates),
             finished=len(rows) == len(candidates), rows=ranking))
        (output / 'comparison.csv').write_text(comparison_csv(ranking))
        print(f'{len(rows)}/{len(candidates)} complete: {result["policy"]} '
              f'{result["score"]:.2%} ({result["wins"]}W/{result["draws"]}D/'
              f'{result["losses"]}L)', flush=True)


if __name__ == '__main__':
    main()
