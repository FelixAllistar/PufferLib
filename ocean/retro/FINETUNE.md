# Fixed-parent fine-tuning / staged hillclimb

**Superseded active preset:** the current config starts at the return-pipe
black screen for an ending-first curriculum. See [PRACTICE.md](PRACTICE.md).
The notes below describe previous full-level experiments.

## Current speed-first round (2026-09-22)

The configuration now uses **speed objective v2**: fastest individual finish
first, mean successful RTA second, no clear-rate contribution. See
[SWEEP.md](SWEEP.md) for the exact rank and new human-readable console output.
The remaining round-1 notes below are historical, not the current preset.

All 26 completed round-1 trials reached exactly the same PB: **1882 frames**
(31.315093892064 seconds to displayed precision). The original parent already
reached that time. No recorded panel improved the PB. Run 22 had the lowest
mean: 1949.59375 frames / 32.439804108730 seconds; run 12 hit the PB 16/32 times,
versus 5/32 for run 22. These are recorded samples, not guarantees of performance.
An additional deterministic argmax trace of run 22 finished in 2113 frames;
the 1882-frame PB comes from sampled actions, not its argmax watch-through.

Current parent is run 22's saved 8,388,608-step checkpoint (tied PB, best mean):
`checkpoints/retro_cnn_64x60_finetune/retro/sweep_1790053407951_0022/0000000008388608.bin`.
This is an exploratory branch, not an independently confirmed new PB champion.
All previous checkpoints remain intact.

Training has no flat completion bonus or death penalty. It pays only the
existing completion speed ramp, weight 16 and common scale 0.0625; checkpoint,
pipe, area, coin, idle and PBRS rewards remain off. This still optimizes an
expected per-attempt return, not an exact best-of-N objective. The **selection**
objective is explicitly PB-first. Penalizing death was removed from the search
to leave risky attempts available.

Exploration experiment: default LR 0.0001, entropy coefficient 0.0003 and gamma
0.9999. Search LR 0.00001–0.0005, entropy 0.00001–0.003, gamma 0.999–0.9999.
The wider exploration range is a hypothesis for escaping the shared 1882-frame
plateau, not evidence that it will work. Your 32-trial budget and 8 initial
random samples are preserved. All trials branch from the fixed run-22 model.
Fresh outputs: `checkpoints/retro_cnn_64x60_pb` and `logs/retro_pb`.
No full new sweep was launched during setup.

```sh
./puffer sweep retro
# Read the finished old sweep in actual time units:
python3 ocean/retro/report_sweep.py \
  logs/retro_finetune/retro/protein_sweep_1790045327281.tsv
# For a new sweep, pass --checkpoints checkpoints/retro_cnn_64x60_pb/retro.
```

Confirm candidates by best frames first, then mean successful frames on equal
fresh-seed panels. Clear percentage remains visible but does not decide the
winner. Do not reinterpret legacy reliability-first numeric scores as v2.

## Historical round 1

Round 1 starts from the existing NTSC checkpoint:

`checkpoints/retro_cnn_64x60_speed/retro/1790040204447/0000000115343360.bin`

This is the latest fully saved checkpoint inspected during setup, not a moving
`latest` alias. The already-running trainer is untouched. Stop it gracefully
before starting a sweep; its later saves will not change this round's parent.
Weights load, but optimizer state resets. Existing models are not overwritten.

Measured parent baseline (seed 20260907): **29/32 clears**, 1978.31034 mean
successful frames = **32.91763 seconds**, panel score 91.15713. Failures were
two timeouts and one death. The successful attempts ranged from 1882 to 2071
frames. Per-attempt data is in [FINETUNE_BASELINE.tsv](FINETUNE_BASELINE.tsv).
This is a screening sample, not a precise estimate of the true clear rate.

From `/home/felix/puffertank/pufferlib`:

```sh
./puffer check retro
./puffer sweep retro
```

## Round design

