#!/usr/bin/env python3
"""Native PROTEIN worker with ~10% snapshot monitoring and final Metamon ranking."""
import csv
import configparser
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
RESULT = struct.Struct('=fffi192f')
assert RESULT.size == 784
STOP = False
EVAL_FRACTION = .1


def checkpoint_schedule(settings):
    """Intervals are rollout epochs, not agent steps. Leave the budget unchanged."""
    rollout = int(float(settings['vec.total_agents'])) * int(float(settings['train.horizon']))
    epochs = int(float(settings['train.total_timesteps'])) // rollout
    if epochs < 1:
        raise ValueError('Training budget must include at least one rollout')
    interval = max(1, math.ceil(epochs * EVAL_FRACTION))
    return interval, epochs * rollout


def atomic_json(path, value):
    temporary = path.with_suffix(path.suffix+'.tmp')
    temporary.write_text(json.dumps(value, indent=2)+'\n')
    temporary.replace(path)


def evaluation_command(settings, checkpoint, output):
    return [settings['sweep.eval_python'], str(ROOT/'ocean/pokemon/benchmarks/evaluate_private.py'),
            str(checkpoint), '--opponent', 'TaurosV0', '--mode', 'argmax', '--device', 'cuda',
            '--threads', '1', '--games', str(int(float(settings['sweep.eval_games']))),
            '--seed', str(int(float(settings['sweep.eval_seed']))), '--output', str(output)]


def evaluation_env():
    return {**os.environ, 'TORCH_COMPILE_DISABLE':'1',
            'METAMON_CACHE_DIR':str(ROOT/'build/pokemon/metamon-cache'),
            'OMP_NUM_THREADS':'1', 'OPENBLAS_NUM_THREADS':'1'}


class SnapshotEvaluations:
    """One external evaluator at a time; failures never signal/stop the trainer."""
    def __init__(self, settings, checkpoint_dir, output, final_steps, interval):
        self.settings = settings
        self.checkpoint_dir = checkpoint_dir
        self.output = output
        self.final_steps = final_steps
        self.interval = interval
        self.pending = None
        self.disabled = False
        self.results = []
        self.attempted = set()
        self.save_status()

    def save_status(self):
        active = None
        if self.pending:
            active = {key:self.pending[key] for key in ('steps', 'checkpoint', 'output')}
        atomic_json(self.output/'snapshot_evaluations.json', dict(
            version=1, fraction=EVAL_FRACTION, checkpoint_interval_epochs=self.interval,
            final_steps=self.final_steps, games_per_evaluation=3*int(float(self.settings['sweep.eval_games'])),
            active=active, results=self.results,
            objective='Final checkpoint only; intermediate scores are monitoring, not early stopping'))

    def available(self):
        if self.disabled:
            return []
        result = []
        for checkpoint in sorted(self.checkpoint_dir.glob('[0-9]'*16+'.bin')):
            steps = int(checkpoint.stem)
            if steps <= 0 or steps >= self.final_steps or steps in self.attempted:
                continue
            sidecar = Path(str(checkpoint)+'.ini')
            # Weights publish via rename. Wait for the subsequent config write too.
            if sidecar.exists() and time.time()-sidecar.stat().st_mtime >= 1:
                result.append(checkpoint)
        return result

    def start(self, checkpoint):
        steps = int(checkpoint.stem)
        self.attempted.add(steps)
        directory = self.output/'snapshots'/checkpoint.stem
        input_dir = directory/'input'
        log = None
        try:
            directory.mkdir(parents=True, exist_ok=False)
            input_dir.mkdir()
            # Freeze the sidecar: parent config.ini is rewritten by later saves.
            config_text = Path(str(checkpoint)+'.ini').read_text()
            config = configparser.ConfigParser(interpolation=None)
            config.read_string(config_text)
            for section, key in [('policy','hidden_size'), ('policy','num_layers'),
                                 ('env','learner_team'), ('env','learner_lead')]:
                if not config.get(section, key):
                    raise ValueError('Incomplete snapshot config')
            (input_dir/'config.ini').write_text(config_text)
            frozen = input_dir/'weights.bin'
            os.link(checkpoint, frozen)  # Same immutable weights; no duplicate disk allocation.
            log = (directory/'eval.log').open('w')
            child = subprocess.Popen(evaluation_command(self.settings, frozen, directory/'games'),
                                     cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                     env=evaluation_env(), start_new_session=True)
            self.pending = dict(child=child, log=log, started=time.monotonic(), steps=steps,
                                checkpoint=str(checkpoint), frozen=frozen, output=str(directory))
            print(f'[Metamon snapshot] {steps:,}/{self.final_steps:,} '
                  f'({steps/self.final_steps:.0%}): evaluation started alongside training', flush=True)
        except Exception as error:
            if log:
                log.close()
            self.results.append(dict(steps=steps, checkpoint=str(checkpoint), status='failed', error=str(error)))
            print(f'[Metamon snapshot] {steps:,}: evaluation could not start: {error}', flush=True)
        self.save_status()

    @staticmethod
    def stop_evaluator(child):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()

    def poll(self):
        if not self.pending:
            return
        pending = self.pending
        child = pending['child']
        timed_out = time.monotonic()-pending['started'] > float(self.settings['sweep.eval_timeout'])
        if child.poll() is None and not timed_out:
            return
        if timed_out:
            self.stop_evaluator(child)
        code = child.wait()
        pending['log'].close()
        result = dict(steps=pending['steps'], checkpoint=pending['checkpoint'],
                      fraction=pending['steps']/self.final_steps,
                      cost=time.monotonic()-pending['started'])
        try:
            if code or timed_out:
                raise RuntimeError(f'Evaluator exit={code}, timeout={timed_out}')
            report_path = Path(pending['output'])/'games/summary.json'
            report = json.loads(report_path.read_text())
            score = validate(report, pending['frozen'], int(float(self.settings['sweep.eval_games'])),
                             int(float(self.settings['sweep.eval_seed'])))
            result.update(status='complete', score=score, wins=report['wins'], draws=report['draws'],
                          losses=report['losses'], games=report['games'], evaluation=str(report_path))
            print(f'[Metamon snapshot] {result["fraction"]:.0%}: {score:.2%} '
                  f'({report["wins"]}W/{report["draws"]}D/{report["losses"]}L)', flush=True)
        except Exception as error:
            result.update(status='failed', error=str(error))
            print(f'[Metamon snapshot] {pending["steps"]:,}: evaluation failed: {error}; training continues', flush=True)
        self.results.append(result)
        self.pending = None
        self.save_status()

    def update(self):
        if self.disabled:
            return
        try:
            self.poll()
            if not self.pending and not STOP:
                available = self.available()
                if available:
                    self.start(available[0])
        except Exception as error:
            # Monitoring must not take down a healthy training subprocess.
            self.disabled = True
            self.close()
            print(f'[Metamon snapshot] monitoring disabled: {error}; training continues', flush=True)
            try:
                self.results.append(dict(status='failed', error=f'Monitor disabled: {error}'))
                self.save_status()
            except OSError:
                pass

    def finish(self):
        # Trainer has exited; finish any remaining queued intermediate evaluations.
        while not STOP:
            self.update()
            if not self.pending and not self.available():
                return
            time.sleep(1)
        raise RuntimeError('Interrupted before evaluations completed')

    def close(self):
        if self.pending:
            self.stop_evaluator(self.pending['child'])
            self.pending['log'].close()
            self.pending = None

