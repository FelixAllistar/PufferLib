#!/usr/bin/env python3
"""Wait for the focused comparison, then test leaders and baseline two at a time."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import time

from compare_metamon_leaders import comparison_csv
from search_metamon import ROOT, digest, evaluate, save


def select(rows):
    ordered = sorted(rows, key=lambda row: (-row['score'], row['id']))
    selected = {row['id']: row for row in ordered[:3]}
    baseline = [r for r in rows if '/members/master/r000026-a001/' in r['checkpoint']]
    if len(baseline) != 1:
        raise ValueError('Expected exactly one round-26 baseline in completed comparison')
    selected[baseline[0]['id']] = baseline[0]
    return list(selected.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--comparison', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--await-pid', type=int, required=True)
    args = parser.parse_args()
    source = args.comparison.resolve() / 'ranking.json'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / 'manifest.json'
    if not manifest_path.exists():
        while True:
            report = json.loads(source.read_text()) if source.exists() else {}
            if report.get('finished') and report.get('completed') == report.get('planned'):
                break
            try:
                os.kill(args.await_pid, 0)
            except ProcessLookupError:
                # Account for completion between reading ranking and polling PID.
                report = json.loads(source.read_text()) if source.exists() else {}
                if report.get('finished') and report.get('completed') == report.get('planned'):
                    break
                raise RuntimeError('Comparison exited before completing; inspect its log')
            print(f'Waiting for focused comparison: {report.get("completed", 0)}/'
                  f'{report.get("planned", 20)} finished.', flush=True)
            time.sleep(30)
        save(manifest_path, dict(source=str(source), source_sha256=digest(source),
             selection='Top three by 96-game Metamon score plus round-26 baseline, deduplicated',
             games_per_opponent_team=100, games_per_policy=300, seed=20261024,
             concurrent_evaluators=2, candidates=select(report['rows'])))
    manifest = json.loads(manifest_path.read_text())
    os.environ.setdefault('METAMON_CACHE_DIR', str(ROOT / 'build/pokemon/metamon-cache'))
    os.environ.setdefault('TORCH_COMPILE_DISABLE', '1')
    started = time.monotonic()
    results = []
    errors = []
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = {pool.submit(evaluate, row, 'confirmation', 100, manifest['seed'], output): row
                for row in manifest['candidates']}
        for job in as_completed(jobs):
            candidate = jobs[job]
            try:
                result = job.result()
                results.append(result)
                print(f'{candidate["policy"]} {candidate["id"]}: '
                      f'{result["score"]:.2%} ({result["wins"]}W/{result["draws"]}D/'
                      f'{result["losses"]}L), {len(results)}/{len(jobs)} complete.', flush=True)
            except Exception as exc:
                errors.append(dict(id=candidate['id'], error=str(exc)))
                print(f'ERROR {candidate["id"]}: {exc}', flush=True)
            ranking = sorted(results, key=lambda row: -row['score'])
            elapsed = time.monotonic() - started
            save(output / 'ranking.json', dict(completed=len(results), planned=len(jobs),
                 finished=len(results) == len(jobs), errors=errors, elapsed_seconds=elapsed,
                 rows=ranking))
            (output / 'comparison.csv').write_text(comparison_csv(ranking))
    if errors:
        raise RuntimeError('Confirmation incomplete; failed evaluations retained for inspection')
    print('Follow-up complete. Compare fresh-seed baseline and leaders in ranking.json; '
          'small score differences remain uncertain.', flush=True)


if __name__ == '__main__':
    main()
