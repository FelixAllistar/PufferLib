#!/usr/bin/env python3
"""Read-only terminal view of a Metamon sweep. Never controls worker processes."""
import argparse
import json
import math
from pathlib import Path
import re
import sys
import time


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return None


def tail(path, limit=65536):
    try:
        with path.open('rb') as stream:
            stream.seek(max(0, stream.seek(0, 2)-limit))
            return stream.read().decode(errors='replace')
    except OSError:
        return ''


def compact(value):
    for size, unit in ((1e9, 'B'), (1e6, 'M'), (1e3, 'K')):
        if value >= size:
            return f'{value/size:.3g}{unit}'
    return f'{value:.0f}'


def parse_progress(text):
    result = {}
    for label, key in (('Steps', 'steps'), ('SPS', 'sps')):
        matches = re.findall(r'│\s+'+label+r'\s+([\d.]+)([KMBG]?)\b', text)
        if matches:
            number, suffix = matches[-1]
            result[key] = float(number)*{'':1, 'K':1e3, 'M':1e6, 'B':1e9, 'G':1e9}[suffix]
    for label, key in (('Uptime', 'elapsed'), ('To go', 'remaining')):
        matches = re.findall(r'│\s+'+label+r'\s+(\S.*?)(?=\s{2,}|│)', text)
        if matches:
            result[key] = matches[-1]
    return result


def processes():
    # /proc is read only: no signals, subprocesses, CUDA, checkpoint loading, or writes.
    found = []
    for path in Path('/proc').glob('[0-9]*/cmdline'):
        try:
            args = path.read_bytes().decode(errors='replace').split('\0')
        except OSError:
            continue
        if args and (Path(args[0]).name == 'puffer'
                     or any(arg.endswith('/evaluate_private.py') for arg in args)):
            found.append(args)
    return found


def completed_trials(directories):
    completed = []
    for directory in directories:
        trial = read_json(directory/'trial.json')
        report = read_json(directory/'games/summary.json')
        if not trial or not report:
            continue
        score = trial.get('score')
        if (isinstance(score, (float, int)) and math.isfinite(score) and 0 <= score <= 1
                and report.get('games', 0) > 0 and report.get('score') == score):
            completed.append((directory, trial, report))
    return completed


def trial_number(run):
    try:
        return int(run.rsplit('_', 1)[1])+1
    except ValueError:
        return '?'


def result_line(item):
    _, trial, report = item
    return (f'{trial["score"]:.2%} - trial {trial_number(trial["run"])} '
            f'({report["wins"]}W/{report["draws"]}D/{report["losses"]}L, '
            f'{report["games"]} games)')


def snapshot_results(directories, completed):
    results = []
    for directory in directories:
        status = read_json(directory/'snapshot_evaluations.json') or {}
        for result in status.get('results', []):
            score = result.get('score')
            if (result.get('status') == 'complete' and isinstance(score, (float, int))
                    and math.isfinite(score) and 0 <= score <= 1):
                results.append({**result, 'run':directory.name.removesuffix('.metamon')})
    for _, trial, report in completed:
        results.append({**report, 'run':trial['run'], 'fraction':1, 'steps':trial.get('steps', 0)})
    return results


def snapshot_line(result):
    return (f'{result["score"]:.2%} - trial {trial_number(result["run"])} '
            f'@ {result["fraction"]:.0%} ({result["wins"]}W/{result["draws"]}D/'
            f'{result["losses"]}L, {result["games"]} games)')


