#!/usr/bin/env python3
"""Goofspiel sweep worker: one trial = SEEDS independent trainings of the suggested settings.
Score = mean final magnet exploitability (exact, at the end of training), so one lucky seed or
checkpoint cannot win. Returns the native TrainResult packet over base.result_fd."""
import json,math,os,re,statistics,struct,subprocess,sys,time
from pathlib import Path

RESULT = struct.Struct('=fff i 192f')
SEEDS = 4

def main():
    if len(sys.argv) < 2 or sys.argv[1] != 'train':
        raise SystemExit('usage: seed_worker.py train --section.key=value ...')
    settings = dict(a[2:].split('=', 1) for a in sys.argv[2:] if a.startswith('--'))
    fd = int(settings.get('base.result_fd', '0'))
    run = settings.get('base.run_id', 'run')
    root = Path(settings['base.checkpoint_dir'])/'goofspiel'/run
    root.mkdir(parents=True, exist_ok=True)
    trainer = settings.get('sweep.trainer_path', './puffer_ablate')
    started = time.monotonic(); seeds = []
    for seed in range(1, SEEDS+1):
        args = dict(settings)
        args.update({'base.result_fd': '0', 'base.seed': str(seed), 'env.seed': str(1000+seed),
                     'base.checkpoint_dir': str(root/f'seed{seed}'), 'base.log_dir': str(root/f'seed{seed}_logs'),
                     'base.run_id': f'{run}_s{seed}', 'sweep.early_stop_patience_steps': '0'})
        text = subprocess.run([trainer, 'train', *[f'--{k}={v}' for k, v in args.items()]],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout
        (root/f'seed{seed}.log').write_text(text)
        learner = [float(x) for x in re.findall(r'Exact checkpoint: exploitability=([0-9.e-]+)', text)]
        magnet = [float(x) for x in re.findall(r'Exact magnet: exploitability=([0-9.e-]+)', text)]
        if not learner or not magnet:
            raise RuntimeError(f'seed {seed} produced no exact scores; see {root}/seed{seed}.log')
        seeds.append({'seed': seed, 'learner': learner[-1], 'magnet': magnet[-1]})
    score = statistics.mean(s['magnet'] for s in seeds)
    cost = time.monotonic()-started; steps = float(settings['train.total_timesteps'])
    (root/'trial.json').write_text(json.dumps({'score': score, 'seeds': seeds, 'cost': cost, 'settings': settings}, indent=1))
    print(f'[Goofspiel seeds] {run}: mean final magnet {score:.5f} '
          f'(learner {statistics.mean(s["learner"] for s in seeds):.5f}) over {SEEDS} seeds', file=sys.stderr, flush=True)
    if fd > 0:
        curve = [0.0]*192; curve[0], curve[64], curve[128] = score, cost, steps
        os.write(fd, RESULT.pack(score, cost, steps, 1, *curve)); os.close(fd)

if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'[Goofspiel seeds] FAILED: {error}', file=sys.stderr, flush=True)
        sys.exit(1)
