# Pokémon experiment checkpoint — 2026-10-17

We have a fixed-team H64/L2 BF16 policy learning battle play against a frozen
14-policy OU roster and current-policy self-play. The expert models are useful
training opponents, but their current strength and whether they are exhausted
have not been established by a fresh head-to-head evaluation. The new experiment
lets those opponents learn again while retaining the existing pool as a baseline.

## Fixed-pool results

All step counts below are counted agent transitions. With the 50/50 concurrent
environment mixture, 75% are learner transitions. These are fresh seed-73 runs
with `load_model_path=None`; they are not continuations of one another.

| Run / experiment | Precision | Counted steps | Oak argmax | Sampled | Games per mode |
| --- | --- | ---: | ---: | ---: | ---: |
| Unrestricted/core curriculum `1792155346981` | BF16 | 499,908,608 | 48.83% | 7.81% | 384 |
| Original fixed-team sweep winner `sweep_1792079639324_0223` | FP32 | 33,554,432 | 60.94% | 18.49% | 384 |
| BF16 sweep trial 163 | BF16 | 33,554,432 | 55.47% | 16.67% | 384 |
| Exact training reproduction `1792196525320` | BF16 | 33,554,432 | 50.78% | 16.93% | 384 |
| Same checkpoint, larger evaluation | BF16 | unchanged | 52.73% | 16.11% | 3,072 |
| Fresh repeat `1792199241996` | BF16 | 33,554,432 | 52.78% | 17.79% | 3,072 |
| Doubled budget `1792201244232` | BF16 | 67,108,864 | 41.67% | 10.03% | 384 |
| Longer budget `1792201557856` | BF16 | 671,088,640 | 64.32% | 14.97% | 384 |
| Doubled again `1792210230017` | BF16 | 1,342,177,280 | 62.11% | 13.02% | 384 |

The 499.9M unrestricted/core run had no frozen experts and is a different task.
The selected BF16 trial, its reproduction, and the fresh repeat above have
byte-identical weights (SHA-256 `9bbf435745b857d538849be0e850015cc8c58f3eb80e6f40bb932e2c3ca275b1`).
Their different scores demonstrate Oak evaluation variability. Oak's native
search seeds itself independently of our recorded battle/draft seed. The longer
fresh runs also stretch the cosine learning-rate schedule over their full budget.

The fixed sets are Starmie lead (Recover / Thunder Wave / Blizzard / Psychic),
Exeggutor (Sleep Powder / Psychic / Explosion / Stun Spore), Rhydon (Earthquake /
Rock Slide / Body Slam / Substitute), Chansey (Sing / Seismic Toss / Soft-Boiled /
Thunder Wave), Snorlax (Body Slam / Reflect / Rest / Self-Destruct), and Tauros
(Body Slam / Hyper Beam / Blizzard / Earthquake). Catalog IDs:
`421,366,389,395,512,445`; lead species ID 121.

The tuned PPO settings are preserved in `baseline_runs.json` and
`experiment.json`: LR 0.00297837891; horizon 32; minibatch 8192; gamma
0.999899983; lambda 0.899999976; entropy coefficient 0.020000007; replay ratio
3.71047497; clip coefficient 0.12561436; value coefficient 0.52888006; maximum
gradient norm 0.338345766; momentum 0.95. Learning rate cosine-anneals to 10%.
Rewards are terminal wins/losses; shaping, forced cores and snapshot resets are off.

## Round-robin experiment

League: `leagues/pokemon/rr50m_bf16_20261017` on the GPU host. The new master
starts from the highest observed long-budget checkpoint, the 671M/64.32% run.
The 1.34B/62.11% model is retained. This choice is based on observed scores,
not a statistically confirmed strength difference. The other 13 members start
from the named specialist checkpoints used in the frozen baseline. Every member
is trainable. The old unrestricted master remains archived in the original pool.

One complete round means all 14 policies train sequentially for 50,331,648
counted steps each (96 complete rollouts at 16,384 agents x horizon 32), then
start the next round. Each stint has 50% current self-play environments and
50% environments against the other 13 current policies. A completed member is
published immediately, so later members face its new version. Peer weights stay
fixed during each individual stint. Master retains its exact fixed catalog sets;
the 13 specialists retain their species/lead constraints and learn moves freely.

Target: 50 complete rounds, 700 member stints, 35,232,153,600 counted steps and
26,424,115,200 learner steps overall. Each member receives 2,516,582,400 counted
steps over the planned experiment. League rollouts use one synchronous buffer
and four CPU workers, as required by the existing league scheduler; this differs
from the two-buffer fixed-pool sweep. The executable is a hash-verified snapshot
of the existing BF16 `./puffer`, not the older FP32 benchmark executable.

Every stint explicitly sets `oak.enabled=0`, disables native end-of-training
evaluations and periodic checkpoints, and retains initial/final weights and the
published member version. No `--evaluate` round-end tournament is scheduled.
Checkpoints, opponent manifests, command plans, live logs, and durable state
remain available for later evaluation and comparison.

Optimizer and recurrent state restart for each weight-loaded stint. Retrying an
interrupted stint uses its saved starting weights and peer snapshot; completed
members are preserved. An invocation's `--rounds` counts additional rounds,
including an unfinished current round, so resume with `50 - state.round` to keep
the original total target of 50. Do not accidentally schedule another 50 after
part of the experiment is complete.

## Evaluation effort

Use `--oak.games=64` for coarse screening, 128 for routine training comparisons,
and 1024 for confirming selected changes. Games are per team per action mode;
there are three teams and both argmax/sample modes. Keep search budget at 4096
for comparable opponent strength. Evaluations can be disabled with
`--oak.enabled=0` when they do not answer the current experiment's question.
The round-robin job uses that setting explicitly. The ordinary config retains
routine evaluation for normal independent training runs.

