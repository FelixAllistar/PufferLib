#!/usr/bin/env python3
"""Mario worker for native PROTEIN: bounded training and a fixed ROM objective.

The native sweep owns proposals/history. This worker uses the single configured
trainer, preserves its binary pipe ABI, and returns independent fixed-test scores.
"""
import concurrent.futures
import configparser
import csv
import fcntl
import io
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
assert RESULT.size == 784
STOP = False
SCORE_VERSION = 2
FPG_DEPTHS = (3, 4, 5, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 320, 425)


def options(arguments):
    return dict(argument[2:].split('=', 1) for argument in arguments if argument.startswith('--'))


def config_for(settings, level=False):
    config = configparser.ConfigParser(interpolation=None)
    for key, value in settings.items():
        section, key = key.rsplit('.', 1)
        if section not in ('env', 'game', 'fpg', 'policy'):
            continue
        if not config.has_section(section):
            config.add_section(section)
        config[section][key] = value
    config['fpg']['max_frames'] = '6000' if level else '1800'
    text = io.StringIO()
    config.write(text)
    return text.getvalue()


def evaluation_panels(settings):
    attempts = int(float(settings['sweep.eval_level_episodes']))
    fpg = int(float(settings['sweep.eval_fpg_episodes']))
    panels = [(f'level_{stage//4+1}_{stage%4+1}', attempts,
               f'level:{stage//4+1}-{stage%4+1}', 0, 'clear', 0) for stage in range(32)]
    panels += [(f'fpg_{depth}', fpg, 'build/mario_sim/fpg/curriculum', depth, 'fpg', 0)
               for depth in FPG_DEPTHS]
    panels += [('new_game_1_1', attempts, 'new-game', 0, 'clear', 0)]
    # One continuing game from each of the original 32 natural stage starts.
    panels += [(f'game_{first:02d}', 4, 'all-stages', 0, 'game', first)
               for first in range(0, 32, 4)]
    return panels


def score_panels(panels):
    """Use fixed ROM outcomes, including bounded signals before the first clear."""
    def success_rate(names):
        selected = [panels[name] for name in names]
        attempts = sum(p['episodes_requested'] for p in selected)
        if attempts <= 0:
            raise ValueError('fixed evaluation has no attempts')
        rate = sum(p['rom_successes'] for p in selected) / attempts
        if not math.isfinite(rate) or not 0 <= rate <= 1:
            raise ValueError('invalid fixed evaluation success rate')
        return rate

    clear_rate = success_rate(f'level_{s//4+1}_{s%4+1}' for s in range(32))
    fpg_rate = sum(success_rate([f'fpg_{depth}']) for depth in FPG_DEPTHS) / len(FPG_DEPTHS)
    long_fpg_rate = sum(success_rate([f'fpg_{depth}']) for depth in (96, 256, 425)) / 3
    games = [panels[f'game_{first:02d}'] for first in range(0, 32, 4)]
    for panel in games:
        fraction = panel['mean_progress_fraction']
        if not math.isfinite(fraction) or not 0 <= fraction <= 1:
            raise ValueError('invalid bounded ROM progress')
    # sim2rom caps each episode at 3400 novel pixels before averaging. Its
    # frontiers persist across lives; revisits and pipe teleports earn no pixels.
    progress_rate = sum(p['mean_progress_fraction'] * p['episodes_requested'] for p in games) \
                    / sum(p['episodes_requested'] for p in games)
    return {'score_version': SCORE_VERSION,
            'score': .50*clear_rate + .25*progress_rate + .25*fpg_rate,
            'level_clear_rate': clear_rate, 'game_progress_rate': progress_rate,
            'fpg_clear_rate': fpg_rate, 'fpg_long_clear_rate': long_fpg_rate,
            'legacy_clear_only_score': .75*clear_rate + .25*long_fpg_rate,
            'objective': '0.50 * all-32-level clear rate + 0.25 * bounded continuing-game progress '
                         '+ 0.25 * mean FPG success over all 17 fixed depths'}


def valid_panel(summary, test, settings):
    name, episodes, selector, depth, objective, first = test
    expected_objective = {'clear': 'level_transition', 'game': 'full_game', 'fpg': 'fpg'}[objective]
    valid = (summary.get('passed') and summary.get('mismatches') == 0
             and summary.get('episodes_completed') == episodes
             and summary.get('episodes_requested') == episodes
             and summary.get('seed') == int(float(settings['sweep.eval_seed']))
             and summary.get('first_episode') == first
             and summary.get('reference_frames') == depth
             and summary.get('objective') == expected_objective
             and summary.get('max_frames') == (6000 if objective == 'clear' else
                                              108000 if objective == 'game' else 1800))
    if name.startswith('level_'):
        world, level = map(int, name.split('_')[1:])
        valid = valid and summary.get('starting_stage') == (world-1)*4+level-1
    return bool(valid)


