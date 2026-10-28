"""Sample N teams from a Metamon team set dir that our engine plays exactly, excluding given team dirs.
Usage (box repo root, PYTHONPATH=ocean/pokemon/benchmarks): sample_teams.py CK SRC OUT N SEED EXCLUDE_DIR..."""
import sys, os, random, shutil, ctypes as C
from pathlib import Path
from evaluate_private import load_team, library
from private_view import PublicMechanics, ROOT
ck, src, out, n, seed, *exclude = sys.argv[1:]
n, seed = int(n), int(seed)
rules = PublicMechanics(); lib = library(ROOT / "build/pokemon/private_adapter_mirror.so")
lib.private_reset_mirror.argtypes = [C.c_void_p, C.c_uint64, C.c_int, C.c_void_p, C.c_void_p]; lib.private_reset_mirror.restype = C.c_int
h = lib.bench_create(os.path.abspath(ck).encode(), 1)
banned = set()
for d in exclude:
    for p in Path(d).iterdir():
        if p.name.endswith(('_team', '.paste.txt')):
            try: banned.add(bytes(load_team(p.stem, rules, p)[0]))
            except Exception: pass
files = sorted(Path(src).iterdir()); random.Random(seed).shuffle(files)
kept, seen, skipped = [], set(), 0
for p in files:
    try: arr = load_team(p.stem, rules, p)[0]
    except Exception: skipped += 1; continue
    key = bytes(arr)
    if key in banned or key in seen: continue
    if lib.private_reset_mirror(h, 1, 0, arr, arr) < 0 or lib.private_reset_mirror(h, 2, 1, arr, arr) < 0: skipped += 1; continue
    seen.add(key); kept.append(p)
    if len(kept) == n: break
os.makedirs(out, exist_ok=True)
for i, p in enumerate(kept): shutil.copy(p, f"{out}/t{i:03d}.gen1ou_team")
print("kept", len(kept), "skipped", skipped, "examined", files.index(kept[-1]) + 1)