Evaluate selected saved round-robin checkpoints later against the unchanged Oak
panel and/or the original frozen pool. A higher mixed training score alone does
not establish external improvement. Confirm promising results over additional
games/seeds before selecting hyperparameters or declaring the league stronger.

`baseline_runs.json` records checkpoint hashes, budgets, model/configuration
details, and exact reports; `roster.json` records every new initializer;
`experiment.json` records the scheduled budgets and controls.

## Manual Oak check after the master's round-11 update

While the league continued (10 full rounds complete, round 11 in progress), the
immutable `members/master/r000011-a001/weights.bin` checkpoint was evaluated
against the same three-team Oak panel, seed 20261015 and search budget 4096.
The manual evaluation used 128 games/team/mode and three CPU workers alongside
training, finishing in 155.25 seconds. Automatic league evaluations remain off.

- Argmax: 238 wins, two draws, 144 losses; **62.24%** over 384 games.
- Sampled: 86 wins, two draws, 296 losses; **22.66%** over 384 games.
- Argmax by team: zam_egg_mie 54.69%, jynx_mie_don 65.23%, gar_egg_zap 66.80%.
- Model SHA-256: `b9140ec6c935ead7598a10cda13958a3114ecd70486419d1e18868f358e496e7`.

The master has received another 553,648,128 counted steps across 11 league
stints, for 1,224,736,768 cumulative counted steps including its 671M initializer.
Greedy performance is close to the initializer's short-panel 64.32% result;
sampled performance is higher than its 14.97%. These noisy single evaluations
do not establish a reliable greedy gain or a plateau. The exact summary is
saved in `round11_oak_summary.json`; the host's full match records are under
`leagues/pokemon/rr50m_bf16_20261017/evaluations/oak-master-r000011-128/`.

## Manual Oak check after the master's round-13 update

About 1 hour 32 minutes after the round-11 evaluation, the league had completed
12 full rounds and 10/14 member updates in round 13. The round-13 master was
evaluated with the same panel, 128 games/team/mode, search budget 4096, seed
20261015 and three evaluation workers alongside the continuing training job.

- Argmax: 54 wins, two draws, 328 losses; **14.32%** over 384 games.
- Sampled: 39 wins, zero draws, 345 losses; **10.16%** over 384 games.
- Argmax by team: zam_egg_mie 9.38%, jynx_mie_don 7.81%, gar_egg_zap 25.78%.
- Model SHA-256: `1eabb62843aa716020d9d5cf0406cb33a1d192f63f8a7cb43f522afb278ed351`.
- The evaluation finished in 92.77 seconds; exact summary:
  `round13_oak_summary.json`.

This is a substantial regression against Oak relative to round 11, far beyond
the few percentage points of variability seen in previous evaluations. The cause
has not been identified. Read-only checks verified finite weights in rounds
11/12/13, matching architecture, exact team/lead/moves, unchanged adapter and
Oak version, and matching evaluation budget. Saved training settings differ
only in expected run IDs, parent checkpoints and evolving opponent manifests.
Weight L2 norms were 142.76, 144.92 and 146.58 respectively; no non-finite
weights were found. This does not establish which training behavior caused the
regression. The original and good round-11 checkpoints remain preserved.

The round-13 master has 1,325,400,064 cumulative counted steps. Round-11 and
round-13 model/config snapshots are also backed up locally under
`saved/pokemon/rr50m_bf16_20261017/master/`. Training was left running as requested;
no learning settings or automatic evaluation settings were changed for this check.

## Round-12 check and full round-13 roster evaluation

To locate the master's abrupt Oak regression, its immutable round-12 snapshot
was evaluated with the same 128 games/team/mode, budget 4096 and seed 20261015:

- Round 12: **59.77% argmax**, **22.01% sampled** (384 games per mode).
- Model SHA-256: `400efc0310c07a7dbdd70543da2ffbe6e863a063d38a85f58b9b3cb4c5931ab7`.

Together with rounds 11 and 13, this places the large observed Oak regression
between the master's round-12 and round-13 updates. It does not identify a cause.

A separate native CPU argmax match compared these exact two snapshots directly:

```bash
./build/pokemon/viewer eval \
  leagues/pokemon/rr50m_bf16_20261017/members/master/r000012-a001/weights.bin \
  leagues/pokemon/rr50m_bf16_20261017/members/master/r000013-a001/weights.bin \
  --games=128 --seed=20261015 --deterministic
```

Round 12 scored 72 wins, three draws and 53 losses (**57.42%**); one game
timed out. This small direct match is much closer than the Oak comparison.
It suggests the Oak drop need not imply an equally large loss against every
opponent, but does not prove matchup-specific forgetting or policy cycling.
Only the CPU viewer was built (`make -C ocean/pokemon -o engine viewer`);
the running training binary and settings were not changed.

The full roster comparison uses all 14 immutable `r000013-a001` member
snapshots, plus the round-12 master. It reuses the already completed round-13
master report. Inputs and hashes are pinned in
`round13_roster_eval_manifest.json`; the batch runs one model at a time with
three Oak workers total alongside training. Each model uses the same three
Oak teams, both action modes, 128 games/team/mode, search budget 4096 and seed
20261015. Per-match records and incremental JSON/CSV summaries are retained at
`leagues/pokemon/rr50m_bf16_20261017/evaluations/roster-round13-128/` on the host.