def render(experiment, process_list=None, now=None):
    now = time.time() if now is None else now
    process_list = processes() if process_list is None else process_list
    directories = sorted((experiment/'logs/pokemon').glob('sweep_*.metamon'))
    completed = completed_trials(directories)
    snapshots = snapshot_results(directories, completed)
    lines = ['POKEMON | 1024 x 2 | METAMON SWEEP', '',
             f'Best seen:        {snapshot_line(max(snapshots, key=lambda row:row["score"])) if snapshots else "Pending - no completed evaluations yet"}',
             f'Best completed:   {result_line(max(completed, key=lambda row:row[1]["score"])) if completed else "Pending - no completed evaluations yet"}',
             f'Latest completed: {result_line(completed[-1]) if completed else "--"}',
             f'Completed trials: {len(completed)}', '']
    if not directories:
        lines.append('Waiting for the first trial to create its logs.')
    else:
        directory = directories[-1]
        run = directory.name.removesuffix('.metamon')
        settings = read_json(directory/'settings.json') or {}
        snapshot_status = read_json(directory/'snapshot_evaluations.json')
        budget = float(settings.get('train.total_timesteps', 0))
        target = int(float(settings.get('sweep.max_runs', 500)))
        training = any(len(args) > 1 and args[1] == 'train'
                       and f'--base.run_id={run}' in args for args in process_list)
        evaluating = any(any(arg.endswith('/evaluate_private.py') for arg in args)
                         and any(run in arg for arg in args) for args in process_list)
        controller = any(len(args) > 1 and args[1] == 'sweep'
                         and any(str(experiment) in arg for arg in args) for args in process_list)
        done = any(item[0] == directory for item in completed)
        state = ('TRAINING' if training else 'EVALUATING' if evaluating else
                 'COMPLETE' if done and not controller else 'BETWEEN STAGES' if controller else 'NOT RUNNING')
        lines.append(f'Trial {trial_number(run)} / {target} | {state}'
                     + (' + SNAPSHOT EVALUATION' if training and evaluating else ''))
        progress = parse_progress(tail(directory/'train.log'))
        steps = progress.get('steps', 0)
        if budget:
            percent = min(100, 100*steps/budget)
            lines.append(f'Training: {compact(steps)} / {compact(budget)} steps ({percent:.1f}%)')
        if training:
            lines.append(f'Elapsed {progress.get("elapsed", "--")} | Remaining ~{progress.get("remaining", "--")} '
                         f'| {compact(progress.get("sps", 0))} steps/sec')
            if snapshot_status:
                results = [row for row in snapshot_status.get('results', []) if row.get('status') == 'complete']
                if results:
                    latest = results[-1]
                    lines.append(f'Latest snapshot: {snapshot_line({**latest, "run":run})}')
                    lines.append('This trial: '+' | '.join(f'{row["fraction"]:.0%}: {row["score"]:.0%}' for row in results))
                else:
                    lines.append('Metamon: waiting for the first ~10% snapshot evaluation')
                active = snapshot_status.get('active')
                if active:
                    count = len(tail(Path(active['output'])/'games/games.jsonl').splitlines())
                    lines.append(f'Evaluating snapshot at {compact(active["steps"])} steps: '
                                 f'{count}/{snapshot_status["games_per_evaluation"]} games')
                failures = [row for row in snapshot_status.get('results', []) if row.get('status') == 'failed']
                if failures:
                    lines.append(f'WARNING: {len(failures)} snapshot evaluation failure(s); see snapshot_evaluations.json')
            else:
                lines.append('Metamon: final-only for this already-running trial; ~10% checks start next trial')
            try:
                age = now-(directory/'train.log').stat().st_mtime
                if age > 120:
                    lines.append(f'WARNING: training log has not updated for {int(age)} seconds.')
            except OSError:
                lines.append('Waiting for training log...')
        elif evaluating or (directory/'games/games.jsonl').exists() and not done:
            records = []
            for line in tail(directory/'games/games.jsonl', 131072).splitlines():
                try:
                    records.append(json.loads(line))
                except ValueError:
                    pass
            games = 3*int(float(settings.get('sweep.eval_games', 8)))
            lines.append(f'Metamon evaluation: {len(records)} / {games} games complete')
            if records:
                score = sum(row['score'] for row in records)/len(records)
                lines.append(f'Partial score: {score:.2%} (not ranked until evaluation completes)')
        elif not done and not controller:
            lines.append('No active trainer/evaluator/controller detected; check the raw log.')
        lines.extend(['', f'Current run: {run}'])
    lines.extend(['', 'Scores: Metamon wins + half draws; 24-game screen, noisy.',
                  'New trials: checks every ~10%; best seen includes intermediate snapshots.',
                  'Sweep ranking still uses final scores only. Snapshot peaks need confirmation.',
                  'Old H64 baseline: 47.17% / 300 games (different seed; reference only).',
                  '', 'Read-only display | Refresh 5s | Raw output: capacity1024-raw',
                  'Closing this display does not stop training.',
                  time.strftime('Updated %Y-%m-%d %H:%M:%S UTC', time.gmtime(now))])
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('experiment', type=Path)
    parser.add_argument('--once', action='store_true')
    args = parser.parse_args()
    experiment = args.experiment.resolve()
    try:
        while True:
            view = render(experiment)
            if not args.once and sys.stdout.isatty():
                print('\033[H\033[2J', end='')
            print(view, flush=True)
            if args.once:
                break
            time.sleep(5)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
