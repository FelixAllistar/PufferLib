import sys, json, collections
path, n_last = sys.argv[1], int(sys.argv[2])
lines = collections.deque(open(path), maxlen=n_last)
team_c = collections.Counter(); sets = collections.defaultdict(collections.Counter); moves = collections.defaultdict(collections.Counter); species = collections.Counter(); n = 0
for l in lines:
    try: r = json.loads(l)
    except Exception: continue
    if r.get("draft_mode") != "normal": continue
    for side, b in enumerate(r["banks"]):
        if b != 0: continue
        n += 1
        team = r["teams"][side]; team_c[tuple(sorted(team))] += 1
        for sp, mv in zip(team, r["moves"][side]):
            species[sp] += 1; sets[sp][tuple(sorted(mv))] += 1; moves[sp].update(mv)
print(f"{n} learner free drafts (last {n_last} log lines)")
for t, k in team_c.most_common(2): print(f"  team {100*k/n:.0f}%: {', '.join(t)}")
for sp, k in species.most_common(8):
    top = sets[sp].most_common(2)
    print(f"{sp} ({100*k/n:.0f}% of drafts)")
    for s, c in top: print(f"   {100*c/k:5.1f}%  {' / '.join(s)}")
    print("   moves:", ", ".join(f"{m} {100*c/k:.0f}%" for m, c in moves[sp].most_common(7)))