The original specialist reference scores come from
`reports/pokemon_benchmarks/20261014_all_roster/summary.json`; all 14 checkpoint
hashes in that archive match `baseline_runs.json`'s original frozen pool.
Those reports used battle seed 20261014 rather than 20261015, so differences
are descriptive comparisons, not paired-seed causal estimates. The archived
unrestricted master is not the current fixed-team master's initializer: use
the 671M initializer's 64.32% argmax / 14.97% sampled baseline for this master.

The roster batch finished in 25.08 minutes, with all 15 comparisons complete
and no failures (14 newly evaluated snapshots plus the cached round-13 master).
Scores below give half credit for draws. The starting column is the archived
specialist score, except for the master, where it is the actual 671M initializer.

| Round-13 policy | Starting argmax | Round-13 argmax | Round-13 sampled |
| --- | ---: | ---: | ---: |
| gar_egg_zap | 20.70% | 42.84% | 11.72% |
| gar_egg_mie | 9.38% | 42.32% | 8.33% |
| mie_egg_cloy | 9.24% | 41.93% | 7.16% |
| zam_egg_cloy_don | 26.43% | 41.02% | 10.16% |
| mie_egg_don | 35.16% | 38.67% | 5.73% |
| jynx_egg_zap | 11.72% | 32.55% | 10.81% |
| zam_egg_mie | 13.28% | 31.12% | 6.51% |
| gar_egg_mie_zap | 17.45% | 31.12% | 2.34% |
| zam_egg_zap | 11.20% | 26.95% | 7.94% |
| jynx_mie_don | 13.15% | 23.31% | 2.08% |
| zam_egg_gar | 11.72% | 17.19% | 1.56% |
| master | 64.32% | 14.32% | 10.16% |
| jynx_egg_gar_zap | 11.59% | 8.85% | 0.00% |
| mie_egg_zap | 13.93% | 2.60% | 1.30% |

Eleven of 13 specialists scored higher than their archived reference. The
unweighted specialist mean argmax score increased from 15.77% to 29.27%.
Small differences should not be overinterpreted at this sample size and with
different battle seeds. Large gains for several specialists coexist with a
large master regression; this is not evidence of either uniform league-wide
collapse or uniform improvement. No round-13 policy reached the round-12
master's 59.77% in this panel.

Exact aggregate counts, model/config hashes and report paths are saved in
`round13_roster_oak_summary.json`, with a compact CSV in
`round13_roster_oak_results.csv`. The round-12 standalone report is also saved
as `round12_oak_summary.json`; its weights/config are backed up locally under
`saved/pokemon/rr50m_bf16_20261017/master/r000012-a001/`.

A fresh round-13 master evaluation after the roster batch used the identical
checkpoint and protocol (the cached result above was not replaced):

- Repeat argmax: 44 wins, four draws, 336 losses; **11.98%**, 384 games.
- Repeat sampled: 47 wins, zero draws, 337 losses; **12.24%**, 384 games.
- Elapsed: 92.11 seconds; exact report:
  `round13_master_repeat_oak_summary.json`.

Both round-13 master evaluations are poor, confirming that the first low score
was not merely a few-point evaluation fluctuation. Combining the two gives
101 score-equivalent wins out of 768 argmax games (**13.15%**) and 86/768 sampled
(**11.20%**). This corroborates the external regression without establishing its
training cause. Round-12 and all other published checkpoints remain preserved;
training and its automatic-evaluation settings were left unchanged.

## Manual check of the round-16 master

At the next check, 15 complete league rounds and 9/14 updates in round 16
were complete; both controller and native trainer were still running. The
already published round-16 master was evaluated with the unchanged three-team
Oak panel, 128 games/team/mode, budget 4096, battle seed 20261015 and three
evaluation workers alongside training.

- Argmax: 267 wins, zero draws, 117 losses; **69.53%** over 384 games.
- Sampled: 105 wins, zero draws, 279 losses; **27.34%** over 384 games.
- Argmax by opponent: zam_egg_mie **62.50%**, jynx_mie_don **68.75%**,
  gar_egg_zap **77.34%**.
- No timeouts in either action mode; elapsed 157.37 seconds.
- Checkpoint SHA-256:
  `6ad3f9e49e85a0c50f42be313b684daa5dccaff4044b6559c5652d1d5f033e1a`.
- Exact report: `round16_oak_summary.json`; full host records:
  `leagues/pokemon/rr50m_bf16_20261017/evaluations/oak-master-r000016-128/`.

The round-16 result is substantially above both low round-13 evaluations and
shows the earlier Oak collapse did not persist through this checkpoint. Its
69.53% argmax / 27.34% sampled are the highest short-panel master scores
recorded in these notes. The argmax difference from the initializer's 64.32%
is promising, but requires repeat or higher-game confirmation before claiming
a reliable gain. Rounds 14 and 15 have not been evaluated here, so the exact
recovery point is unknown; the cause of the round-13 regression remains unknown.

The master has now received 805,306,368 additional counted steps in the league,
or 1,476,395,008 including its 671M initializer. Its exact weights and config
were backed up locally under
`saved/pokemon/rr50m_bf16_20261017/master/r000016-a001/`. The training run,
its binary/configuration and disabled automatic evaluations were not changed.

## Manual check of the round-22 master

At this check, 21 complete league rounds and 13/14 member updates in round 22
were complete. Both league controller and native trainer were running. The
latest published master at the start of the check was the immutable round-22
snapshot; it was evaluated using the unchanged panel, 128 games/team/mode,
budget 4096, battle seed 20261015 and three CPU workers alongside training.

- Argmax: 132 wins, three draws, 249 losses; **34.77%** over 384 games.
- Sampled: 68 wins, one draw, 315 losses; **17.84%** over 384 games.
- Argmax by opponent: zam_egg_mie **44.14%**, jynx_mie_don **43.36%**,
  gar_egg_zap **16.80%**.