def packet(score, cost, steps):
    if not (math.isfinite(score) and 0 <= score <= 1 and cost > 0 and steps > 0):
        raise ValueError('Invalid completed trial')
    curve = [0.0]*192
    curve[0], curve[64], curve[128] = score, cost, steps
    return RESULT.pack(score, cost, steps, 1, *curve)

def validate(report, checkpoint, games, seed):
    if (report['sha256'] != hashlib.sha256(checkpoint.read_bytes()).hexdigest()
            or report['games'] != games*3 or report['games_per_team'] != games
            or report['seed'] != seed or report['opponent'] != 'TaurosV0'
            or report['mode'] != 'argmax' or report['protocol'] != 'native-private-v1'):
        raise ValueError('Metamon result does not match trial')
    expected = (report['wins'] + .5*report['draws']) / report['games']
    if report['wins'] + report['draws'] + report['losses'] != report['games']:
        raise ValueError('Incomplete evaluation')
    if not math.isclose(expected, report['score'], abs_tol=1e-12):
        raise ValueError('Invalid evaluation score')
    return expected

def main():
    if len(sys.argv) < 3 or sys.argv[1] != 'train':
        raise ValueError('Launch through native sweep, or provide train --section.key=value')
    settings = dict(arg[2:].split('=', 1) for arg in sys.argv[2:] if arg.startswith('--'))
    interval, final_steps = checkpoint_schedule(settings)
    # Only newly launched workers see this change. Never change a live trainer.
    settings['base.checkpoint_interval'] = str(interval)
    destination = int(settings['base.result_fd'])
    run = settings['base.run_id']
    logs = Path(settings['base.log_dir']).resolve() / 'pokemon'
    logs.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(logs).free < 2*1024**3:
        raise RuntimeError('Less than 2 GiB disk space remains; archive checkpoints before continuing')
    output = logs / f'{run}.metamon'
    output.mkdir(parents=True, exist_ok=False)
    checkpoint_dir = Path(settings['base.checkpoint_dir']).resolve() / 'pokemon' / run
    (output / 'settings.json').write_text(json.dumps(settings, indent=2)+'\n')
    child_settings = {**settings, 'sweep.metric': 'score', 'oak.enabled': '0'}
    read_fd, write_fd = os.pipe()
    child_settings['base.result_fd'] = str(write_fd)
    command = [settings['sweep.trainer_path'], 'train'] + [f'--{k}={v}' for k,v in child_settings.items()]
    started = time.monotonic()
    child = None
    snapshots = SnapshotEvaluations(settings, checkpoint_dir, output, final_steps, interval)
    try:
        print(f'[Metamon sweep] {run}: training fresh 1024x2 for '
              f'{float(settings["train.total_timesteps"]):,.0f} steps', flush=True)
        with (output / 'train.log').open('w') as log:
            child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                     pass_fds=(write_fd,), start_new_session=True)
            os.close(write_fd)
            write_fd = -1
            status_at = time.monotonic()
            interrupted = False
            while child.poll() is None:
                if STOP:
                    interrupted = True
                    os.kill(child.pid, signal.SIGUSR1)
                    try:
                        child.wait(timeout=60)
                    except subprocess.TimeoutExpired:
                        os.killpg(child.pid, signal.SIGKILL)
                    break
                snapshots.update()
                if time.monotonic()-status_at >= 30:
                    path = logs / f'{run}.progress.csv'
                    if path.exists():
                        with path.open() as stream:
                            rows = list(csv.DictReader(stream))
                        if rows and rows[-1].get('sps') and rows[-1].get('steps'):
                            print(f'[Metamon sweep] {run}: {rows[-1]["steps"]} steps, '
                                  f'{float(rows[-1]["sps"]):,.0f} steps/s', flush=True)
                    else:
                        with (output/'train.log').open('rb') as stream:
                            stream.seek(max(0, stream.seek(0, 2)-32768))
                            lines = stream.read().decode(errors='replace').splitlines()
                        steps_line = next((line.strip() for line in reversed(lines) if '│ Steps ' in line), '')
                        speed_line = next((line.strip() for line in reversed(lines) if '│ SPS ' in line), '')
                        print(f'[Metamon sweep] {run}: {steps_line} {speed_line}', flush=True)
                    status_at = time.monotonic()
                time.sleep(1)
            code = child.wait()
            data = os.read(read_fd, RESULT.size)
        if code or len(data) != RESULT.size or interrupted or STOP:
            raise RuntimeError(f'Trainer exit={code}, bytes={len(data)}, interrupted={interrupted}; {output}/train.log')
        native = RESULT.unpack(data)
        checkpoint = max(checkpoint_dir.glob('[0-9]'*16+'.bin'), key=lambda p:int(p.stem))
        steps = int(checkpoint.stem)
        requested = int(float(settings['train.total_timesteps']))
        rollout = int(float(settings['vec.total_agents'])) * int(float(settings['train.horizon']))
        if abs(steps-requested) > rollout or abs(native[2]-steps) > 256:
            raise RuntimeError('Trainer did not finish requested budget')
        snapshots.finish()
        games, seed = int(settings['sweep.eval_games']), int(settings['sweep.eval_seed'])
        print(f'[Metamon sweep] {run}: training complete; evaluating {3*games} Metamon games', flush=True)
        evaluate = evaluation_command(settings, checkpoint, output/'games')
        with (output / 'eval.log').open('w') as log:
            subprocess.run(evaluate, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT,
                           env=evaluation_env(),
                           timeout=float(settings['sweep.eval_timeout']))
        report = json.loads((output / 'games/summary.json').read_text())
        score = validate(report, checkpoint, games, seed)
        cost = time.monotonic()-started
        summary = dict(run=run, checkpoint=str(checkpoint), score=score, steps=steps,
                       requested_steps=requested, cost=cost, settings=settings,
                       trainer_sha256=hashlib.sha256(Path(settings['sweep.trainer_path']).read_bytes()).hexdigest(),
                       snapshot_evaluations=str(output/'snapshot_evaluations.json'),
                       evaluation=str(output/'games/summary.json'))
        (output/'trial.json').write_text(json.dumps(summary, indent=2)+'\n')
        print(f'[Metamon sweep] {run}: {score:.2%} ({report["wins"]}W/{report["draws"]}D/'
              f'{report["losses"]}L), {steps:,} steps, {cost/3600:.2f} hours', flush=True)
        if destination > 0:
            os.write(destination, packet(score, cost, steps))
    finally:
        snapshots.close()
        os.close(read_fd)
        if write_fd >= 0:
            os.close(write_fd)
        if child is not None and child.poll() is None:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()

def stop(signum, frame):
    global STOP
    STOP = True

if __name__ == '__main__':
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    try:
        main()
    except Exception as error:
        print(f'[Metamon sweep] FAILED: {error}', file=sys.stderr, flush=True)
        sys.exit(1)
