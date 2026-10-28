#!/usr/bin/env python3
"""Ladder audit by era (2026-10-28): record per team/model era, per-opponent record, and every Nivky battle with his
revealed team and the key events (sleeps, misses, crits, freezes, explosions). Usage (box repo root): era_audit.py"""
import glob, os, re, collections, datetime
D = 'reports/pokemon_experiments/20261027_showdown_vs_metamon/ladder_battles'
NEW_TEAM = 1793210434  # 28 Oct ~18:00 UTC: Stun Spore Exeggutor + Rest Snorlax
NEW_MODEL = 1793222160  # 28 Oct 21:16 UTC: ladder-run 2B model
battles = []
for f in glob.glob(f'{D}/*.log'):
    L = open(f, errors='replace').read().splitlines()
    players, side, win, t0, rat = {}, None, None, None, {}
    for l in L:
        p = l.split('|')
        if len(p) > 3 and p[1] == 'player' and p[3]:
            players[p[2]] = p[3]; rat[p[2]] = p[5] if len(p) > 5 else ''
        elif len(p) > 2 and p[1] == 'win': win = p[2]
        elif len(p) > 2 and p[1] == 't:' and t0 is None: t0 = int(p[2])
    if not win or t0 is None: continue
    us = [k for k, v in players.items() if v == 'felix_pufferlib']
    if not us: continue
    us = us[0]; them = 'p1' if us == 'p2' else 'p2'
    mons = {us: collections.defaultdict(set), them: collections.defaultdict(set)}
    ev = collections.Counter(); turns = 0
    for l in L:
        p = l.split('|')
        if len(p) < 3: continue
        if p[1] in ('switch', 'drag'):
            s = p[2][:2]; mons[s][p[3].split(',')[0]]
        elif p[1] == 'move':
            s = p[2][:2]; mons[s][p[2].split(': ', 1)[1]].add(p[3])
            if '[miss]' in l: ev[('miss', s == us)] += 1
        elif p[1] == '-crit': ev[('crit', p[2][:2] != us)] += 1   # crit landed on the other side -> attacker
        elif p[1] == '-status' and p[3] == 'slp' and '[from] move: Rest' not in l: ev[('slept', p[2][:2] != us)] += 1
        elif p[1] == '-status' and p[3] == 'frz': ev[('froze', p[2][:2] != us)] += 1
        elif p[1] == '-status' and p[3] == 'par': ev[('para', p[2][:2] != us)] += 1
        elif p[1] == 'turn': turns = int(p[2])
    battles.append(dict(t=t0, opp=players[them], won=win == 'felix_pufferlib', team=mons[them], ours=mons[us], ev=ev, turns=turns,
                        orat=rat.get(them, ''), f=os.path.basename(f)))
battles.sort(key=lambda b: b['t'])
def era(b): return 'C new model+team' if b['t'] >= NEW_MODEL else 'B new team' if b['t'] >= NEW_TEAM else 'A old team'
rec = collections.defaultdict(lambda: [0, 0])
for b in battles: rec[era(b)][0 if b['won'] else 1] += 1
print('RECORD BY ERA'); [print(f'  {k}: {w}-{l} ({100*w/(w+l):.0f}%)') for k, (w, l) in sorted(rec.items())]
last = battles[-40:]; w = sum(b['won'] for b in last); print(f'  last 40: {w}-{40-w}')
opp = collections.defaultdict(lambda: [0, 0])
for b in battles: opp[b['opp']][0 if b['won'] else 1] += 1
print('MOST PLAYED OPPONENTS'); [print(f'  {k}: {w}-{l}') for k, (w, l) in sorted(opp.items(), key=lambda x: -sum(x[1]))[:10]]
print('LOSSES SINCE THE TEAM CHANGE')
for b in battles:
    if b['t'] >= NEW_TEAM and not b['won']: print(f"  {datetime.datetime.utcfromtimestamp(b['t']):%H:%M} {b['opp']} ({b['orat']}) {b['turns']}t: " + ', '.join(sorted(b['team'])))
print('NIVKY')
for b in battles:
    if b['opp'].lower() != 'nivky': continue
    e = b['ev']
    print(f"  {datetime.datetime.utcfromtimestamp(b['t']):%d %H:%M} {'WIN ' if b['won'] else 'LOSS'} {era(b)[0]} {b['turns']}t | " +
          ' / '.join(f"{m}: {','.join(sorted(v))}" for m, v in b['team'].items()))
    print(f"      misses us {e[('miss',True)]} him {e[('miss',False)]} | crits us {e[('crit',True)]} him {e[('crit',False)]} | "
          f"we slept {e[('slept',True)]} he slept {e[('slept',False)]} | we froze {e[('froze',True)]} he froze {e[('froze',False)]} | we para {e[('para',True)]} he para {e[('para',False)]}")