- One timeout per action mode; elapsed 126.22 seconds.
- Checkpoint SHA-256:
  `e84020829579ab045d3ef589fe399ce29ab614bb4d4ffc9b07416510f1a82924`.
- Exact report: `round22_oak_summary.json`; full host records:
  `leagues/pokemon/rr50m_bf16_20261017/evaluations/oak-master-r000022-128/`.

This is another substantial external-score drop from round 16's 69.53%
argmax / 27.34% sampled. The same evaluation protocol was retained; the
largest matchup decline was against gar_egg_zap (77.34% to 16.80%). The
observed recovery at round 16 therefore did not establish sustained improvement
through round 22. The measured sequence is volatile, not monotonically improving;
the training cause of these changes remains undetermined. Rounds 17 through 21
have not been evaluated here.

The round-22 master has 1,778,384,896 cumulative counted steps, including its
671M initializer. Its weights/config are backed up locally under
`saved/pokemon/rr50m_bf16_20261017/master/r000022-a001/`; the stronger observed
round-16 snapshot and all other published versions remain preserved. Training,
its configuration/binary and disabled automatic evaluations were not changed.

## Manual check of the round-26 master

At the start of this check, 25 full rounds and 12/14 member updates in round 26
were complete; the controller and native trainer were running. The latest
published master was round 26. Its immutable snapshot was evaluated with the
unchanged three-team Oak panel, 128 games/team/mode, budget 4096, battle seed
20261015 and three CPU workers alongside training.

- Argmax: 286 wins, zero draws, 98 losses; **74.48%** over 384 games.
- Sampled: 131 wins, one draw, 252 losses; **34.24%** over 384 games.
- Argmax by opponent: zam_egg_mie **69.53%**, jynx_mie_don **73.44%**,
  gar_egg_zap **80.47%**.
- No timeouts in either mode; elapsed 149.67 seconds.
- Checkpoint SHA-256:
  `440b4dd80bebcc01fc491fdd6b1b9bed046953936e7d597c2207f2b0a6335b40`.
- Exact report: `round26_oak_summary.json`; full host records:
  `leagues/pokemon/rr50m_bf16_20261017/evaluations/oak-master-r000026-128/`.

This is a substantial recovery from round 22's 34.77% argmax / 17.84% sampled.
Both round-26 aggregate scores are the highest short-panel master results
recorded in these notes, exceeding round 16's 69.53% / 27.34%. This single
short-panel result is promising, not confirmation of a reliably superior
checkpoint or steady improvement. The previous large declines remain relevant;
the cause of the observed swings is undetermined. Rounds 23 through 25 have
not been evaluated here.

The round-26 master has 1,979,711,488 cumulative counted steps including its
671M initializer. Its exact weights and config were backed up locally under
`saved/pokemon/rr50m_bf16_20261017/master/r000026-a001/`, with checkpoint and
config hashes verified. Training and disabled automatic evaluation settings
were left unchanged; previous published snapshots remain preserved.

## Manual check of the round-29 master

At the start of this check, 28 full rounds and 2/14 member updates in round 29
were complete; both controller and native trainer were running. The published
round-29 master was evaluated with the unchanged three-team Oak panel,
128 games/team/mode, budget 4096, battle seed 20261015 and three CPU workers
alongside training.

- Argmax: 262 wins, one draw, 121 losses; **68.36%** over 384 games.
- Sampled: 135 wins, one draw, 248 losses; **35.29%** over 384 games.
- Argmax by opponent: zam_egg_mie **60.94%**, jynx_mie_don **73.83%**,
  gar_egg_zap **70.31%**.
- One argmax timeout, no sampled timeouts; elapsed 146.33 seconds.
- Checkpoint SHA-256:
  `492d8f76ecca6a4afcf95636d34463d4fd0d0643f67c857ac141e0894a5ee386`.
- Exact report: `round29_oak_summary.json`; full host records:
  `leagues/pokemon/rr50m_bf16_20261017/evaluations/oak-master-r000029-128/`.

The argmax result is 6.12 percentage points below round 26's 74.48%, and
close to round 16's 69.53%; it is not a collapse comparable to round 22's
34.77%. A repeat or larger evaluation would be needed to confidently assess
the smaller round-26 versus round-29 gap. The sampled score is slightly above
round 26's 34.24%, the highest observed sampled result in these notes, but
that 1.04-point difference should not be treated as a demonstrated improvement.
Rounds 27 and 28 have not been evaluated here.

The round-29 master has 2,130,706,432 cumulative counted steps including its
671M initializer. Its weights/config were backed up locally under
`saved/pokemon/rr50m_bf16_20261017/master/r000029-a001/`, with both hashes
verified. Previous snapshots remain preserved; training and disabled automatic
evaluation settings were unchanged.

## Manual check of the round-31 master

At the start of this check, 30 full rounds and 7/14 member updates in round 31
were complete; controller and native trainer were still running. The published
round-31 master was evaluated against the unchanged three-team Oak panel,
128 games/team/mode, budget 4096, battle seed 20261015 and three CPU workers
alongside training.

- Argmax: 241 wins, three draws, 140 losses; **63.15%** over 384 games.
- Sampled: 103 wins, two draws, 279 losses; **27.08%** over 384 games.
- Argmax by opponent: zam_egg_mie **55.08%**, jynx_mie_don **53.91%**,
  gar_egg_zap **80.47%**.
- Three argmax timeouts, no sampled timeouts; elapsed 140.24 seconds.
- Checkpoint SHA-256:
  `a9cdb2111836b4746398910bfa78065ae271f161a64491d90e56186694936095`.
