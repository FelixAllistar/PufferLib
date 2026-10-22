# New pixel/ROM continuation sweep — 2026-10-18

Requested after stopping the semantic simulator experiment. Run on the Vast GPU
in the existing `ssh_tmux:0.0`, from `/workspace/PufferLib`. The active trainer
is `./puffer`, compiled as `retro` FP32, with one editable `config/retro.ini`.
The previous experiment's handoff is
`reports/mario_sim/paused_for_retro_20261018/README.md`.
The local `./puffer` has not been rebuilt for Retro; the switched trainer is
the one on Vast. Local source/config and these reports mirror the remote setup.

## Recovered parent and original task

The old pixel policy and its original run config survived locally as
`saved/retro/vast_838m.bin` and `saved/retro/vast_838m.bin.ini`. Both were copied
back to Vast. The parent is from
`checkpoints/full_games/retro/1790504381476/0000000838860800.bin` in the earlier
instance, representing 838,860,800 trained decisions. Its SHA-256 is
`addbbc7e69f4ec2c666fcab9531d9e76cdc5bec1af0eeeca9f3e0b02f3060cfb`.

Its saved all-level report recorded 184/256 clears with natural lives. That
report used `perf`; its numeric score is not the new `checkpoints` objective.
Do not compare its clear rate directly with the simulator's first-death panel.

The active task executes the verified World/NTSC ROM in independent QuickNES
instances, using compiled CPU instruction blocks. Observations are the entire
64×60 luminance image plus 112 RAM features, 3,952 values total. The existing
three-convolution/RAM/fusion encoder feeds a 256-wide two-layer recurrent model
and 64 controller combinations. Each decision holds buttons for four native
frames, and all frames execute the real ROM. This is the preserved hybrid
pixel/RAM policy, not an image-only ablation.

Start levels span all 32 original stages. There is no pipe-exit practice replay.
Training continues through level changes, deaths and the ROM's natural midpoint
respawns. An episode ends at game over, final victory or 30,000 native frames.
Glitches/warps are allowed by ROM execution; this is not a certified warpless
or glitchless objective.

Original exploration defaults are restored: completion reward 2, death penalty
.05, 128-pixel progress reward .125, novel-area reward 1, score scale .0001 and
reward scale .0625. Time/coin/idle bonuses start at zero. The later local
post-download config had disabled progress and novel-area rewards; that version
is deliberately not the baseline for this new search.

## Search

- 500 successful trials, one at a time on the GPU.
- Every trial independently loads the same parent weights. Optimizer,
  annealing, recurrent state and the training step counter restart each trial.
  Winners are not automatically promoted as the next trial's parent.
- Trial zero uses the original PPO/reward defaults and 30M additional decisions.
  Subsequent trials sweep 10M–100M decisions, equivalent to 40M–400M nominal
  emulated frames. Native rollout rounding is recorded per trial.
- 31 varying dimensions: training budget, actors 128–32K, horizons 8–512,
  minibatches 1K–32K, replay .25–4, LR 1e-5–.02, gamma .97–.99999, GAE
  .85–.9999, entropy 0–.02, learning-rate/entropy annealing and minimum ratios,
  standard PPO/value/gradient/momentum controls, V-trace, async/graphs and eight
  reward weights. Exact ranges are in `config/retro.ini` and `plan.json`.
- The model and image contract remain fixed to match the checkpoint. Threads
  are fixed at 12 with four buffers for this instance's approximately 11.5-core
  CPU quota. High actor counts must fit the actual pixel rollout memory.

Preflight checks tensor/sequence divisions, nonzero update counts, a 15,360 MiB
estimated GPU memory ceiling and bounded graph construction work. The worker
also measures resident memory and speed, allows 600 seconds for startup,
rejects sustained speed below 5K decisions/s after warmup, and bounds training
at one hour plus at most 900 seconds for evaluation. A timed-out training run
saves and evaluates its actual budget when it has reached the minimum budget.
Slow/memory/failed evaluations are failures, not successful zero scores. One
final checkpoint per trial keeps the checkpoint footprint near 1.1 GB for 500
successful trials, plus small reports and any explicitly preserved failures.

## Fixed evaluation

Each candidate gets eight sampled attempts on every original level: 256 attempts,
seed 20260907, four-frame actions, 30,000 native-frame cap per attempt, natural
lives/midpoint respawns, and first source-level clear as terminal. There is no
practice replay. The model is frozen; CPU inference has independent recurrent
states and fixed per-attempt seeds.

The search target is **mean novel 128-pixel progress checkpoints**, including
failed attempts and additional areas. Respawn revisits cannot earn distance
again. Swept reward weights and checkpoint spacing do not enter the evaluation
score. Per-attempt clear/time/progress and all 32 level IDs are retained in each
`.bin.panel.tsv`, with clear rate and timing also logged. This measures balanced
level competence; it is separate from a continuing full-game/speedrun evaluation.

The unchanged parent is evaluated with exactly this protocol before interpreting
candidate gains. Smoke/qualification panels use separate reports/history and
never enter the live search history.

## Artifacts and status

```text
config/retro.ini
logs/retro_pixel_20261018/sweep_20261018.history
logs/retro_pixel_20261018/sweep_20261018.log
logs/retro_pixel_20261018/retro/RUN.ini
logs/retro_pixel_20261018/retro/RUN.progress.csv
logs/retro_pixel_20261018/retro/RUN.stderr.log
logs/retro_pixel_20261018/retro/RUN.eval/trial.json
checkpoints/retro_pixel_20261018/retro/RUN/STEPS.bin
checkpoints/retro_pixel_20261018/retro/RUN/STEPS.bin.panel.tsv
```

Native Protein owns proposal generation and persistent history. The env-owned
worker adds runtime guards and returns the native result pipe after validating
the panel's coverage and score. The score is preserved; GP costs include actual
wall time through initialization, graph capture and evaluation. No shared optimizer, encoder or ROM
rules are changed. Use a new history when changing parent, ranges or protocol.

The sweep was launched in `ssh_tmux:0.0`. `launch.json` records the exact command,
time, parent/config/worker/binary fingerprints and verification; `live_status.json`
records the first observed live trial. The active history is separate from the
short qualification histories.

Validation completed before launch:

- CPU shape/memory/graph preflight tests and six worker tests passed locally
  and remotely. Tests cover incomplete panel coverage, mismatched score/curve
  points, real wall-time costs and graceful rejection of slow workers.
- The FP32 256×2 encoder passed CPU/GPU feature-map and recurrent output checks,
  all 40 numerical gradient checks, branch/image-edge coverage, and borrowed
  input/CUDA-graph replay checks. The learned parent passed 384 ROM decisions
  with six recurrent resets; observation bytes matched the staged reference.
- The unchanged parent reproduced **184/256 clears (71.875%)** and scored
  **20.6484375 mean progress checkpoints** on the full fixed panel. Per-level
  results are in `parent_baseline.tsv` and the summary is in `parent_baseline.json`.
- End-to-end native sweep qualification trained 524,288 decisions, changed the
  weights, saved a checkpoint, evaluated all 32 levels with a short 512-frame
  panel, and wrote the matching score into a separate native GP history.
  A second qualification verified that GP cost equals measured worker wall time
  (about 12 seconds), including initialization and graph capture. Its score of
  6.59375 is for the short panel and does not compare with the full baseline.

The qualification runs are tests, not live search observations. Initial memory
was about 14.8 GiB for the original 1,024-actor/H256 baseline, under the 15 GiB
resident ceiling. Later trial settings must still pass preflight and runtime
guards independently. No learning-quality improvement is established at launch.
