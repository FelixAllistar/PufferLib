#!/usr/bin/env python3
"""Which team does a checkpoint draft as a free-pick player? Drafts (argmax) against 6 of Metamon's competitive teams x 2
seeds, prints each distinct team with its count, and writes the most common one (drafted order, lead first) as a
Showdown export. Usage (box repo root): draft_team.py CHECKPOINT OUT.paste.txt"""
import sys, glob, shutil, tempfile, ctypes as C, collections
sys.path.insert(0, "ocean/pokemon/benchmarks")
from pathlib import Path
from evaluate_private import load_team, library
from private_view import PublicMechanics, ROOT
ck, out = sys.argv[1:]
rules = PublicMechanics(); species = {p["id"]: p["name"] for p in rules.data["species"]}; moves = {m["id"]: m["name"] for m in rules.data["moves"]}
lib = library(ROOT / "build/pokemon/private_adapter_mirror.so"); lib.bench_team.argtypes = [C.c_void_p, C.c_void_p]; lib.bench_team.restype = None
tmp = Path(tempfile.mkdtemp()); shutil.copy(ck, tmp / "w.bin")
with open(Path(ck).with_name("config.ini")) as src, open(tmp / "config.ini", "w") as dst:
    sec = None
    for line in src:
        s = line.strip()
        if s.startswith("["): sec = s[1:-1]
        k = s.split("=")[0].strip() if "=" in s else None
        if sec == "env" and k in ("learner_team_pool", "learner_team", "learner_lead"): line = f"{k} = None\n"
        dst.write(line)
seen = collections.Counter(); teams = {}
for t in sorted(Path("reports/pokemon_benchmarks/metamon_competitive_gen1ou").glob("*.gen1ou_team"))[:6]:
    opp = load_team(t.stem, rules, t)[0]
    for seed in (1, 2):
        h = lib.bench_create(str(tmp / "w.bin").encode(), 1)
        assert lib.bench_reset(h, 20261100 + seed, 0, opp) == 0
        d = (C.c_uint8 * 30)(); lib.bench_team(h, d); lib.bench_destroy(h)
        mons = [list(d)[i:i + 5] for i in range(0, 30, 5)]
        key = "; ".join(species[m[0]] + ": " + "/".join(moves[x] for x in m[1:] if x) for m in mons)
        seen[key] += 1; teams[key] = mons
shutil.rmtree(tmp)
for team, n in seen.most_common(): print(n, "x", team)
mons = teams[seen.most_common(1)[0][0]]
Path(out).write_text("\n\n".join(species[m[0]] + "".join(f"\n- {moves[x]}" for x in m[1:] if x) for m in mons) + "\n")
