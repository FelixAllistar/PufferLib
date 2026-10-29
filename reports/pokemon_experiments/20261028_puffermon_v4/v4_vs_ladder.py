#!/usr/bin/env python3
"""puffermon_v4 (ladder-run 2B + 2B full free-pick) vs the ladder-run 2B model, both piloting the same team (./puffer
match, 4096 games): the live Zapdos team and the old peak team. Results: v4_vs_ladder.txt."""
import glob, re, subprocess, sys
sys.path.insert(0, "ocean/pokemon/benchmarks")
from pathlib import Path
from evaluate_private import load_team
from private_view import PublicMechanics
rules = PublicMechanics()
V4 = glob.glob("reports/pokemon_experiments/20261028_puffermon_v4/checkpoints/pokemon/*/0000001999896576.bin")[0]
OLD = "reports/pokemon_experiments/20261028_ladder/checkpoints/pokemon/sweep_1793048711881_0014/0000001999896576.bin"
T = "reports/pokemon_experiments/20261028_round_robin/teams"
def sets(p):
    a = list(load_team(Path(p).stem, rules, Path(p))[0]); return "sets:" + "/".join(",".join(map(str, a[i:i + 5])) for i in range(0, 30, 5)), str(a[0])
for name in ["egg1_std_zapdos", "old_peak"]:
    t, lead = sets(f"{T}/{name}.paste.txt")
    out = subprocess.run(["./puffer", "match", "--headless", "--base.env_name=pokemon", "--base.eval_episodes=4096",
        f"--base.load_model_path={V4}", f"--base.load_enemy_model_path={OLD}", "--policy.hidden_size=1024", "--policy.num_layers=2",
        f"--env.learner_team={t}", f"--env.opponent_team={t}", f"--env.learner_lead={lead}", f"--env.opponent_lead={lead}",
        "--env.learner_team_pool=None", "--env.match_team_pool=None", "--env.native_league=None", "--env.pool_free_fraction=0",
        "--env.expert_fraction=0"], capture_output=True, text=True).stdout
    m = re.search(r"CUDA_EVAL env=pokemon score=(\S+) .* games=(\d+)", out)
    line = f"{name}\tv4 {100 * float(m.group(1)):.1f}% vs ladder-run 2B\t{m.group(2)} games" if m else f"{name}\tFAILED"
    print(line, flush=True); open("reports/pokemon_experiments/20261028_puffermon_v4/v4_vs_ladder.txt", "a").write(line + "\n")
