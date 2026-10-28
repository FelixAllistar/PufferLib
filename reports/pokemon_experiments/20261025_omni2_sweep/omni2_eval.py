#!/usr/bin/env python3
"""Metamon for the OMNI-2 sweep's best trials: every trial that enters the top 3 by sweep score is evaluated on all 19
pool teams vs SyntheticRLV2 and TaurosV0 (48 games per Metamon panel team = 144 games per team and opponent, argmax,
seed 20261025, batched evaluator). Results: metamon.txt (one line per trial: means and per-team scores) and
eval/<trial>/<team>/eval_<opponent>/summary.json. Weight copies are deleted after each trial. Checks every 5 minutes.
Usage (box, repo root): python3 omni2_eval.py SWEEP_DIR"""
import glob, json, os, re, shutil, subprocess, sys, time

D = sys.argv[1]
PY = 'build/pokemon/metamon-eval-env-cu128/bin/python'
OPPONENTS = ['SyntheticRLV2', 'TaurosV0']
pool = open(f'{D}/pool.ini').read()
TEAMS = re.findall(r'\[bank\.\d+\]\s*name = (\S+)\s*team = (\S+)\s*lead = (\S+)', pool)
assert len(TEAMS) == 19, len(TEAMS)


def scores():
    out = {}
    for line in open(f'{D}/sweep.log', errors='ignore'):
        m = re.match(r'sweep run=(\d+) score=(\S+)', line)
        if m: out[int(m.group(1))] = float(m.group(2))
    return out


def with_team(src, dst, team, lead):
    lines, section = [], None
    for line in open(src):
        s = line.strip()
        if s.startswith('['): section = s[1:-1]
        key = s.split('=')[0].strip() if '=' in s else None
        if section == 'env' and key == 'learner_team': line = f'learner_team = {team}\n'
        if section == 'env' and key == 'learner_lead': line = f'learner_lead = {lead}\n'
        lines.append(line)
    open(dst, 'w').writelines(lines)


def evaluate(run):
    ckdir = glob.glob(f'{D}/checkpoints/pokemon/sweep_*_{run:04d}')[0]
    final = sorted(f for f in glob.glob(f'{ckdir}/*.bin') if not f.endswith('0000000000000000.bin'))[-1]
    cks = []
    for name, team, lead in TEAMS:
        d = f'{D}/eval/{run:04d}/{name}'
        os.makedirs(d, exist_ok=True)
        shutil.copy(final, f'{d}/{os.path.basename(final)}')  # a copy: evaluate_private resolves symlinks
        with_team(f'{ckdir}/config.ini', f'{d}/config.ini', team, lead)
        cks.append(f'{d}/{os.path.basename(final)}')
    for opp in OPPONENTS:
        outs = [f'{os.path.dirname(c)}/eval_{opp}' for c in cks]
        subprocess.run([PY, 'ocean/pokemon/benchmarks/evaluate_batched.py', *cks, '--output', *outs, '--skip-done',
                        '--opponent', opp, '--mode', 'argmax', '--games', '48', '--seed', '20261025', '--batch', '256',
                        '--workers', '12', '--threads', '1'], check=True, stdout=subprocess.DEVNULL)
    table = {name: {o: json.load(open(f'{D}/eval/{run:04d}/{name}/eval_{o}/summary.json'))['score'] for o in OPPONENTS}
             for name, _, _ in TEAMS}
    for c in cks: os.remove(c)
    mean = {o: sum(t[o] for t in table.values()) / len(table) for o in OPPONENTS}
    league = [n for n, _, _ in TEAMS[:13]]
    both = lambda names: sum((table[n]['SyntheticRLV2'] + table[n]['TaurosV0']) / 2 for n in names) / len(names)
    line = (f'trial {run:4d} metamon {100 * (mean["SyntheticRLV2"] + mean["TaurosV0"]) / 2:5.1f} '
            f'(Syn {100 * mean["SyntheticRLV2"]:.1f} Tau {100 * mean["TaurosV0"]:.1f}) league13 {100 * both(league):.1f} '
            f'new6 {100 * both([n for n, _, _ in TEAMS[13:]]):.1f} | '
            + ' '.join(f'{n}={100 * t["SyntheticRLV2"]:.0f}/{100 * t["TaurosV0"]:.0f}' for n, t in table.items()))
    with open(f'{D}/metamon.txt', 'a') as f: f.write(line + '\n')
    print(line, flush=True)


done = set()
if os.path.exists(f'{D}/metamon.txt'):
    done = {int(l.split()[1]) for l in open(f'{D}/metamon.txt') if l.startswith('trial')}
while True:
    s = scores()
    for run in sorted(s, key=lambda r: -s[r])[:3]:
        if run not in done:
            evaluate(run)
            done.add(run)
            break
    else:
        time.sleep(300)