- Exact report: `round31_oak_summary.json`; full host records:
  `leagues/pokemon/rr50m_bf16_20261017/evaluations/oak-master-r000031-128/`.

Both modes scored lower than round 29's 68.36% argmax / 35.29% sampled.
The argmax change is 5.21 percentage points; sampled declined by 8.20 points.
This is not a return to the much lower round-13/22 scores, but neither does it
show continued improvement over the recent evaluations. Repeated or larger
panels would be needed to assess the smaller changes confidently. Round 26
retains the highest observed master argmax score (74.48%); all evaluated
snapshots remain preserved. Round 30 has not been evaluated here.

The round-31 master has 2,231,369,728 cumulative counted steps including its
671M initializer. Exact weights/config were backed up locally under
`saved/pokemon/rr50m_bf16_20261017/master/r000031-a001/`, with both hashes
verified. Training and disabled automatic evaluation settings were unchanged.

## Information audit and external baseline shortlist

### Agreed next-step order and deferred experiment fork

1. Implement and validate a normal-player-information Gen1 evaluation, first
   against a heuristic and then the released Metamon `TaurosV0` opponent.
   Establish the current policy's external baseline before changing training.
2. Use that fair benchmark to guide a new hyperparameter sweep; the old
   short, fixed-pool settings are not assumed optimal for this training regime.
3. Only then investigate the remaining training issues and choose the next
   experiment branch. Deferred questions: is retuning the existing sequential
   procedure sufficient, or should all league members train concurrently with
   continuous optimizer/LR state? If concurrent training is considered, examine
   per-policy versus shared LR/optimizer state, rollout/minibatch allocation,
   and effective per-policy batch/update budgets. These are open questions,
   not an agreed design or a conclusion that parameters/LR must be shared.

The user explicitly requested deferring research/design of that final fork.
Do not start sweeps, change the running league, or investigate joint-training
minibatch mechanics as part of implementing the fair evaluation.

The existing Oak scores are explicitly asymmetric-information stress tests,
not measurements against a normal-information Showdown player. In
`bridge.zig:pk_benchmark_state`, the adapter copies the complete native battle
into Oak's state. `oak_benchmark.py` then searches that state, including
unrevealed opposing species/moves and exact stats/HP/PP. Our learned player
still receives its private native observation. Oak chooses before
`bench_step` invokes the learner, so this does not mean it is handed the
learner's already-selected action for the current turn.

