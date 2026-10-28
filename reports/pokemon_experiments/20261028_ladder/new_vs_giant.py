#!/usr/bin/env python3
"""Ladder run final (giant 4B + 2B on 4,150 teams) vs the giant 4B model, both piloting the same team (our engine,
./puffer match, 4096 games per team): the current ladder team and the old peak team. Results: new_vs_giant.txt."""
import glob, re, subprocess, sys
sys.path.insert(0, "ocean/pokemon/benchmarks")
from pathlib import Path
from evaluate_private import load_team
from private_view import PublicMechanics
rules = PublicMechanics()
NEW = "reports/pokemon_experiments/20261028_ladder/checkpoints/pokemon/sweep_1793048711881_0014/0000001999896576.bin"
OLD = glob.glob("reports/pokemon_experiments/20261027_giant/checkpoints/pokemon/*/0000003999793152.bin")[0]
D = "reports/pokemon_experiments/20261027_showdown_vs_metamon"
def sets(p):
    a = list(load_team(Path(p).stem, rules, Path(p))[0]); return "sets:" + "/".join(",".join(map(str, a[i:i + 5])) for i in range(0, 30, 5)), str(a[0])
for name, paste in [("current ladder team", f"{D}/ladder_rest_snorlax.paste.txt"), ("old peak team", f"{D}/giant4b_freepick.paste.txt")]:
    t, lead = sets(paste)
    out = subprocess.run(["./puffer", "match", "--headless", "--base.env_name=pokemon", "--base.eval_episodes=4096",
        f"--base.load_model_path={NEW}", f"--base.load_enemy_model_path={OLD}", "--policy.hidden_size=1024", "--policy.num_layers=2",
        f"--env.learner_team={t}", f"--env.opponent_team={t}", f"--env.learner_lead={lead}", f"--env.opponent_lead={lead}",
        "--env.learner_team_pool=None", "--env.match_team_pool=None", "--env.native_league=None", "--env.pool_free_fraction=0",
        "--env.expert_fraction=0"], capture_output=True, text=True).stdout
    m = re.search(r"CUDA_EVAL env=pokemon score=(\S+) .* games=(\d+)", out)
    line = f"{name}\tnew {100 * float(m.group(1)):.1f}% vs giant 4B\t{m.group(2)} games" if m else f"{name}\tFAILED"
    print(line, flush=True); open("reports/pokemon_experiments/20261028_ladder/new_vs_giant.txt", "a").write(line + "\n")
