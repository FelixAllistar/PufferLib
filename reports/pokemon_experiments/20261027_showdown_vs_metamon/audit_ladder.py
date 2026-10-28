#!/usr/bin/env python3
"""Ladder audit: for every saved battle log (ladder_battles/), our result, opponent, rating, turns and the opponent's team
as revealed (species and moves seen). Prints record by our team, the species we meet and lose to, and every loss.
Writes ladder_audit.json. Usage (in 20261027_showdown_vs_metamon): audit_ladder.py"""
import collections, glob, json, os, re
ME = 'felix_pufferlib'
rows = []
for f in sorted(glob.glob('ladder_battles/*.log'), key=os.path.getmtime):
    t = open(f, errors='ignore').read()
    if '|win|' not in t and '|tie' not in t: continue
    side = re.search(r'\|player\|(p[12])\|' + ME + r'\|', t)
    if not side: continue
    side = side.group(1); foe = 'p2' if side == 'p1' else 'p1'
    opp = re.search(r'\|player\|%s\|([^|]+)\|[^|]*\|(\d*)' % foe, t)
    win = re.search(r'\|win\|(.+)', t)
    nick = {}; team = collections.OrderedDict()
    for m in re.finditer(r'\|(?:switch|drag|replace)\|%sa: ([^|]+)\|([^,|]+)' % foe, t):
        nick[m.group(1)] = m.group(2); team.setdefault(m.group(2), set())
    for m in re.finditer(r'\|move\|%sa: ([^|]+)\|([^|]+)(\|[^\n]*)?' % foe, t):
        if '[from]' in (m.group(3) or ''): continue
        sp = nick.get(m.group(1))
        if sp: team[sp].add(m.group(2))
    mine = sorted(set(re.findall(r'\|switch\|%sa: [^|]*\|([^,|]+)' % side, t)))
    turns = re.findall(r'\|turn\|(\d+)', t)
    rows.append(dict(file=f, opp=opp.group(1) if opp else '?', rating=opp.group(2) if opp else '',
                     won=bool(win and win.group(1).strip() == ME), turns=int(turns[-1]) if turns else 0,
                     mine=','.join(mine), team={k: sorted(v) for k, v in team.items()}))
json.dump(rows, open('ladder_audit.json', 'w'), indent=1)
by_team = collections.defaultdict(lambda: [0, 0])
for r in rows: by_team[r['mine']][0 if r['won'] else 1] += 1
print(len(rows), 'battles with full logs')
for k, (w, l) in by_team.items(): print(f'  our team {k}: {w}-{l}')
seen = collections.Counter(s for r in rows for s in r['team'])
lost = collections.Counter(s for r in rows if not r['won'] for s in r['team'])
print('opponent species: battles seen in / our losses it was in')
for s, n in seen.most_common(): print(f'  {s:12s} {n:3d} / {lost[s]}')
print('losses:')
for r in rows:
    if r['won']: continue
    team = '; '.join(k + ' [' + '/'.join(v) + ']' for k, v in r['team'].items())
    print(f"  {r['opp']} ({r['rating']}), {r['turns']} turns: {team}")
