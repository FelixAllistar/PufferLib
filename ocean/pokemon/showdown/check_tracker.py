#!/usr/bin/env python3
"""Tracker parity: for random-choice battles (check_engine.py's), the tracker's observation (built from what Showdown sends
one player) vs our engine's pk_observe on the identical battle, before every decision, both players. Reports mismatching
bytes by field; foe HP (Showdown sends a percentage) is reported separately as an approximation.
Usage (repo root): check_tracker.py [BATTLES]"""
import collections, glob, os, random, subprocess, sys
n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
only = sys.argv[2] if len(sys.argv) > 2 else None  # print the protocol around the first mismatch of this field
teams = sorted(glob.glob('reports/pokemon_benchmarks/metamon_competitive_gen1ou/*.gen1ou_team'))
VOL = ['Bide', 'Thrashing', 'Charging', 'Binding', 'Invulnerable', 'Confusion', 'Mist', 'FocusEnergy', 'Substitute',
       'Recharging', 'Rage', 'LeechSeed', 'Toxic', 'LightScreen', 'Reflect']
def field(i):
    if i < 16: return {0: 'phase', 1: 'byte1', 2: 'turn', 3: 'turn', 4: 'active id self', 5: 'active id foe', 6: 'pending self',
                       7: 'pending foe', 8: 'last move self', 9: 'last move foe', 10: 'last actor self', 11: 'last actor foe'}.get(i, f'hdr{i}')
    if i < 400:
        r, k = divmod(i - 16, 192); k %= 32; side = 'self' if r == 0 else 'foe'
        names = {0: 'species', 1: 'hp', 2: 'status', 3: 'fainted', 4: 'level', 5: 'type1', 6: 'type2'}
        if k in names: return f'{side} mon {names[k]}'
        if 8 <= k < 12: return f'{side} mon moves'
        if 12 <= k < 16: return f'{side} mon pp'
        return f'{side} mon stat'
    if i < 464:
        r, k = divmod(i - 400, 32); side = 'self' if r == 0 else 'foe'
        if k == 0: return f'{side} active species'
        if k < 3: return f'{side} active types'
        if k < 9: return f'{side} active boosts'
        if k < 24: return f'{side} volatile {VOL[k - 9]}'
        return f'{side} active stats'
    if i >= 480: return f'mask {i - 480}'
    return f'byte{i}'
total = collections.Counter(); decisions = 0; examples = {}; set_bad = set()
for k in range(n):
    rnd = random.Random(k); seed = rnd.getrandbits(63); a, b = rnd.sample(teams, 2)
    trace = subprocess.run(['node', 'build/pokemon/showdown_parity/battle.js', str(seed), a, b], capture_output=True, text=True,
                           check=True, env={**os.environ, 'TRACKER_OBS': '1', 'SHOWDOWN_LOG': '1'}).stdout
    eng = subprocess.run(['build/pokemon/engine_replay'], input=trace, capture_output=True, text=True,
                         env={**os.environ, 'ENGINE_OBS': '1'}).stdout
    t = [l.split()[2] for l in trace.splitlines() if l.startswith('tobs')]
    e = [l.split()[2] for l in eng.splitlines() if l.startswith('obs')]
    for j, (x, y) in enumerate(zip(t, e)):
        x, y = bytes.fromhex(x), bytes.fromhex(y); decisions += 1
        bad = {field(i) for i in range(648) if x[i] != y[i]}
        if bad: set_bad.add((k, j))
        for f in bad:
            total[f] += 1
            if f not in examples and only and only in f:
                chunks = trace.split('\ndo ')
                print(f'--- {f}: battle {k} decision {j // 2} p{j % 2 + 1}')
                print('\n'.join(l for l in ('do ' + '\ndo '.join(chunks[max(1, j // 2 - 1):j // 2 + 1])).splitlines() if l.startswith(('# |', 'do')) and '|t:|' not in l)[-3000:])
            examples.setdefault(f, (k, seed, j // 2, j % 2 + 1, [(i, x[i], y[i]) for i in range(648) if x[i] != y[i] and field(i) == f][:3]))
exact = sum(1 for _ in [])
print(f'{n} battles, {decisions} observations, {decisions - len(set_bad)} byte-identical ({100 * (decisions - len(set_bad)) / decisions:.2f}%)')
for f, c in total.most_common():
    print(f'{c:6d} ({100 * c / decisions:5.1f}%)  {f:28s} e.g. battle {examples[f][0]} decision {examples[f][2]} p{examples[f][3]}: (byte, tracker, engine) {examples[f][4]}')
