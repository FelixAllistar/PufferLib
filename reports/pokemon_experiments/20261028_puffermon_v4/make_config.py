#!/usr/bin/env python3
"""puffermon_v4 config (2026-10-28): the ladder run's config continued from its 2B checkpoint with every learner seat
drafting freely (env.pool_free_fraction 1) for 2B more steps. Usage (box repo root): make_config.py"""
import re
L = "reports/pokemon_experiments/20261028_ladder"; V = "reports/pokemon_experiments/20261028_puffermon_v4"
ck = L + "/checkpoints/pokemon/sweep_1793048711881_0014/0000001999896576.bin"
s = open(L + "/pokemon.ini").read(); s = s[s.index("[base]"):]
over = {"base": {"checkpoint_dir": V + "/checkpoints", "log_dir": V + "/logs", "load_model_path": ck},
        "env": {"pool_free_fraction": "1"}, "train": {"total_timesteps": "2000000000"}}
out, sec = [], None
for line in s.splitlines(True):
    m = re.match(r"\[(.+)\]", line.strip())
    if m: sec = m.group(1)
    k = line.split("=")[0].strip() if "=" in line else None
    if k and k in over.get(sec, {}): line = f"{k} = {over[sec].pop(k)}\n"
    out.append(line)
assert not any(over[x] for x in over), over
open(V + "/pokemon.ini", "w").write("# puffermon_v4 (2026-10-28): ladder-run 2B model -> +2B steps, every learner seat drafts freely (pool_free_fraction 1). Notes: README.md\n" + "".join(out))
print("ok")
