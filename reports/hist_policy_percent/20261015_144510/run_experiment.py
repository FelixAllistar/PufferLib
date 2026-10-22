from pathlib import Path
import configparser
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parent
results = []
for name in ('disabled', 'enabled', 'enabled_final_eval'):
    case = root / name
    with (case / 'sweep.log').open('w') as log:
        done = subprocess.run([str(root / 'goofspiel'), 'sweep'], cwd=case,
                              stdout=log, stderr=subprocess.STDOUT, timeout=45)
    if done.returncode:
        print((case / 'sweep.log').read_text()[-5000:], flush=True)
        raise SystemExit(f'{name} failed: {done.returncode}')
    history = [line.split() for line in (case / 'history.txt').read_text().splitlines() if line.strip()]
    assert history[0] == ['PUFFER_SWEEP_HISTORY_V1', '1']
    assert history[1][0] == 'vec.hist_policy_percent'
    trials = {int(row[0]): {'sampled': float(row[4]), 'failed': int(row[3])} for row in history[2:]}
    logs = sorted((case / 'logs/goofspiel').glob('*.ini'))
    assert len(logs) == len(trials) == 3
    for path in logs:
        run = int(path.stem.rsplit('_', 1)[1])
        row = trials[run]
        cfg = configparser.ConfigParser(interpolation=None)
        cfg.read(path)
        checkpoints = list((case / 'checkpoints/goofspiel' / path.stem).glob('*.bin.ini'))
        final = max(checkpoints, key=lambda p: int(p.name.split('.')[0]))
        trained = configparser.ConfigParser(interpolation=None)
        trained.read(final)
        row.update(case=name, run=run, training_fraction=trained.getfloat('vec', 'hist_policy_percent'),
                   logged_fraction=cfg.getfloat('vec', 'hist_policy_percent'),
                   logged_selfplay=cfg.getint('selfplay', 'enabled'),
                   training_policies=trained.getint('vec', 'num_policies'),
                   logged_policies=cfg.getint('vec', 'num_policies'),
                   training_agents=trained.getint('vec', 'total_agents'),
                   logged_agents=cfg.getint('vec', 'total_agents'),
                   log=str(path.relative_to(root)), checkpoint_config=str(final.relative_to(root)))
        assert row['failed'] == 0 and .0625 <= row['sampled'] <= 1
        expected = 0 if name == 'disabled' else row['sampled']
        assert abs(row['training_fraction'] - expected) < 1e-6
        logged = row['sampled'] if name == 'enabled' else 0
        assert abs(row['logged_fraction'] - logged) < 1e-6
        results.append(row)
        print(f"{name} run={run} sampled={row['sampled']:.9g} train={row['training_fraction']:.9g} log={row['logged_fraction']:.9g} policies={row['training_policies']}->{row['logged_policies']}", flush=True)
(root / 'results.json').write_text(json.dumps({'trials': results, 'passed': True}, indent=2) + '\n')
print('PASS: samples stay in range; disabled selfplay resets the effective value; final evaluation mutates the logged training configuration.', flush=True)
