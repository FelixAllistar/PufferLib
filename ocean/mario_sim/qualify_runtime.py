#!/usr/bin/env python3
"""Replay qualified ROM traces through both optimized backends and the adapter."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest.hexdigest()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime', type=Path, default=Path('build/mario_sim/runtime'))
    parser.add_argument('--reference', type=Path, default=Path('build/mario_sim/qualification_v2'))
    parser.add_argument('--bank', type=Path, default=Path('build/mario_sim/runtime/generated'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    traces = [(name, args.reference / name / 'clips.bin')
              for name in ('main', 'supplement', 'world', 'boundaries')]
    generated = json.loads((args.bank / 'summary.json').read_text())
    traces += [(f'generated_world_{i}', args.bank / f'world_{i}' / 'clips.bin')
               for i in range(generated['worlds'])]
    traces.append(('teacher', args.bank / 'teacher' / 'clips.bin'))
    rows = []
    for backend in ('cpu', 'cuda'):
        for name, trace in traces:
            with (args.output / f'{backend}_{name}.log').open('w') as err:
                run = subprocess.run([str(args.runtime / f'verify_{backend}'), str(trace)],
                                     stdout=subprocess.PIPE, stderr=err, text=True, check=True)
            result = json.loads(run.stdout)
            if result['failures'] or result['frames'] < 1:
                raise RuntimeError(f'failed or empty {backend} {name}')
            (args.output / f'{backend}_{name}.json').write_text(run.stdout)
            rows.append({'backend': backend, 'panel': name, **result})
            print(backend, name, result['frames'], 'matched', flush=True)
    for name, command in (
        ('integration', [str(args.runtime / 'test_runtime'), str(args.runtime / 'cuda_replay.cubin'), str(args.bank)]),
        ('bank_guards', [str(args.runtime / 'test_bank'), str(args.bank / 'bank.bin'), str(args.output / 'bank_guards')]),
        ('trace_guards', ['python3', 'ocean/mario_sim/check_trace_guards.py', str(args.runtime / 'verify_cuda'),
                          str(traces[0][1]), str(args.output / 'trace_guards')]),
    ):
        with (args.output / f'{name}.log').open('w') as err:
            run = subprocess.run(command, stdout=subprocess.PIPE, stderr=err, text=True, check=True)
        (args.output / f'{name}.json').write_text(run.stdout)
    artifacts = [args.runtime / name for name in ('cuda_replay.cubin', 'logic_cpu.a', 'generation.json')]
    artifacts += [args.bank / 'bank.bin'] + [trace for _, trace in traces]
    (args.output / 'summary.json').write_text(json.dumps({
        'passed': True, 'panels': rows, 'artifacts': [fingerprint(path) for path in artifacts],
        'frames_per_backend': sum(row['frames'] for row in rows if row['backend'] == 'cpu'),
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
