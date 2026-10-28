#!/usr/bin/env python3
"""Engine-vs-Showdown determinism check: random-choice battles in patched Showdown (battle.ts, @pkmn/sim 0.9.31)
replayed in our engine (engine_replay); after every decision, the turn number and every Pokémon's HP must match.
Teams: Metamon's 28 competitive gen1ou teams, random pairs. Build first: ocean/pokemon/showdown/build.sh.
Usage (repo root): check_engine.py [BATTLES]"""
import glob, random, subprocess, sys
n = int(sys.argv[1]) if len(sys.argv) > 1 else 50
teams = sorted(glob.glob('reports/pokemon_benchmarks/metamon_competitive_gen1ou/*.gen1ou_team'))
diverged, decisions, notes = 0, 0, []
for k in range(n):
    rnd = random.Random(k); seed = rnd.getrandbits(63); a, b = rnd.sample(teams, 2)
    trace = subprocess.run(['node', 'build/pokemon/showdown_parity/battle.js', str(seed), a, b],
                           capture_output=True, text=True, check=True).stdout
    sd = [l for l in trace.splitlines() if l.startswith('hp ')]
    eng = [l for l in subprocess.run(['build/pokemon/engine_replay'], input=trace, capture_output=True,
                                     text=True).stdout.splitlines()]
    eh = [l for l in eng if l.startswith('hp ')]
    for i, (x, y) in enumerate(zip(sd, eh)):
        if x != y:
            diverged += 1; notes.append(f'battle {k} seed {seed} decision {i}: showdown "{x}" engine "{y}"'); break
        decisions += 1
    else:
        if len(eh) != len(sd):
            diverged += 1; notes.append(f'battle {k} seed {seed}: engine stopped after {len(eh)} of {len(sd)} ({[l for l in eng if not l.startswith("hp")][:1]})')
print('\n'.join(notes[:10]))
print(f'{n} battles, {decisions} decisions identical, {diverged} battles diverged')