def fixed_evaluation(checkpoint, settings, output):
    output.mkdir(parents=True, exist_ok=True)
    result_path = output / 'score.json'
    cached = {}
    previous = None
    if result_path.exists():
        previous = json.loads(result_path.read_text())
        if (previous.get('checkpoint') == str(checkpoint)
                and previous.get('seed') == int(float(settings['sweep.eval_seed']))
                and (output / 'evaluation.ini').exists() and (output / 'levels.ini').exists()
                and (output / 'evaluation.ini').read_text() == config_for(settings)
                and (output / 'levels.ini').read_text() == config_for(settings, level=True)):
            cached = previous.get('panels', {})
            if (previous.get('score_version') == SCORE_VERSION
                    and all(valid_panel(cached.get(test[0], {}), test, settings)
                            for test in evaluation_panels(settings))):
                return previous
    (output / 'evaluation.ini').write_text(config_for(settings))
    (output / 'levels.ini').write_text(config_for(settings, level=True))
    binary = ROOT / 'build/mario_sim/fpg/sim2rom'
    seed = int(float(settings['sweep.eval_seed']))
    started = time.monotonic()
    deadline = started + float(settings['sweep.eval_timeout'])

    def panel(test):
        name, episodes, selector, depth, objective, first = test
        if name in cached and valid_panel(cached[name], test, settings):
            return name, cached[name]
        directory = output / name
        config = output / ('levels.ini' if objective == 'clear' else 'evaluation.ini')
        command = [str(binary), str(checkpoint), str(config), str(directory),
                   str(episodes), str(seed), '0', selector, str(depth), objective, str(first)]
        remaining = deadline-time.monotonic()
        if remaining <= 0:
            raise TimeoutError('fixed evaluation exceeded its overall runtime limit')
        with (output / f'{name}.log').open('w') as log:
            subprocess.run(command, cwd=ROOT, env={**os.environ, 'OMP_NUM_THREADS': '1'},
                           stdout=log, stderr=subprocess.STDOUT, check=True,
                           timeout=min(remaining, float(settings['sweep.eval_panel_timeout'])))
        summary = json.loads((directory / 'summary.json').read_text())
        if not valid_panel(summary, test, settings):
            raise RuntimeError(f'invalid ROM evaluation: {name}')
        # Compact summaries and episode outcomes suffice for sweep diagnostics.
        # Keep all failure fixtures; discard only this worker's successful traces.
        (directory / 'trajectories.csv').unlink(missing_ok=True)
        return name, summary

    with concurrent.futures.ThreadPoolExecutor(max_workers=int(float(settings['sweep.eval_workers']))) as pool:
        panels = dict(pool.map(panel, evaluation_panels(settings)))
    additional_seconds = time.monotonic()-started
    result = {**score_panels(panels), 'seconds': additional_seconds,
              'additional_seconds': additional_seconds,
              'checkpoint': str(checkpoint), 'seed': seed, 'panels': panels,
              'reused_panels': sum(valid_panel(cached.get(test[0], {}), test, settings)
                                   for test in evaluation_panels(settings))}
    if result['reused_panels']:
        result['seconds'] += previous['seconds']
    temporary = result_path.with_suffix('.tmp')
    temporary.write_text(json.dumps(result, indent=2)+'\n')
    temporary.replace(result_path)
    with (output / 'per_level.csv').open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow(('level', 'clears', 'attempts', 'deaths', 'timeouts'))
        for stage in range(32):
            summary = panels[f'level_{stage//4+1}_{stage%4+1}']
            writer.writerow((f'{stage//4+1}-{stage%4+1}', summary['rom_successes'],
                             summary['episodes_requested'], summary['deaths'], summary['timeouts']))
    return result


