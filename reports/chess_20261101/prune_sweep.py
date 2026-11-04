"""Sweep checkpoint pruner (2026-11-04): keeps the final checkpoint (.bin, .magnet, .ini) of the top KEEP completed
trials by score and deletes every other trial checkpoint directory, so a 500-trial sweep fits the box's disk.
Trial n's directory is <checkpoints>/<env>/sweep_<time>_<n:04d>. Usage: prune_sweep.py HISTORY CKPT_ENV_DIR [KEEP=10]"""
import pathlib
import re
import shutil
import sys

history, root = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
keep_n = int(sys.argv[3]) if len(sys.argv) > 3 else 10
rows = [line.split() for line in history.read_text().splitlines() if re.match(r'^\d+ ', line)]
done = [(float(r[1]), int(r[0])) for r in rows if r[3] == '0']
keep = {n for _, n in sorted(done, reverse=True)[:keep_n]}
finished = {int(r[0]) for r in rows}
freed = 0
for d in root.glob('sweep_*_*'):
    m = re.search(r'_(\d{4})$', d.name)
    if not m or int(m.group(1)) not in finished:
        continue  # a trial still running
    n = int(m.group(1))
    if n not in keep:
        freed += sum(f.stat().st_size for f in d.rglob('*') if f.is_file())
        shutil.rmtree(d)
        continue
    bins = sorted(f for f in d.glob('*.bin'))
    for f in bins[:-1]:  # keep only the final step
        for g in d.glob(f.stem + '.*'):
            freed += g.stat().st_size
            g.unlink()
print(f'kept trials {sorted(keep)}; freed {freed / 1e9:.1f} GB')
