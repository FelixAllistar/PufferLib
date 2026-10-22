#!/usr/bin/env python3
"""Evaluate an immutable, explicit roster through the standard Oak evaluator."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def checked_summary(path, member, protocol):
    summary = json.loads(path.read_text())
    assert summary['sha256'] == member['sha256'], 'Checkpoint hash mismatch'
    assert summary['seed'] == protocol['seed'], 'Evaluation seed mismatch'
    assert summary['budget'] == protocol['budget'], 'Search budget mismatch'
    for mode in ('argmax', 'sample'):
        counts = summary['scores'][mode]
        assert counts['games'] == 3 * protocol['games']
        assert sum(counts[k] for k in ('wins', 'draws', 'losses')) == counts['games']
        score = (counts['wins'] + .5 * counts['draws']) / counts['games']
        assert abs(counts['score'] - score) < 1e-10
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    protocol, members = manifest['protocol'], manifest['members']
    assert protocol['games'] >= 2 and protocol['games'] % 2 == 0
    assert protocol['workers'] >= 1 and protocol['budget'] >= 1
    assert len({m['name'] for m in members}) == len(members)
    assert all(m['name'] and Path(m['name']).name == m['name'] and m['name'] not in ('.', '..') for m in members)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    saved_manifest = args.output / 'manifest.json'
    if saved_manifest.exists():
        assert json.loads(saved_manifest.read_text()) == manifest, 'Batch inputs changed'
    else:
        atomic_json(saved_manifest, manifest)
    for member in members:
        assert digest(member['checkpoint']) == member['sha256'], member['name']
        assert digest(Path(member['checkpoint']).with_name('config.ini')) == member['config_sha256'], member['name']
    started = time.monotonic()
    rows = []

    def publish():
        atomic_json(args.output / 'summary.json', {
            'protocol': protocol, 'snapshot': manifest.get('snapshot'),
            'planned': len(members), 'completed': sum(r['status'] == 'complete' for r in rows),
            'failed': sum(r['status'] == 'failed' for r in rows),
            'finished': len(rows) == len(members) and all(r['status'] != 'running' for r in rows),
            'elapsed_seconds': time.monotonic() - started, 'rows': rows})
        with (args.output / 'results.csv').open('w', newline='') as stream:
            fields = ('name', 'version', 'status', 'argmax', 'sample', 'checkpoint', 'error')
            writer = csv.DictWriter(stream, fieldnames=fields, extrasaction='ignore')
            writer.writeheader()
            writer.writerows(rows)

    publish()
    for member in members:
        name = member['name']
        row = {**member, 'status': 'running'}
        rows.append(row)
        publish()
        output = args.output / name
        summary_path = Path(member['existing_summary']) if member.get('existing_summary') else output / 'summary.json'
        if not member.get('existing_summary') and not summary_path.exists():
            cached = sorted(args.output.glob(f'{name}-attempt*/summary.json'))
            if cached:
                summary_path = cached[-1]
        print(f"Evaluating {name} ({member.get('version', '')})", flush=True)
        try:
            if not summary_path.exists():
                # Existing directories may be incomplete after interruption.
                # Keep them as evidence; resume into a new attempt directory.
                attempt = 1
                while output.exists():
                    attempt += 1
                    output = args.output / f'{name}-attempt{attempt}'
                summary_path = output / 'summary.json'
                command = [sys.executable, str(Path(__file__).with_name('evaluate_checkpoint.py')),
                           member['checkpoint'], '--output', str(output),
                           '--games', str(protocol['games']), '--budget', str(protocol['budget']),
                           '--workers', str(protocol['workers']), '--seed', str(protocol['seed'])]
                with (args.output / f'{name}-attempt{attempt}.log').open('w') as stream:
                    subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                   check=True, timeout=14400)
            summary = checked_summary(summary_path, member, protocol)
            row.update(status='complete', argmax=summary['scores']['argmax']['score'],
                       sample=summary['scores']['sample']['score'], scores=summary['scores'],
                       report=str(summary_path))
            print(f"{name}: argmax={row['argmax']:.4%} sampled={row['sample']:.4%}", flush=True)
        except Exception as error:
            row.update(status='failed', error=str(error))
            print(f'{name}: FAILED {error}', flush=True)
        publish()
    if any(row['status'] == 'failed' for row in rows):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