def record_rung(path, run, rung, result, settings):
    """Compare the same budget AND actor count, preserving curriculum exposure."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+') as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        stream.seek(0)
        prior = [json.loads(line) for line in stream if line.strip()]
        agents = int(float(settings['vec.total_agents']))
        scores = sorted(row['score'] for row in prior if row.get('score_version') == SCORE_VERSION
                        and row['rung'] == rung
                        and row.get('agents') == agents and row['run'] != run)
        minimum = int(float(settings['sweep.prune_min_trials']))
        threshold = None
        if len(scores) >= minimum:
            quantile = float(settings['sweep.prune_quantile'])
            threshold = scores[min(len(scores)-1, int(quantile*len(scores)))]
        if not any(row['run'] == run and row['rung'] == rung
                   and row.get('score_version') == SCORE_VERSION for row in prior):
            stream.seek(0, 2)
            stream.write(json.dumps({'run': run, 'rung': rung, 'agents': agents, 'score': result['score'],
                                     'score_version': SCORE_VERSION,
                                     'checkpoint': result['checkpoint']})+'\n')
            stream.flush()
            os.fsync(stream.fileno())
        margin = float(settings['sweep.prune_score_margin'])
        return threshold is not None and result['score'] + margin < threshold


def progress(path):
    if not path.exists():
        return []
    with path.open() as stream:
        rows = list(csv.DictReader(stream))
    # A line may be in flight when read. Never make a decision on a partial row.
    return [row for row in rows if None not in row and all(value is not None for value in row.values())]


def checkpoint_steps(path):
    return int(path.stem)


def prune_checkpoints(directory, keep):
    files = sorted(directory.glob('[0-9]'*16+'.bin'))
    retained = set(files[-2:]) | set(keep)
    for checkpoint in files:
        if checkpoint in retained:
            continue
        for path in (checkpoint, Path(str(checkpoint)+'.curriculum'),
                     Path(str(checkpoint)+'.curriculum.json'), Path(str(checkpoint)+'.practice')):
            path.unlink(missing_ok=True)


def train_worker(settings):
    destination = int(settings['base.result_fd'])
    run = settings['base.run_id']
    logs = ROOT / settings['base.log_dir'] / 'mario_sim'
    logs.mkdir(parents=True, exist_ok=True)
    output = logs / f'{run}.eval'
    output.mkdir(parents=True, exist_ok=True)
    checkpoint_dir = ROOT / settings['base.checkpoint_dir'] / 'mario_sim' / run
    csv_path = logs / f'{run}.progress.csv'
    budget = int(float(settings['train.total_timesteps']))
    (output / 'settings.json').write_text(json.dumps(settings, indent=2)+'\n')
    rollout = int(float(settings['vec.total_agents'])) * int(float(settings['train.horizon']))
    child_settings = dict(settings)
    child_settings['base.checkpoint_interval'] = str(max(1, math.ceil(100_000_000/rollout)))
    read_fd, write_fd = os.pipe()
    child_settings['base.result_fd'] = str(write_fd)
    trainer = Path(settings['sweep.trainer_path'])
    if not trainer.is_absolute():
        trainer = ROOT / trainer
    command = [str(trainer), 'train'] + [f'--{key}={value}' for key, value in child_settings.items()]
    started = time.monotonic()
    reason = 'completed'
    requested_stop = None
    speed_checked = False
    keep = set()
    pending = None
    rungs = [500_000_000, 1_000_000_000, 2_000_000_000]
    rungs = [rung for rung in rungs if budget >= 1.4*rung]
    rung_results = []
    baseline = None
    last_status = started
    global STOP

    def stop_child(child, why):
        nonlocal requested_stop, reason
        if requested_stop is None and child.poll() is None:
            reason = why
            requested_stop = time.monotonic()
            os.kill(child.pid, signal.SIGUSR1)
            print(f'[Mario sweep] {run}: stopping ({why}); saving checkpoint', file=sys.stderr, flush=True)

    with (logs / f'{run}.stderr.log').open('w') as errors, \
            concurrent.futures.ThreadPoolExecutor(max_workers=1) as evaluator:
        child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=errors,
                                 pass_fds=(write_fd,), start_new_session=True)
        os.close(write_fd)
        try:
            while child.poll() is None:
                elapsed = time.monotonic()-started
                rows = progress(csv_path)
                last = rows[-1] if rows else None
                if last and time.monotonic()-last_status >= 60:
                    print(f'[Mario sweep] {run}: {int(last["steps"]):,}/{budget:,} steps; '
                          f'{float(last["sps"]):,.0f} steps/s', file=sys.stderr, flush=True)
                    last_status = time.monotonic()
                if last and float(last['vram_used_gb'])*1024 > float(settings['sweep.memory_limit_mb']):
                    stop_child(child, 'memory limit')
                if last and not speed_checked:
                    seconds = float(last['seconds'])
                    steps = int(last['steps'])
                    if baseline is None and seconds >= 10 and steps >= 4*rollout:
                        baseline = (steps, seconds)
                    if baseline and seconds-baseline[1] >= float(settings['sweep.speed_check_seconds']) \
                            and steps-baseline[0] >= 4*rollout:
                        sps = (steps-baseline[0])/(seconds-baseline[1])
                        speed_checked = True
                        print(f'[Mario sweep] {run}: measured {sps:,.0f} steps/s', file=sys.stderr, flush=True)
                        if sps < float(settings['sweep.min_steps_per_second']):
                            stop_child(child, 'slow')
                if not last and elapsed > float(settings['sweep.startup_timeout']):
                    reason = 'startup timeout'
                    os.killpg(child.pid, signal.SIGKILL)
                    break
                if elapsed > float(settings['sweep.max_trial_seconds']):
                    stop_child(child, 'runtime limit')
                if STOP:
                    stop_child(child, 'interrupted')
                if requested_stop and time.monotonic()-requested_stop > 90:
                    os.killpg(child.pid, signal.SIGKILL)
                    break
                if pending and pending[2].done():
                    rung, checkpoint, future = pending
                    result = future.result()
                    rung_results.append({'rung': rung, 'score': result['score']})
                    history = ROOT / settings['sweep.rung_history_path']
                    if record_rung(history, run, rung, result, settings):
                        stop_child(child, f'poor fixed evaluation at {rung:,} steps')
                    pending = None
                if not pending and not requested_stop and rungs:
                    checkpoints = sorted(checkpoint_dir.glob('[0-9]'*16+'.bin'))
                    # Sidecars are saved after weights; require a stable finished save.
                    candidates = [path for path in checkpoints if checkpoint_steps(path) >= rungs[0]
                                  and time.time()-path.stat().st_mtime > 3
                                  and Path(str(path)+'.practice').exists()
                                  and Path(str(path)+'.curriculum').exists()]
                    if candidates:
                        rung = rungs.pop(0)
                        checkpoint = candidates[0]
                        keep.add(checkpoint)
                        pending = (rung, checkpoint, evaluator.submit(fixed_evaluation,
                                   checkpoint, settings, output / f'rung_{rung}'))
                if last:
                    prune_checkpoints(checkpoint_dir, keep)
                time.sleep(2)
            returncode = child.wait()
            data = os.read(read_fd, RESULT.size)
            if returncode or len(data) != RESULT.size:
                raise RuntimeError(f'trainer exited {returncode}, result bytes={len(data)}; see {errors.name}')
            native = RESULT.unpack(data)
            steps = native[2]
            if reason in ('slow', 'startup timeout', 'memory limit'):
                raise RuntimeError(f'rejected trial: {reason}')
            if steps < 0.98*float(settings['sweep.train.total_timesteps.min']):
                raise RuntimeError('interrupted before minimum training budget')
            if pending:
                rung, checkpoint, future = pending
                result = future.result()
                record_rung(ROOT / settings['sweep.rung_history_path'], run, rung, result, settings)
                rung_results.append({'rung': rung, 'score': result['score']})
            final = max(checkpoint_dir.glob('[0-9]'*16+'.bin'), key=checkpoint_steps)
            keep.add(final)
            print(f'[Mario sweep] {run}: training ended at {steps:,.0f}; running fixed ROM tests',
                  file=sys.stderr, flush=True)
            result = fixed_evaluation(final, settings, output / 'final')
            cost = time.monotonic()-started
            summary = {'run': run, 'reason': reason, 'steps': steps, 'requested_steps': budget,
                       'cost': cost, 'score': result['score'], 'score_version': SCORE_VERSION,
                       'rungs': rung_results,
                       'checkpoint': str(final), 'settings': settings}
            (output / 'trial.json').write_text(json.dumps(summary, indent=2)+'\n')
            with (logs / f'{run}.ini').open('a') as log:
                log.write(f'\nenv/fixed_eval_score = {result["score"]:.12g}\n')
            prune_checkpoints(checkpoint_dir, keep)
            print(f'[Mario sweep] {run}: score={result["score"]:.4f} '
                  f'levels={result["level_clear_rate"]:.1%} progress={result["game_progress_rate"]:.1%} '
                  f'FPG={result["fpg_clear_rate"]:.1%} '
                  f'steps={steps:,.0f} ({reason})', file=sys.stderr, flush=True)
            values = [0.0]*192
            values[0], values[64], values[128] = result['score'], cost, steps
            try:
                os.write(destination, RESULT.pack(result['score'], cost, steps, 1, *values))
            except BrokenPipeError:
                if not STOP:
                    raise
        finally:
            os.close(read_fd)
            if child.poll() is None:
                os.kill(child.pid, signal.SIGUSR1)
                try:
                    child.wait(timeout=90)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--eval':
        config, checkpoint, output = sys.argv[2:]
        ini = configparser.ConfigParser(interpolation=None)
        ini.read(config)
        settings = {f'{section}.{key}': value for section in ini.sections() for key, value in ini[section].items()}
        print(json.dumps(fixed_evaluation(Path(checkpoint).resolve(), settings, Path(output).resolve())))
        return
    if len(sys.argv) < 3 or sys.argv[1] != 'train':
        raise RuntimeError('worker must be launched by the native sweep')
    def stop(signum, frame):
        global STOP
        STOP = True
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    train_worker(options(sys.argv[2:]))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'[Mario sweep] {error}', file=sys.stderr, flush=True)
        sys.exit(1)
