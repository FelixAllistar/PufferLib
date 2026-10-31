"""Pick regret (2026-10-31): the random-pick tournament (picks were uniform, a randomized experiment) gives each
trainer's brought sets a win rate against the field; scores another tournament's picks on that table.
Usage: python3 pick_regret.py RANDOM_GAMES.txt MODEL_GAMES.txt trainers_l100.json
"""
import json, sys, collections as C
T = json.load(open(sys.argv[3]))
def sets(path):
    out = C.defaultdict(lambda: [0.0, 0])
    for line in open(path):
        f = line.split(); ids = (int(f[1]), int(f[2])); r = int(f[3]); rest = f[4:]
        for p in range(2):
            mons = rest[1].split(',') if len(rest) > 1 and ':' in rest[1] else []
            rest = rest[2:] if mons else rest[1:]
            sp = tuple(sorted(T[ids[p]]['party'][int(m.split(':')[0])]['species'] for m in mons))
            s = 1.0 if (r == 1 and p == 0) or (r == 2 and p == 1) else 0.5 if r in (3, 5, 6) else 0.0
            a = out[(ids[p], sp)]; a[0] += s; a[1] += 1
    return out
rand, model = sets(sys.argv[1]), sets(sys.argv[2])
regret, n_tr, best_hit, rows = 0.0, 0, 0, []
for t in range(len(T)):
    opts = {sp: v[0] / v[1] for (tt, sp), v in rand.items() if tt == t and v[1] >= 200}
    if len(opts) < 2: continue
    best = max(opts.values()); uni = sum(v[0] for (tt, _), v in rand.items() if tt == t) / sum(v[1] for (tt, _), v in rand.items() if tt == t)
    mine = [(sp, v[1]) for (tt, sp), v in model.items() if tt == t]
    tot = sum(c for _, c in mine)
    exp = sum(opts.get(sp, uni) * c for sp, c in mine) / tot
    top = max(mine, key=lambda x: x[1])[0]
    n_tr += 1; regret += best - exp; best_hit += opts.get(top) == best
    rows.append((best - exp, best - uni, f"{T[t]['name']} #{T[t]['index']}", max(opts, key=opts.get), top))
print(f"{n_tr} trainers with a choice: mean regret model {regret / n_tr:.3f} vs random {sum(r[1] for r in rows) / n_tr:.3f}; "
      f"model's most common set is the best set for {best_hit / n_tr:.0%}")
for r in sorted(rows, reverse=True)[:15]: print(f"  {r[0]:.3f} (random {r[1]:.3f}) {r[2]:<18} best {r[3]}  model {r[4]}")
