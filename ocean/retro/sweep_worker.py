#!/usr/bin/env python3
"""Bound native Retro trials; the trainer's fixed ROM panel supplies the score."""
import configparser
import csv
import json
import math
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
RESULT = struct.Struct('=fff i 192f')
STOP = False


def progress(path):
    if not path.exists():
        return []
    with path.open() as stream:
        rows = list(csv.DictReader(stream))
    return [row for row in rows if None not in row and all(v is not None for v in row.values())]


def measured_speed(rows, seconds):
    """Measure aggregate completed decisions, excluding the initial warmup."""
    valid = [r for r in rows if float(r['seconds']) >= 10]
    if len(valid) < 2:
        return None
    first, last = valid[0], valid[-1]
    elapsed = float(last['seconds'])-float(first['seconds'])
    return (int(last['steps'])-int(first['steps']))/elapsed if elapsed >= seconds else None


def panel_summary(path):
    summary = {}
    for line in path.read_text().splitlines():
        if line.startswith('# ') and not line.startswith('# game_attempt='):
            for part in line[2:].split():
                if '=' in part:
                    key, value = part.split('=', 1)
                    try:
                        summary[key] = float(value)
                    except ValueError:
                        summary[key] = value
    if not math.isfinite(float(summary.get('score', float('nan')))):
        raise RuntimeError('fixed ROM panel has no finite score')
    if float(summary.get('attempts', 0)) <= 0:
        raise RuntimeError('fixed ROM panel has no attempts')
    return summary


def validate_parent(settings):
    path = settings.get('base.load_model_path', 'None')
    if not path or path == 'None':
        return
    checkpoint = ROOT / path
    if not checkpoint.is_file():
        raise RuntimeError(f'parent checkpoint does not exist: {checkpoint}')
    metadata = Path(str(checkpoint)+'.ini')
    if not metadata.is_file():
        raise RuntimeError(f'continuation requires parent architecture metadata: {metadata}')
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(metadata)
    for key in ('architecture', 'hidden_size', 'num_layers'):
        expected = config.get('policy', key, fallback='puffernet' if key == 'architecture' else None)
        actual = settings.get('policy.'+key, 'puffernet' if key == 'architecture' else None)
        if key != 'architecture' and expected is not None and actual is not None:
            expected, actual = float(expected), float(actual)
        if expected is None or actual != expected:
            raise RuntimeError(f'parent architecture mismatch: policy.{key} requires '
                               f'{expected}, candidate requested {actual}')


def panel_levels(settings):
    levels = settings.get('panel.levels', 'all')
    return 32 if levels == 'all' else len(levels.split(','))


def validated_panel_score(settings, panel, game):
    if panel['attempts'] != panel_levels(settings)*int(float(settings['panel.repeats'])):
        raise RuntimeError(f"incomplete {'all-32-level' if panel_levels(settings) == 32 else 'level-subset'} ROM panel")
    score = panel['score']
    if settings.get('sweep.metric') == 'curriculum':
        if not game or game['attempts'] != int(float(settings['panel.game_repeats'])) or game.get('game') != 1 or game.get('archive_enabled') != 0:
            raise RuntimeError('incomplete or contaminated full-game panel')
        expected = 100*game['clears']/game['attempts']+game['mean_levels']
        if not 0 <= game['clears'] <= game['attempts'] or not 0 <= game['mean_levels'] <= 32 or not math.isclose(game['score'], expected, rel_tol=1e-6, abs_tol=1e-6):
            raise RuntimeError('invalid full-game score')
        score += game['score']
    return score


def retain_archives(logs, latest):
    """Keep the best and latest completed trial archives in this sweep root."""
    trials = []
    for path in logs.glob('*.eval/trial.json'):
        data = json.loads(path.read_text())
        trials.append(data)
    if not trials:
        return
    best = max(trials, key=lambda t: t['score'])['checkpoint']
    root = latest.parent.parent.resolve()
    for trial in trials:
        checkpoint = Path(trial['checkpoint'])
        # Only generated sidecars belonging to this sweep; preserve all weights.
        if checkpoint.resolve().parent.parent != root or str(checkpoint) in (best, str(latest)):
            continue
        Path(str(checkpoint)+'.archive.gz').unlink(missing_ok=True)


