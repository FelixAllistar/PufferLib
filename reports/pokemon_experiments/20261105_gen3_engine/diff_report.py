# Summarise an engine_diff.js / rescore.js output: agreement rate and the worst positions. Gen 3 rows flagged midturn
# (Showdown stopped at a mid-turn faint switch, before end-of-turn effects) are reported apart.
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1])]
ok = [r for r in rows if not r.get('crash')]
mid = [r for r in ok if r.get('midturn')]
full = [r for r in ok if not r.get('midturn')]
good = lambda r: r['hp'] <= 3 and r['cat'] <= 0.08 and r['boost'] <= 0.08
print(f"{len(rows)} positions, {len(rows) - len(ok)} engine crashes, {len(mid)} end in a mid-turn faint (scored apart)")
print(f"full turns: {sum(map(good, full))}/{len(full)} agree (mean HP within 3% of max, actives/status and boosts TVD <= 0.08)")
print(f"mid-turn faints: {sum(map(good, mid))}/{len(mid)} agree anyway")
for r in sorted(full, key=lambda r: -(r['hp'] / 100 + r['cat'] + r['boost']))[:int(sys.argv[2]) if len(sys.argv) > 2 else 15]:
    print(f"hp {r['hp']:5.1f}% cat {r['cat']:.2f} boost {r['boost']:.2f} | {r['m1']} vs {r['m2']} | "
          f"{r['p1']} vs {r['p2']} weather={r['weather']}")
