# Constant-rate pixel/ROM continuation search — 2026-10-18

## Correction — 2026-10-19

The initial launch inherited seven extra sweep dimensions from config/default.ini,
so the native history had 15 dimensions despite the intended eight. Trial zero
used the correct 256x2 architecture and is a valid result: 196/256 clears after
50,331,648 decisions. All subsequently completed candidates changed architecture
and cannot be interpreted as continuation quality or evidence of LR collapse.
The old scheduler was stopped, with artifacts retained for diagnosis. The corrected
active search is documented in ../constant_lr_20261019/ and uses a new history.

The user requested the newly successful ROM policy as the next base, a narrower
search with no annealing, short trials to screen early collapse, and an immediate
launch in the existing Vast tmux. One active binary (`./puffer`, Retro FP32) and
one editable config (`config/retro.ini`) remain in use.

## Parent and preserved experiment

Parent: `saved/retro/pixel_197_20261018.bin`.
Source: `checkpoints/retro_pixel_20261018/retro/1792356246093/0000000838860800.bin`.
SHA-256: `80127cad3ec19a5a1df84f5566a22a3c41d741a0bceec6da96ded2c9d95178bc`.
This is the old 838,860,800-decision parent plus another 838,860,800 decisions,
for a total lineage of 1,677,721,600. The 2.24 MB parent and its resolved source
configuration are preserved locally and on Vast. No ROM or large state dumps
were downloaded. Each trial loads the same weights with fresh optimizer state;
trials do not accumulate into one policy.

The completed long run cleared 197/256 attempts (76.953125%), with 5-4 newly
clearing 7/8. Seven stages remained 0/8: 1-3, 4-4, 5-3, 7-4, 8-1, 8-3, 8-4.
Mean progress checkpoints were 21.1289062. The preceding short sweep's best clear
count was 192/256. Its history stays in `logs/retro_pixel_20261018/`; changing
parent, objective and ranges requires the new history below.

## Learning-rate and update arithmetic

The successful long run used cosine LR decay from 0.0001 to 0.00001. Its
continuous mean is 0.000055, and the native 3,200-epoch discrete mean is
0.0000550140625. Trial zero uses constant 0.000055. Neither LR nor entropy
anneals in this experiment; inactive minimum ratios are set to one.

For batch B = actors * horizon and minibatch M, native optimizer updates are
floor(decisions/B) * floor(replay * B/M). At 1024 actors, horizon 256, minibatch
4096 and replay 2, the new 50,331,648-decision screen performs 24,576 updates;
a requested 1B run performs 488,192 updates after rollout rounding. The screen
is about one twentieth of the experience. Multiplying LR by 19.87 would only
match a crude sum of learning rates; it cannot match the policy trajectory,
new experience, optimizer state or long-run stability. We do not inflate LR.
Constant-rate screening tests the actual prefix of that candidate's long run.
Short-run improvement or survival still needs a later long-run confirmation.

## Active search

500 successful trials, one at a time, exactly 50,331,648 decisions per trial.
Eight varying parameters: LR 5e-6–2e-4; entropy 0–.001; gamma .999–.99999;
GAE .99–.9999; replay 1–2.25; PPO clip .1–.25; gradient clip .25–1;
momentum .75–.95. See `plan.json` and `config/retro.ini` for exact distributions.

Keep the successful 256x2 CNN/RAM/recurrent model, 64x60 image, 1024 actors,
H256, minibatch4096, 12 threads/4 buffers, async and CUDA graphs. Reward weights,
all32 starts, real QuickNES ROM, natural lives/level continuation, and four-frame
actions retain the successful parent's setup. V-trace stays disabled.

Preflight/runtime guards retain a 15,360 MiB VRAM ceiling. Reject sustained
throughput below 50K decisions/sec after warmup, allow 600sec startup, bound
training at 1,800sec and final CPU evaluation at 900sec. A stopped trial needs
at least 98% of the fixed budget to enter history. With the measured roughly
100–115K decisions/sec, expect around 8–10 minutes including fixed evaluation
for typical candidates; actual replay/runtime determines the cost.

The same normal trainer defaults to 1B additional decisions. Its checkpoint
interval is 192 epochs, or 50,331,648 decisions with this fixed rollout shape.
Every short sweep trial finishes at the first interval and has one checkpoint;
a later ordinary long run gets intermediate checkpoints for evaluation.

## Evaluation and ranking

Use the existing all32 level panel: eight attempts each, seed20260907,
30000 native frames, frameskip4, natural lives, first source-level clear stops
an attempt. A fresh parent baseline under the new metric verifies 197 clears.

The existing `perf` objective ranks actual clears first:
100 * (clears + 0.5 * mean_bounded_progress) / attempts.
Any additional clear outweighs all progress tie-breaking. This score includes
a small progress bonus, so it is not identical to clear percentage. Mean128px
checkpoints remain in reports for comparison with the preceding search. Raw
training return is not used as evaluation. Fixed attempts are comparable, but
only eight samples per stage: small changes need independent confirmation.

## Locations

- Active config: `config/retro.ini`
- History: `logs/retro_constant_20261018/sweep.history`
- Console: `logs/retro_constant_20261018/sweep.log`
- Trial settings, progress and summaries: `logs/retro_constant_20261018/retro/`
- Checkpoints and per-level TSVs: `checkpoints/retro_constant_20261018/retro/`
- This report: `reports/retro/constant_lr_20261018/`

`plan.json` records arithmetic and ranges. `parent_baseline.tsv/json` records
the new baseline. Qualification smoke artifacts have separate histories and
budgets and do not enter the 500-trial live search. `launch.json` records tmux,
command, fingerprints, and initial live verification. The old config snapshot
is archival, not a second active training profile.
