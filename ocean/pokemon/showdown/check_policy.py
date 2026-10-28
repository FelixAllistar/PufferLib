#!/usr/bin/env python3
"""Does the bridge change our policy's decisions? For random-choice battles (check_engine.py's), feeds the policy server
both observation streams of each player, the tracker's (from what Showdown sends) and the engine's own, each with its own
recurrent state, and counts decisions where the chosen action differs. Usage (repo root): check_policy.py CHECKPOINT [BATTLES]"""
import glob, os, random, subprocess, sys
ck = sys.argv[1]; n = int(sys.argv[2]) if len(sys.argv) > 2 else 30
teams = sorted(glob.glob('reports/pokemon_benchmarks/metamon_competitive_gen1ou/*.gen1ou_team'))
cmds, keys = [], []
for k in range(n):
    rnd = random.Random(k); seed = rnd.getrandbits(63); a, b = rnd.sample(teams, 2)
    trace = subprocess.run(['node', 'build/pokemon/showdown_parity/battle.js', str(seed), a, b], capture_output=True,
                           text=True, check=True, env={**os.environ, 'TRACKER_OBS': '1'}).stdout
    eng = subprocess.run(['build/pokemon/engine_replay'], input=trace, capture_output=True, text=True,
                         env={**os.environ, 'ENGINE_OBS': '1'}).stdout
    t = [l.split() for l in trace.splitlines() if l.startswith('tobs')]
    e = [l.split() for l in eng.splitlines() if l.startswith('obs')]
    for src, rows in (('t', t), ('e', e)):
        for p in ('1', '2'): cmds.append(f'{k}-{p}-{src} new')
        for j, (_, p, h) in enumerate(rows[:min(len(t), len(e))]):
            cmds.append(f'{k}-{p}-{src} {h}')
            if src == 't': keys.append((k, p, j // 2))
out = subprocess.run(['build/pokemon/policy_server', ck], input='\n'.join(cmds) + '\n', capture_output=True, text=True).stdout
acts = {}
for line in out.splitlines():
    bid, a = line.split()
    acts.setdefault(bid, []).append(a)
same = total = obs_same = 0
for k in range(n):
    for p in ('1', '2'):
        x, y = acts.get(f'{k}-{p}-t', []), acts.get(f'{k}-{p}-e', [])
        total += min(len(x), len(y)); same += sum(u == v for u, v in zip(x, y))
print(f'{n} battles, {total} decisions: same action {same} ({100 * same / total:.2f}%)')
