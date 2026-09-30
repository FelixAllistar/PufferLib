# Punisher Xray Electrical sweep — 2026-09-28

From `/home/felix/puffertank/pufferlib`, run:

```bash
./abyss_puffer sweep
```

`abyss_puffer` has been built with the repaired NPC model. Rebuild after changing
mechanics/data with `CUDA_HOME=/usr/local/cuda bash build.sh abyss abyss_puffer`.
The older `puffer` binary was not overwritten; use the command above to avoid
accidentally sweeping the previous mechanics. The full sweep has not been started.

## Manual normalization and sampling

- Success reward .60, speed bonus up to .25, room clear .05, loot .07,
  cache kill .01, hostile kill .003. Even their conservative positive sum .983
  fits under the learner's +1 clip. Death/timeout stays -1; step cost -.00025.
- `perf` retains completion/cache/survival dominance. Its bounded speed term
  increases from .001 to .05 so it can distinguish completed policies more
  reliably in FP32. One extra failure in a 10,000-run comparison costs more than
  the entire speed term. Always compare completion before interpreting speed.
- All 28 empirical T0 templates sampled uniformly. Removed oversampling of
  indices 3/9/11, which came from the old Dark fit.
- Weather sampling is 50/50 between 30% and 50% Electrical penalties for coverage;
  this is not a claim about live frequency. Evaluate finalists by strength too.
- Ship optimal 11880 m, falloff 2875 m, and NPC orbit ranges are unchanged.
  No forced approach or arbitrary closer-is-better reward was added.
- Fresh initialization (`load_model_path=None`); 24 trials, 100M target steps
  each, existing pruning retained. Base and fixed sweep step budgets agree.

## Search ranges

| Parameter | Range | Distribution |
|---|---|---|
| agents | 512–1024 | powers of two |
| horizon | 32–128 | powers of two |
| minibatch | 1024–4096 | powers of two |
| replay ratio | .5–2 | uniform |
| hidden size | 64–128 | powers of two |
| recurrent layers | 1–3 | integer uniform |
| learning rate | .00003–.001 | log normal |
| entropy coefficient | .0001–.01 | log normal |
| gamma | .995–.9995 | logit normal |

GAE .90, clipping .20, value coefficient 2, gradient norm 1.5 and momentum .95
remain fixed. Physics, ship stats, rewards and repair rates are not sweep variables.
Entropy still anneals to 1% of its initial value. All rollout/batch combinations
meet native divisibility constraints. These bounds target the local 3 GB GPU.

## Repair model and limits

The CSV contains 38 local repairers and 29 remote repairers across 107 NPCs.
Normalization now preserves layer plus HP/s and remote optimal/falloff; the
generator passes them into every spawned NPC. Current T0 repairers include
Hunter (12.5 shield/s), Pacifier (26 armor/s), Lucifer variants, Lucid Aegis/Escort
and remote-only Striking Damavik. Zero-repair NPCs stay zero.

Local repair runs continuously, is capped to the weather-adjusted layer maximum,
does not spill into another layer, and never revives a dead NPC. This is the
requested steady-rate approximation, not a simulation of individual rep cycles,
NPC capacitor or passive shield recharge.

Remote repair uses a continuous rate too. Each living healer selects one damaged
hostile ally, excluding itself, caches and the player. Selection maximizes missing
layer fraction weighted by range application; application is full through optimal,
half at optimal+falloff, and decays beyond. This is an explicit approximation of
target selection and faction cooperation, not measured NPC AI. Repair is applied
after movement and before suppressor/player damage each tick. New diagnostics:
`npc_local_repaired` and `npc_remote_repaired` (effective HP per episode).

The policy observation/action ABI is unchanged. Old checkpoints load, but old
completion claims were measured without NPC repair and need re-evaluation.

## Verification and starting baseline

- Native mechanics and ASan/UBSan tests passed, including layer/rate propagation,
  capping, dead-NPC exclusion, remote range/self exclusion and reward budget.
- Four Python checks cover every CSV repairer, reproducible catalog/header output,
  bad repair input, fixed sweep parameters and rollout shapes.
- Two 8,192-step native sweep smoke trials passed; artifacts are isolated under
  `/tmp/abyss-repair-sweep-smoke-_p12rvjy`. These only verify sweep execution.
- Maximum search shape (1024 agents, horizon 128, minibatch 4096, hidden 128,
  three layers, replay 2) completed a 131,072-step GPU training smoke with CUDA
  graphs/pipelining enabled. Output: `/tmp/abyss-sweep-max-shape-1cpxc8g8`.
- Existing checkpoint `1790591097599/0000000499974144.bin`: 522 completed episodes
  in the new sim, dashboard completion/survival .998, no timeouts, mean episode
  length 303.7 ticks (includes failures), mixed penalties and uniform templates.
  Full output: `data/punisher_repair_checkpoint_eval.txt`. No retraining preceded
  this check; this is a small baseline, not a live-parity or T1 qualification.

For finalists, pin the exact checkpoint and its hidden-size/layer settings, test
all templates at both strengths, and compare Hunter engagement range/time and
armor margin with the saved live run. Check the bot's assumed weather strength
against each live site. T1 is now selected in this same environment with `--env.filament_tier=1`.
The current shared ABI is 1,234 inputs; the checkpoint results above predate this
change. See [tier controls and weight migration](TIERS.md).