Upstream [Oak](https://github.com/pokemon-labs/oak) explicitly implements
perfect-information search. Our wrapper has no hidden-information toggle.
A fair Oak-based opponent would require public-history tracking, inference or
sampling of possible hidden states, and aggregation of searches over those
states. Zeroing unknown fields is not a valid substitute for that layer.
Our `eval=fp` is a Foul Play-style static evaluation inside Oak, not the
complete Foul Play bot and its inference machinery. Existing Oak reports
should retain their original protocol label and never be silently mixed with
future normal-information scores.

Primary-source shortlist checked online:

- [Metamon](https://github.com/UT-Austin-RPL/metamon): explicitly supports
  Gen1OU, provides pretrained agents including Gen1 specialist `TaurosV0`
  and multigeneration `Kakuna`, plus graded heuristic baselines such as
  `Grunt`, `GymLeader` and `PokeEnvHeuristic`. Its reported human ladder
  achievements are author claims, not evaluations of our policy.
- [Foul Play](https://github.com/pmariglia/foul-play): documents singles support
  across generations. Its [search implementation](https://github.com/pmariglia/foul-play/blob/main/fp/search/main.py)
  searches multiple prepared/sampled battle hypotheses and combines their
  results. Its Rust engine exposes a `gen1` build feature. Promising independent
  search-based opponent; not a drop-in replacement for our native adapter.
- [poke-env](https://poke-env.readthedocs.io/en/stable/): provides Random,
  MaxBasePower and SimpleHeuristics players. Upstream warns that older-generation
  support can have gaps; verify Gen1-specific behavior rather than assuming it.
- [Showdown's own RandomPlayerAI](https://github.com/smogon/pokemon-showdown/blob/master/sim/tools/random-player-ai.ts)
  randomly samples legal moves/switches; useful as a smoke test, not a strong
  competitive standard. The search did not identify an official Smogon
  championship-strength reference bot; Foul Play and Metamon are independent
  projects, not official Smogon agents.

The initial audit recommended the following evaluation work (implementation
progress is recorded below): establish a
player-information-only Gen1OU bridge and test against a simple heuristic
before a released Metamon Gen1 policy. Preserve our policy's exact teams and
validate generation/rules, legal-action mapping and observation reconstruction.
Neither Metamon nor Foul Play currently plugs directly into this native setup.
A key no-leak test is that changing only unrevealed opposing information must
not change the opponent's observation/input history. Keep perfect-info Oak as
a separate stress test; defer new training sweeps or a custom Oak belief wrapper
until a fair external benchmark is working. This audit changed no training or
evaluation implementation/settings and started no new training experiment.

### Player-information evaluator implementation, October 18

Added an evaluation-only private-observation C bridge and Python external-bot
runner, separate from Oak. Neither the live trainer/config nor league settings
were changed. Initial baseline uses the backed-up round-26 fixed-team master
(best observed Oak argmax snapshot, not a claim that round 26 is best in general),
SHA-256 `440b4dd80bebcc01fc491fdd6b1b9bed046953936e7d597c2207f2b0a6335b40`.
Both seats alternate; team panel and native 512-update draw cap match the old
benchmark, but the opponent protocol is now `native-private-v1`.

The first opponent is **our own** deliberately simple type/STAB/power/accuracy
heuristic. This is an adapter sanity test, not a released competitive bot or
evidence of strong play. Against it, round 26 scored 365W/0D/19L in 384 games
(95.05%), 128 games/team, seed 20261018, no timeouts; elapsed 146.16 seconds.
Team scores: zam_egg_mie 116/128, jynx_mie_don 121/128, gar_egg_zap 128/128.
Exact records: `private-heuristic-round26-128/summary.json`.

The actual external model is Metamon **TaurosV0 checkpoint 62**, upstream
revision `0a00a759c9a4382a2877088d828302ec294a05a5` (Metamon 1.6.0), model
SHA-256 `3f55b698bd19f189a465797c7bd1fe49c75884de701ab91ad156e6bac70b8ba1`.
Inference uses isolated CPU PyTorch 2.8.0+cpu, AMAGO 3.4.0, the upstream
poke-env 0.8.3.3 fork. Our native policy still runs in C; no Torch migration
or training precision change occurred. The model strictly matched all 510
checkpoint keys, including 62,068,951 state-dict elements (parameters/buffers).
The local CPU attention backend preserves upstream window=(32,0) and BF16
Q/K/V rounding. It is not claimed bit-identical to CUDA FlashAttention.

Qualification checks so far: C hidden-reserve-moves mutation leaves the
observer's private observation identical; mask and phase checks pass. Ten
stdlib projection/action-mapping tests pass. Six optional integration tests
pass for species stats, upstream schema/order, cached/full attention and context
window, feature parity with an upstream replay-Pokemon fixture, and legal
move/switch mapping against upstream BattleOrder conversion.
A two-game Metamon smoke test completed without illegal actions or
timeouts (our learner lost both); that tiny sample is not a strength estimate.
All these evaluations ran locally on the backed-up round-26 checkpoint, not
on Vast or the latest league snapshot. The user objected to this unannounced
execution location and the homemade heuristic's lack of benchmark value.
The local 24-game Metamon run was stopped with SIGTERM after 12 completed games;
`private-tauros-round26-8/games.jsonl` retains those partial records. No complete
summary or benchmark score should be inferred from that interrupted run.
The user's subsequent instruction authorizes evaluating the best measured
round against the released Metamon opponent **on Vast**. Do not restart local
evaluation. A fresh host audit found round 26 remains the highest recorded
Oak argmax master result (74.48%); later rounds through published master 36
had no saved external scores. This is selection by the existing measured
criterion, not proof that round 26 is best against Metamon or among untested
rounds. Exact ranking/coverage/checkpoint hash: `metamon_round_selection.json`.
GPU inference is set up in the separate `metamon-eval-env-cu128` environment
on Vast (PyTorch 2.8.0+cu128). CUDA availability and matrix-multiply checks
passed on the RTX 5060 Ti. The private adapter was linked against the host's
existing engine archive without rebuilding it. C privacy checks, all ten
projection tests and all seven integration tests (including CUDA cached/full
attention parity) passed on the host. The released checkpoint's strict
510-key/state-element validation also passed there.

The evaluation launcher is `pokemon:metamon-round26` in tmux. It runs a
six-game CUDA smoke test, followed on success by 128 games/team (384 total)
against sampled TaurosV0, with our selected round-26 policy in argmax mode,
seed 20261018. Main output directory on the host:
`leagues/pokemon/rr50m_bf16_20261017/evaluations/metamon-master-r000026-128-cuda-20261018/`.
The live training environment/config/binary are untouched. The shared GPU may
reduce training throughput while evaluation runs; its learning settings and
schedule are not changed. This is not a new sweep or training experiment.

Vast smoke completed all six games without illegal actions or timeouts in
22.37 seconds. Its win rate is not used as a strength estimate. The full
384-game run started successfully and is saving completed games incrementally;
do not report its partial running score as a completed benchmark. A separate
same-model CPU/CUDA probability comparison on the public Starmie fixture
passed (maximum absolute difference 5.96e-7). Sampled CPU/CUDA game traces
are not required to match, since their random-number streams differ.
After setup, the live `puffer` executable still hashes to
`07849fd021b6534648cdbcddf98375ae855a9e31c12b5f9829c607d0a2f93a78`.

Limitations: native HP quantization differs from Showdown display; compact
observations require reconstruction of public PP/effect ordering and do not
provide a complete Showdown event stream. Final Metamon clipped probabilities
are conditioned on public legal actions (upstream clipping adds a floor even
to masked choices). No silent random fallback. Native rules/draw cap and fixed
teams apply; no ladder/Elo claims. Do not treat scores as a fully qualified
Showdown benchmark or wire them into sweeps before further validation.

Early smoke attempts are preserved, including failed loader/projection setup
attempts. They are not counted as completed evaluations. The planned order
remains: finish/qualify this evaluation, then consider a new fair-objective
sweep. Joint-training/LR/minibatch research remains explicitly deferred.

`original_pool_provenance.json` records each frozen model's last stage budget,
seed, team, and parent checkpoint. Each original model's last stage was
49,938,432 counted steps; this does not count its earlier initialization and
previous league stages. `state_after_start.json` and `first_native_plan.json`
capture the new league's saved state and the actual first native command.

### October 18: completed external benchmark and next experiment decisions

The Vast round-26 TaurosV0 run completed: 183W/0D/201L (47.65625%), 384 games,
1539.95 seconds, no timeouts. Team scores: zam_egg_mie 46.875%, jynx_mie_don
37.5%, gar_egg_zap 58.59375%. See the native/private-protocol limitations above.
The full report is archived locally under `vast-metamon-round26-128/`.

The user now prioritizes Oak argmax optimization, then capacity, then round
robin, then coordinated schedules if preceding experiments do not improve.
The explicit continuation-sweep choice is: every trial starts from the best
saved policy. See [the experiment DAG](EXPERIMENT_DAG.md) for gates, controls,
evaluation counts and deferred scheduling ideas. This supersedes the earlier
fair-objective sweep proposal; no scores from different opponents are combined.
At check-in, 37 rounds were complete, round 38 had 6/14 members published.
Master round 38 evaluation started on Vast in `pokemon:oak-master38` with the
old 128-game/team protocol to check recent progress before stopping the league.

Round 38 completed: argmax 57W/1D/326L, **14.9740%**; sampled 14W/0D/370L,
**3.6458%**. Sent SIGTERM to the verified league controller PID 317401, using
its existing graceful interruption handler. Keep all completed snapshots and
partial-round metadata; this stops training, not the Vast instance.
The next isolated experiment uses `launch_continuation_sweep.sh`: first evaluate
unchanged round 26 at 256 games/team/mode, then 32 continuation trials of
50,331,648 counted steps each, fixed round-25 peers, new sweep history, Oak
argmax objective. Wider models and subsequent DAG nodes remain deferred.

#### User correction: 500 trials and swept training budgets

The user rejected the 32-trial/50M scope: use 500 trials as the sweep default,
start at about 100M and sweep steps as well as the broader training settings.
Stopped the old launcher during baseline evaluation; no completed sweep trials.
Its partial baseline is retained, not scored. Replacement experiment:
`reports/pokemon_experiments/20261018_continue_r26_500`, tmux
`pokemon:continue-r26-500`, log `build/pokemon/continue-r26-500-20261018.log`.
Trial zero: 100,663,296 steps; sweep range 100,663,296–402,653,184.
See `continuation_sweep.ini` and updated DAG for all dimensions. LR cosine
annealing remains enabled; its final ratio is swept. Entropy annealing is a
swept toggle. Existing normal-training config remains unchanged.
The frozen host binary requires `--base.load_model_path=...` for the sweep;
an initial positional-checkpoint launch failed before training and was corrected.

#### Sweep crash repaired; tmux cleanup

The first 500-trial sweep finished run 0 at Oak argmax 0.747395813 after
100,663,296 counted steps, then aborted after 32 sampled layout failures.
Continuous expert fractions and minibatches violated learner-row divisibility.
The assertion is local (absent in checked-out `upstream/5c`), but removing it
alone would permit out-of-bounds contiguous minibatch slices. Retained the guard;
restricted expert fractions to 0.25/0.5/0.75 and minibatches to 1024–8192.
Exhaustively checked all 144 combinations with agents 4096–16384 and horizons
16–128. Restarted with fresh history in `20261018_continue_r26_500_v2`.

Latest requested step range: 50,000,000–500,000,000, starting around 100M;
500 trials, LR cosine annealing on. Verified live training beyond 2.6M steps.
Current window: `pokemon:sweep`; log:
`build/pokemon/continue-r26-500-v2-20261018.log`. Closed 18 obsolete windows,
preserving scrollback in `tmux_archive/` and all on-disk checkpoints/reports.
Historical `pokemon:round-robin` and `pokemon:continue-r26-500` windows are closed.

#### Metamon search across teams and provenance

User requested concurrent evaluation, explicitly chose all 14 learner teams
and 512 games per opposing team (1,536 per policy), then clarified the objective
is finding the strongest policy regardless of team, round or experiment.
Existing non-master Oak measurements cover only round 13; those are not known
to be their best snapshots. Started the current best Oak sweep master (run 50,
89,653,248 steps, Oak 0.7467447917) against sampled TaurosV0 on Vast CUDA,
argmax learner, 512 games/team, seed 20261020. Checkpoint SHA-256:
`767a5e649cf0cf1cbdbc6d941eca9617073330ddca4b8af55c1ecab5e04272ee`.
Output: league `evaluations/metamon-best-teams-512/master-sweep0050/`.

Added resumable `ocean/pokemon/benchmarks/search_metamon.py`. Frozen manifest:
league `evaluations/metamon-policy-search/manifest.json`, 559 distinct
checkpoint/team candidates across 14 teams. Includes all 539 published league
snapshots plus the top 20 distinct Oak-scored historical sweep candidates per
team where available (20 master sweep candidates). Selection of league
snapshots does not depend on Oak scores. New sweep completions after manifest
creation are not automatically added to this comparison.

Sequential stages, all against Metamon directly and alternating seats:

- Screen: 8 games/opponent team, 24 total/candidate, seed 20261021.
- Shortlist: top 3 per learner team plus top 12 globally; 64 games/opponent
  team, 192 total/candidate, independent seed 20261022.
- Confirmation: best per learner team plus top 3 globally from shortlist;
  512 games/opponent team, 1,536 total/candidate, seed 20261023.

Small-screen pruning may miss strong policies; final result is best measured
among promoted candidates, not an exhaustive proof. This broad search takes
several days at measured evaluation speeds while sharing the training GPU.
The queue waits for the initial full master evaluation, then runs one evaluator
at a time, one CPU inference thread. Training continues independently.
Progress/rankings saved after each candidate; completed evaluations resume
without rerunning; failed partial outputs retained separately. Three errors
stop the queue for inspection. All checkpoints/config hashes are verified.
Selection logic checked to preserve each team plus global leaders without
duplicates. Initial master evaluation verified progressing beyond 24 games.
Launcher: `launch_metamon_search.sh`, tmux `pokemon:metamon-search`, log
`build/pokemon/metamon-policy-search.log`. Initial `pokemon:metamon` window
closes automatically when its run ends; its log and reports persist.

#### Reduced Metamon budgets at user's request

User corrected the evaluation budget: 512 was excessive; approximately 32 is
preferred, 100 at most. Stopped the initial 512-game/team process and its waiting
search controller. Preserved its partial `games.jsonl` (at least 312 games, all
against zam_egg_mie); that partial result is not a complete three-team score.
Restarted the master comparison at **32 games/team (96 total)** in new output
`evaluations/metamon-best-teams-32/master-sweep0050`. The historical launcher
filename `launch_metamon_master512.sh` now explicitly passes `--games 32`.

Reduced the queued search to **8 / 16 / 32 games per opponent team** for screen,
shortlist and final comparison respectively (24 / 48 / 96 total per candidate).
The frozen 559-candidate pool remains. Small final samples are coarse ranking,
not reliable resolution of narrow differences. Search stage counts are now CLI
options, persisted in `protocol.json` to prevent accidental mixed-count resumes.
Both master and subsequent search execute sequentially in the single window
`pokemon:metamon-search`; log `build/pokemon/metamon-policy-search-32.log`.
The sweep continues independently. This supersedes all 512-game stage budgets
above; old complete/partial reports remain preserved under their original names.

#### Focused 20-policy Metamon comparison replaces broad search

User did not expect 559 policies and requested narrowing around leaders with
side-by-side comparison to the top scorers. Stopped the broad search controller
and its child evaluation, preserving all completed and partial files. At check:
41/559 screens completed. Best screen: gar_egg_mie round 36, 54.1667% in only
24 games; not a confirmed improvement. The completed sweep-50 short evaluation
was 48W/0D/48L, 50% in 96 games, versus historical round-26 Metamon 47.65625%
in 384 games. These unequal samples/seeds do not resolve small differences.

Replacement manifest: `metamon_leaders20_manifest.json`, generated from current
saved results. Exactly 20 candidates: top five recorded Oak sweep checkpoints,
round-26 incumbent, the 13 previously Oak-evaluated non-master team policies
(round 13), and the leading Metamon screen candidate (gar_egg_mie round 36).
All 14 teams retained. Five Oak leaders and incumbent run first, then the new
Metamon screen leader, then the other team representatives. No further broad
screening is queued. All receive 32 games/opponent team (96 total), common fresh
seed 20261023, same native/private TaurosV0 protocol.

Runner: `ocean/pokemon/benchmarks/compare_metamon_leaders.py`, existing tmux
`pokemon:metamon-search`, log `build/pokemon/metamon-leaders-20.log`.
Output: league `evaluations/metamon-leaders-20/`; incremental `ranking.json`
and `comparison.csv` show Metamon score/counts alongside available Oak
argmax/sample scores and sample sizes. Missing Oak data remains blank.
Manifest validation checked exactly 20 distinct candidates and 14 team identities.
The larger 559-candidate manifests/results remain archived and are superseded
by this active queue. Sweep training continues independently.

#### Sweep stopped; larger Metamon follow-up queued

User explicitly requested stopping the sweep and retaining evaluations, then
more evaluation of the narrowed leaders with faster execution if possible.
Sent SIGTERM to the verified sweep process group 548744, including controller
548746 and its active trainer. Verified both exited while Metamon controller
583105 and evaluator remained alive. Sweep stopped after **73 completed trials**;
all saved checkpoints, history and reports are retained. Best current-v2 Oak
argmax was 74.6745% (trial 50). No automatic sweep restart is authorized here.

Queued `confirm_metamon_leaders.py` after successful completion of the 20-policy
comparison. It selects the top three by Metamon score plus the round-26 baseline
(deduplicated), then runs **100 games/opponent team, 300 per policy**, on fresh
seed 20261024. This respects the user's revised upper preference of 100 per
opponent team. Two independent evaluator processes share the freed GPU, one
CPU inference thread each. At setup, the sole running evaluation used about
460 MiB GPU memory and 13% GPU utilization after training stopped, providing
headroom; actual parallel speedup is not yet measured. Selection/deduplication
checked with both baseline-in-top-three and baseline-outside-top-three cases.

Current focused eval continues in `pokemon:metamon-search`; follow-up waits in
`pokemon:metamon-followup`, log `build/pokemon/metamon-finalists-100.log`, output
league `evaluations/metamon-finalists-100/`. The first window closes on exit;
completed rankings and logs persist. Follow-up requires a complete focused
ranking and detects premature controller exit. Final rankings include the
baseline on the same new seed and sample size; no confirmed-improvement claim
from small differences alone.

#### Follow-up completed; new capacity branch

Fresh-seed 300-game confirmation completed: master r26 141W/1D/158L (47.17%),
sweep50 140W/0D/160L (46.67%), sweep49 130W/0D/170L (43.33%),
sweep39 122W/2D/176L (41.00%). No improvement over r26 confirmed.
Reports: league `evaluations/metamon-finalists-100/`.

User then requested a fresh1024x2 learner vs good frozen64x2 policies, initial1B
steps and sweep500M–2B. This independent branch is documented in
`../20261018_capacity1024/NOTES.md`; prior sweep/league remain stopped.

Inspect the stopped league's saved progress with:

```bash
. /opt/nvm/nvm.sh
./ocean/pokemon/league.sh status leagues/pokemon/rr50m_bf16_20261017
```

The launched job is the native league command below, with all 14 members in
each cycle and no evaluation flag:

```bash
./ocean/pokemon/league.sh train leagues/pokemon/rr50m_bf16_20261017 \
  --rounds 50 --live
```