- 12 trials, each 8,388,608 decisions (128 full rollout batches), one GPU.
- Every trial starts from the same parent, not the previous trial's final model.
- Default trial: learning rate 0.00005, entropy coefficient 0.0001,
  gamma 0.9995, death penalty 0. Learning rate anneals to 10% within each trial;
  the entropy coefficient stays constant.
- Search only learning rate (0.00001–0.0003), entropy (0.00001–0.001),
  gamma (0.999–0.9999), and raw death penalty (0–4).
- Keep the 256 × 2 model, one-frame controls, horizon 256, and minibatch 4096.
- Final checkpoints get 32 sampled attempts on 1-1, with fixed seeds and a
  3000-frame budget. Rank by clears first, then mean successful RTA frames.
  Training return is not the selection metric.

This is about 100.7M total training decisions. At the screenshot's 43.2k SPS,
training alone is about 39 minutes; initialization/evaluation add overhead.
Actual throughput may differ. No full sweep was launched during setup.

Models go under `checkpoints/retro_cnn_64x60_finetune/retro/`.
The ranking TSV is `logs/retro_finetune/retro/protein_sweep_*.tsv`;
each final checkpoint also has a `.bin.ini` and `.bin.eval.tsv` sidecar.

## Reward hypothesis

The previous config paid raw 3 for the first novel area and raw 3 times each
pipe-segment speed fraction, versus only about 1.6 for completing near frame
2000. It also used learning rate 0.001234825 and reward scale 0.625.
That is a plausible reason to revisit the objective, not proof of the cause
of a particular slowdown.

This round removes all partial-route rewards and uses:

`(4 + 12 * clamp((3000 - finish_frames) / 1800, 0, 1)) * 0.0625`

on the existing RTA split. At frame 2000 this pays about 0.667. Each saved
frame inside the range adds 0.0004167; a 60-frame improvement adds 0.025.
These are optimization anchors, not claims about the best possible time.
Failure pays zero in trial 0; sampled death penalties subtract up to 0.25.
Timeout still pays zero. PBRS, idle penalties, checkpoint rewards, points,
coins, generic area rewards and pipe rewards all remain off.

Gamma now discounts a reward 2000 decisions away by about 0.368 at the default,
versus about 0.0114 previously. This increases the relative importance of the
eventual finish versus immediate outcomes. It does not guarantee faster learning.
Restoring a death penalty is a testable change even for an existing policy;
it can encourage reliability but does not itself pay for speed. Negative
returns in positive-death-penalty trials would be intentional, not PBRS.

## Hillclimb between rounds, not within one sweep

The native sweep is a fixed-parent hyperparameter search, **not automatic
winner-to-next-trial population training**. Do not switch it to `latest`.
Trial 0 is the new-objective/no-death-penalty control, not an unchanged-policy
baseline. Compare all candidates to the untrained parent as well.

After round 1, take the best few candidates and the original parent and run
the same 32-attempt evaluation on each with each of three additional seeds:
20260922, 20260923, 20260924. For example, substitute a concrete model path:

```sh
build/retro_batch/sweep_eval PATH.bin --config config/retro.ini \
  --levels 1-1 --frameskip 1 --metric speed --frames 3000 \
  --repeats 32 --workers 4 --seed 20260922 --output REPORT.tsv
```

Aggregate total clears across these 96 attempts, then successful-frame sums
divided by total clears. Do not average successful times with failed runs.
Small differences can still be sampling noise; 32/32 is not proof of 100%
reliability. Do not promote a candidate merely for higher training return or
because it is the newest file. If none clearly improves, keep the parent.

For a confirmed winner, set `base.load_model_path` to its concrete `.bin` path
and copy its winning four searched settings from its `.bin.ini` into the
defaults for the next round. Narrow the search around them, keeping the
defaults inside their ranges. Then run another fixed-parent round. This
preserves the best model while allowing short exploratory branches to fail.
