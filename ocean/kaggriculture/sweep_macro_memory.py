#!/usr/bin/env python3
"""Sequential native-PPO sweep. One active INI; immutable audit data is JSON.

Run --prepare while another trainer is running, then --run when GPU is free.
No league promotions, reward mutations, or Kaggle submissions happen here.
"""
import argparse
import configparser
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def initial_trials():
    # Baseline first, then isolate agent count and rollout length.
    shapes = [(4096, 256), (8192, 128), (4096, 128), (8192, 64)]
    return [dict(agents=a, horizon=h, minibatch=m, lr=.004, seed=42)
            for a, h in shapes for m in (2048, 1024, 4096)] + [
        dict(agents=16384, horizon=h, minibatch=2048, lr=.004, seed=42)
        for h in (32, 64)]


def amend_prestart(out, steps):
    if (out / 'baseline.json').exists() or list(out.glob('*_eval.log')) or list(out.glob('trial_*.json')):
        raise RuntimeError('Sweep already started; refusing to change its comparison budget.')
    path = out / 'plan.json'
    plan = json.loads(path.read_text())
    plan['steps'] = steps
    for trial in initial_trials():
        if trial['agents'] == 16384 and trial not in plan['trials']:
            plan['trials'].append(trial)
    temp = out / 'plan.json.pending'
    temp.write_text(json.dumps(plan, indent=2) + '\n')
    temp.replace(path)
    print(f'Updated unstarted sweep: {steps} steps/trial, {len(plan["trials"])} initial trials.')


def is_oom(text):
    return any(marker in text.lower() for marker in (
        'out of memory', 'cudaerrormemoryallocation', 'cublas_status_alloc_failed'))


def cli_value(value):
    # INI files accept quoted strings. subprocess argv does not have a shell
    # to remove those quotes, and the native CLI parser preserves them.
    value = str(value).strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
        return value[1:-1]
    return value


