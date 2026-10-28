#!/usr/bin/env python3
"""Turn-level story of our most recent ladder losses: both teams as seen, and the key events per side (faints,
sleep/freeze/paralysis, crits, misses, Explosion/Self-Destruct, switches). Usage: loss_story.py [N_LOSSES]"""
import glob, os, re, sys, collections
ME = 'felix_pufferlib'; n = int(sys.argv[1]) if len(sys.argv) > 1 else 4
logs = sorted(glob.glob('ladder_battles/*.log'), key=os.path.getmtime)
losses = [f for f in logs if '|win|' in open(f, errors='ignore').read() and not re.search(r'\|win\|' + ME, open(f, errors='ignore').read())][-n:]
for f in losses:
    t = open(f, errors='ignore').read()
    me = re.search(r'\|player\|(p[12])\|' + ME + r'\|', t).group(1); foe = 'p2' if me == 'p1' else 'p1'
    opp = re.search(r'\|player\|%s\|([^|]+)\|[^|]*\|(\d*)' % foe, t)
    nick = {}
    for m in re.finditer(r'\|(?:switch|drag)\|(p[12])a: ([^|]+)\|([^,|]+)', t): nick[(m.group(1), m.group(2))] = m.group(3)
    side = lambda s: 'US ' if s == me else 'OPP'
    def who(ident):
        p, nm = ident[:2], ident.split(': ', 1)[1]
        return f"{side(p)} {nick.get((p, nm), nm)}"
    ev = collections.Counter(); story = []; turn = 0
    for line in t.splitlines():
        f2 = line.split('|')
        if len(f2) < 2: continue
        k = f2[1]
        if k == 'turn': turn = int(f2[2])
        elif k == 'faint': story.append(f'T{turn} faint {who(f2[2])}'); ev[(f2[2][:2], 'faints')] += 1
        elif k == '-status': story.append(f'T{turn} {f2[3]} {who(f2[2])}'); ev[(f2[2][:2], 'status ' + f2[3])] += 1
        elif k == '-crit': ev[(f2[2][:2], 'crits taken')] += 1
        elif k == '-miss': ev[(f2[2][:2], 'missed')] += 1
        elif k == 'move' and f2[3] in ('Explosion', 'Self-Destruct', 'Hyper Beam', 'Rest'):
            ev[(f2[2][:2], f2[3])] += 1
            if f2[3] in ('Explosion', 'Self-Destruct'): story.append(f'T{turn} {f2[3]} by {who(f2[2])}')
        elif k == 'cant' and len(f2) > 3 and f2[3] in ('slp', 'frz', 'par'): ev[(f2[2][:2], 'turns lost to ' + f2[3])] += 1
    foeteam = sorted({v for (p, _), v in nick.items() if p == foe})
    print(f"\n=== loss vs {opp.group(1)} ({opp.group(2)}), {turn} turns. Their team: {', '.join(foeteam)}")
    for s in (me, foe):
        print('  ' + side(s) + ': ' + ', '.join(f'{k} {v}' for (p, k), v in sorted(ev.items()) if p == s))
    print('  key events: ' + '; '.join(story[:40]))