def train_worker(settings):
    validate_parent(settings)
    # This worker detects post-training evaluation from the final checkpoint.
    # Ordinary training may save periodically; sweep children save only at exit.
    settings = {**settings, 'base.checkpoint_interval': '0'}
    destination = int(settings['base.result_fd'])
    run = settings['base.run_id']
    logs = ROOT / settings['base.log_dir'] / 'retro'
    logs.mkdir(parents=True, exist_ok=True)
    output = logs / f'{run}.eval'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'settings.json').write_text(json.dumps(settings, indent=2)+'\n')
    checkpoint_dir = ROOT / settings['base.checkpoint_dir'] / 'retro' / run
    read_fd, write_fd = os.pipe()
    child_settings = {**settings, 'base.result_fd': str(write_fd)}
    trainer = ROOT / settings['sweep.trainer_path']
    command = [str(trainer), 'train']+[f'--{k}={v}' for k, v in child_settings.items()]
    started = time.monotonic()
    stop_time = None
    reason = 'completed'
    speed_checked = False
    last_status = started
    rows = []
    with (logs / f'{run}.stderr.log').open('w') as errors:
        child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=errors,
                                 pass_fds=(write_fd,), start_new_session=True)
        os.close(write_fd)
        try:
            while child.poll() is None:
                elapsed = time.monotonic()-started
                rows = progress(logs / f'{run}.progress.csv')
                last = rows[-1] if rows else None
                # Native post-training evaluation writes its report after stopping
                # training. Apply a separate timeout while that panel is running.
                checkpoints = sorted(checkpoint_dir.glob('[0-9]'*16+'.bin'))
                evaluating = bool(checkpoints)
                if last and time.monotonic()-last_status >= 60:
                    print(f'[Retro sweep] {run}: {int(last["steps"]):,}/'
                          f'{int(float(settings["train.total_timesteps"])):,} decisions; '
                          f'{float(last["sps"]):,.0f} decisions/s'
                          + ('; fixed ROM evaluation' if evaluating else ''),
                          file=sys.stderr, flush=True)
                    last_status = time.monotonic()
                why = None
                if STOP:
                    why = 'interrupted'
                elif not last and elapsed > float(settings['sweep.startup_timeout']):
                    raise TimeoutError('startup timeout')
                elif not evaluating and elapsed > float(settings['sweep.max_trial_seconds']):
                    why = 'runtime limit'
                if last and float(last['vram_used_gb'])*1024 > float(settings['sweep.memory_limit_mb']):
                    why = 'memory limit'
                if not speed_checked and not evaluating:
                    speed = measured_speed(rows, float(settings['sweep.speed_check_seconds']))
                    if speed is not None:
                        speed_checked = True
                        print(f'[Retro sweep] {run}: measured {speed:,.0f} decisions/s',
                              file=sys.stderr, flush=True)
                        if speed < float(settings['sweep.min_steps_per_second']):
                            why = 'slow'
                if why and stop_time is None:
                    reason, stop_time = why, time.monotonic()
                    os.kill(child.pid, signal.SIGUSR1)
                    print(f'[Retro sweep] {run}: stopping ({why}); saving checkpoint',
                          file=sys.stderr, flush=True)
                if evaluating:
                    age = time.time()-checkpoints[-1].stat().st_mtime
                    if age > float(settings['sweep.eval_timeout']):
                        raise TimeoutError('fixed ROM evaluation timeout')
                elif stop_time and time.monotonic()-stop_time > 90:
                    raise TimeoutError('trainer did not save after stop request')
                time.sleep(2)
            returncode = child.wait()
            data = os.read(read_fd, RESULT.size)
            if returncode or len(data) != RESULT.size:
                raise RuntimeError(f'trainer exited {returncode}; see {errors.name}')
            native = RESULT.unpack(data)
            if native[3] != 1 or not math.isfinite(native[0]) or not math.isclose(
                    native[4], native[0], rel_tol=1e-6, abs_tol=1e-6):
                raise RuntimeError('native result must contain one matching fixed-panel score')
            if reason in ('slow', 'memory limit'):
                raise RuntimeError(f'rejected trial: {reason}')
            minimum = float(settings['sweep.train.total_timesteps.min'])
            if native[2] < .98*minimum:
                raise RuntimeError('stopped before minimum training budget')
            checkpoint = max(checkpoint_dir.glob('[0-9]'*16+'.bin'))
            panel = panel_summary(Path(str(checkpoint)+'.panel.tsv'))
            game = None
            if settings.get('sweep.metric') == 'curriculum':
                game = panel_summary(Path(str(checkpoint)+'.game.tsv'))
            expected = panel_levels(settings)*int(float(settings['panel.repeats']))
            expected_score = validated_panel_score(settings, panel, game)
            if not math.isclose(expected_score, native[0], rel_tol=1e-5, abs_tol=1e-5):
                raise RuntimeError('native result disagrees with the all-32-level ROM panel and game panel')
            summary = {'run': run, 'reason': reason, 'score': native[0], 'steps': native[2],
                       'requested_steps': int(float(settings['train.total_timesteps'])),
                       'cost': time.monotonic()-started, 'checkpoint': str(checkpoint),
                       'panel': panel, 'game_panel': game, 'settings': settings}
            (output / 'trial.json').write_text(json.dumps(summary, indent=2)+'\n')
            retain_archives(logs, checkpoint)
            print(f'[Retro sweep] {run}: score={native[0]:.4f}; '
                  f'clears={int(panel.get("clears", 0))}/{expected}; '
                  f'{int(native[2]):,} decisions ({reason})', file=sys.stderr, flush=True)
            # Native uptime excludes initialization/graph capture. Feed actual
            # wall cost to both the result header and GP's single curve point.
            # Preserve the independently evaluated score and binary pipe ABI.
            values = list(native)
            values[1] = values[4+64] = summary['cost']
            data = RESULT.pack(*values)
            try:
                os.write(destination, data)
            except BrokenPipeError:
                if not STOP:
                    raise
        finally:
            os.close(read_fd)
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()


def main():
    if len(sys.argv) < 3 or sys.argv[1] != 'train':
        raise RuntimeError('launch this worker through the native sweep')
    def stop(signum, frame):
        global STOP
        STOP = True
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    settings = dict(arg[2:].split('=', 1) for arg in sys.argv[2:] if arg.startswith('--'))
    train_worker(settings)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'[Retro sweep] {error}', file=sys.stderr, flush=True)
        sys.exit(1)