def refinements(winners):
    result = []
    for w in winners:
        for lr in (.002, .006):
            result.append(dict(w, lr=lr))
        for h in (w['horizon'] // 2, w['horizon'] * 2):
            if h >= 32 and w['agents'] * h <= 1048576:
                result.append(dict(w, horizon=h))
    return list({json.dumps(x, sort_keys=True): x for x in result}.values())


def config_values():
    c = configparser.ConfigParser(interpolation=None, strict=False)
    c.read([ROOT / 'config/default.ini', ROOT / 'config/kaggriculture.ini'])
    return {f'{s}.{k}': v for s in ('base', 'policy', 'torch', 'vec', 'selfplay', 'env', 'train')
            if c.has_section(s) for k, v in c.items(s)}


def prepare(out, champion, league, steps, games):
    out.mkdir(parents=True, exist_ok=True)
    planfile = out / 'plan.json'
    if planfile.exists():
        raise SystemExit('Plan already exists; use --run to resume, or a new output name.')
    champion = champion.resolve()
    frozen = out / 'league'
    frozen.mkdir()
    c = configparser.ConfigParser(interpolation=None)
    c.read(league)
    opponents = []
    for section in c.sections():
        if not section.startswith('policy.') or c.getint(section, 'enabled', fallback=1) == 0:
            continue
        src = Path(c[section]['path'].strip("'\""))
        if not src.is_absolute():
            src = ROOT / src
        dst = frozen / src.name
        shutil.copy2(src, dst)
        c[section]['path'] = str(dst)
        opponents.append(dst)
    if not 2 <= len(opponents) <= 8:
        raise SystemExit('Expected 2..8 league opponents')
    with (frozen / 'league.ini').open('w') as f:
        c.write(f)
    shutil.copy2(champion, out / 'champion.bin')
    if Path(str(champion) + '.emag').exists():
        shutil.copy2(str(champion) + '.emag', out / 'champion.bin.emag')
    (out / 'opponents.tsv').write_text('id\tpolicy\tweight\tcheckpoint\n' + ''.join(
        f'{i}\t{p.stem}\t1\t{p}\n' for i, p in enumerate(opponents)))
    plan = dict(base=config_values(), steps=steps, games=games, trials=initial_trials())
    planfile.write_text(json.dumps(plan, indent=2) + '\n')
    print(f'Prepared {len(plan["trials"])} initial trials; active config unchanged. {planfile}')


def execute(cmd, log):
    values = dict(x.split('=', 1) for x in cmd[3:] if '=' in x)
    print('START', cmd[1], values.get('base.run_id'),
          'agents='+values.get('vec.total_agents', '?'),
          'horizon='+values.get('train.horizon', '?'),
          'minibatch='+values.get('train.minibatch_size', '?'),
          'lr='+values.get('train.learning_rate', '?'), 'log='+str(log), flush=True)
    log.with_suffix('.command.json').write_text(json.dumps(cmd, indent=2))
    with log.open('w') as f:
        return subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode


def evaluate(out, plan, base, name, checkpoints):
    manifest = out / f'{name}_candidates.tsv'
    manifest.write_text('id\tpolicy\tcheckpoint\n' + ''.join(
        f'{i}\t{p.parent.name}@{p.stem}\t{p}\n' for i, p in enumerate(checkpoints)))
    raw = out / f'{name}_matches.tsv'
    values = dict(base, **{'league.mode': 'screen', 'league.candidate_manifest': str(manifest),
        'league.opponent_manifest': str(out / 'opponents.tsv'), 'league.output': str(raw),
        'league.games': str(plan['games']), 'league.min_agents': '0',
        'base.run_id': f'{out.name}_{name}_eval',
        'base.eval_deterministic': '1', 'base.seed': '6100', 'env.seed': '6100',
        'env.reset_state_prob': '0', 'env.reset_opening_prob': '0',
        'env.curriculum_enabled': '0'})
    if execute(['./puffer', 'league', 'kaggriculture'] + [f'{k}={v}' for k, v in values.items()],
               out / f'{name}_eval.log'):
        raise RuntimeError(f'Evaluation failed: {name}')
    rows = [line.split('\t') for line in raw.read_text().splitlines() if line.strip()]
    results = []
    for i, ckpt in enumerate(checkpoints):
        matches = [r for r in rows if int(r[0]) == i]
        if len(matches) != len((out / 'opponents.tsv').read_text().splitlines()) - 1:
            raise RuntimeError('Incomplete opponent coverage')
        results.append(dict(checkpoint=str(ckpt), score=sum(float(r[2]) for r in matches)/len(matches),
            money=sum(float(r[4]) for r in matches)/len(matches),
            margin=sum(float(r[4])-float(r[5]) for r in matches)/len(matches)))
    return results


def run(out):
    plan = json.loads((out / 'plan.json').read_text())
    base = {k: cli_value(v) for k, v in plan['base'].items()}
    base.update({'base.load_model_path': str(out / 'champion.bin'),
        'base.env_name': 'kaggriculture', 'base.checkpoint_dir': 'checkpoints', 'base.log_dir': 'logs',
        'selfplay.opponent_league': str(out / 'league/league.ini'),
        'selfplay.opponent_pool': 'None', 'selfplay.opponent_pool_prob': '1',
        'selfplay.snapshot_interval': str(10**12), 'train.anneal_lr': '0',
        'selfplay.eval_pool_size': '0'})
    if float(base.get('vec.frozen_bank_pct', '0')) != .75:
        raise RuntimeError('This trial grid was validated for frozen_bank_pct=.75; review grid for changed bank allocation.')
    if not (out / 'baseline.json').exists():
        (out / 'baseline.json').write_text(json.dumps(evaluate(out, plan, base, 'baseline',
            [out / 'champion.bin']), indent=2))
    for phase in range(3):
        if phase == 0:
            trials = plan['trials']
        else:
            previous = [json.loads(p.read_text()) for p in out.glob('trial_*.json')]
            previous = [x for x in previous if x.get('status') == 'complete' and x['phase'] < phase]
            previous.sort(key=lambda x: (x['final']['score'], x['final']['margin']), reverse=True)
            winners = [x['params'] for x in previous[:2]]
            trials = refinements(winners) if phase == 1 else [dict(w, seed=43) for w in winners]
            if phase == 1:
                tried = {json.dumps(x['params'], sort_keys=True) for x in previous}
                trials = [x for x in trials if json.dumps(x, sort_keys=True) not in tried]
        for i, params in enumerate(trials):
            name = f'{out.name}_p{phase}_{i:02d}'
            record = out / f'trial_{phase}_{i:02d}.json'
            if record.exists():
                continue
            run_dir = ROOT / 'checkpoints/kaggriculture' / name
            if run_dir.exists():
                raise RuntimeError(f'Interrupted run preserved: {run_dir}. Choose a new sweep output; not overwriting.')
            a, h, m = (params[k] for k in ('agents', 'horizon', 'minibatch'))
            v = dict(base, **{'base.run_id': name, 'vec.total_agents': str(a),
                'train.horizon': str(h), 'train.minibatch_size': str(m),
                'train.learning_rate': str(params['lr']), 'train.seed': str(params['seed']),
                'base.seed': str(params['seed']), 'env.seed': str(params['seed']),
                'selfplay.seed': str(params['seed']),
                'train.total_timesteps': str(plan['steps']),
                'base.checkpoint_interval': str(max(1, 25000000 // (a*h)))})
            # Allocates real training tensors and exercises multiple complete updates.
            probe = dict(v, **{'base.run_id': name+'_probe', 'train.total_timesteps': str(a*h*2),
                'base.checkpoint_interval': '1000000'})
            command = lambda values: ['./puffer', 'train', 'kaggriculture'] + [f'{k}={v}' for k, v in values.items()]
            rc = execute(command(probe), out / f'{name}_probe.log')
            start = time.monotonic()
            if not rc:
                rc = execute(command(v), out / f'{name}_train.log')
            result = dict(phase=phase, params=params, seconds=time.monotonic()-start, status='failed')
            if rc:
                text = '\n'.join(p.read_text(errors='replace') for p in out.glob(f'{name}_*.log'))
                result['status'] = 'oom' if is_oom(text) else 'failed'
                record.write_text(json.dumps(result, indent=2))
                if result['status'] != 'oom':
                    raise RuntimeError(f'{name} failed; stopped, see logs')
                # Ordinary allocation failure is recoverable after the child exits;
                # a sick driver/device is not something to blindly retry.
                subprocess.run(['nvidia-smi', '--query-gpu=uuid', '--format=csv,noheader'],
                               check=True, timeout=15, stdout=subprocess.DEVNULL)
                print(f'OOM: {name}; isolated process exited, continuing.', flush=True)
                continue
            checkpoints = sorted(run_dir.glob('[0-9]'*16 + '.bin'))
            if not checkpoints:
                raise RuntimeError(f'No checkpoints: {name}')
            results = evaluate(out, plan, base, name, checkpoints)
            result.update(status='complete', evaluations=results, final=results[-1])
            record.write_text(json.dumps(result, indent=2))
            print(f'RESULT {name}: {result["final"]} seconds={result["seconds"]:.1f}', flush=True)
    print('COMPLETE. Compare trial_*.json with baseline.json; no automatic promotion.', flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--champion', type=Path)
    p.add_argument('--league', type=Path)
    p.add_argument('--steps', type=int, default=300000000)
    p.add_argument('--games', type=int, default=64)
    p.add_argument('--prepare', action='store_true')
    p.add_argument('--run', action='store_true')
    p.add_argument('--amend-prestart', action='store_true')
    args = p.parse_args()
    if args.steps < 1 or args.games < 2 or args.games % 2:
        p.error('steps must be positive; games must be even and at least 2')
    args.output = args.output.resolve()
    if args.amend_prestart:
        amend_prestart(args.output, args.steps)
    if args.prepare:
        if not args.champion or not args.league:
            p.error('--prepare requires --champion and --league')
        prepare(args.output, args.champion, args.league, args.steps, args.games)
    if args.run:
        run(args.output)
