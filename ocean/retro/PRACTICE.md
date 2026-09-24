# Stage 1: return-pipe black screen to 1-1 finish

## Dashboard counters and reward cleanup

The snapshot already contains **7 HUD coins** from its recorded prefix.
`hud_coins` is the episode-ending ROM coin counter, averaged over completed
episodes; it is not a cross-episode total and is not reward. `coin_events`
counts new pickups since the practice reset. Thus 7 HUD coins and 0 coin
events is expected. Restoring the snapshot restores those same 7 coins.
Coin, score, checkpoint, pipe and idle weights remain zero in the active config.

Generic area transition rewards and their HUD-time extra have been removed
from the implementation. Older sidecars may retain zero-valued options;
nonzero legacy values now fail explicitly instead of enabling a reward.
`area_transitions` counts confirmed area arrivals. `novel_areas` counts newly
visited destinations within this episode. The old `area_transition_rewards`
label was misleading: it counted novel destinations even with reward weight
zero. Neither of the new diagnostic counters pays anything.

The inspected run `1790103369569` used `load_model_path=None`, i.e. fresh
random weights. That user setting is preserved. To resume those practice
weights, explicitly use `base.load_model_path=latest` with this checkpoint
directory; to fine-tune an earlier model, pass its concrete checkpoint path.

`config/retro.ini` now enables a **single fixed practice snapshot**, not a
mixture of arrival states. `env.practice_replay=None` restores ordinary level
starts. Training writes to `checkpoints/retro_cnn_64x60_pipe_exit`; old models
and full-run sweep results are untouched. No long training was started for setup.

## Exact start

`practice/pipe_exit.inputs` records 823 frames of ordinary controller input
from the normal 1-1 start. These naturally enter the first pipe, traverse the
underground area, enter its side pipe, and stop on the **first fully black
frame**. Capture source: round-1 run 12, replicate 0 action RNG stream
(`20260907 ^ 2654435761`), not modified ROM RAM. At capture: TIME 376,
routine 2, bus phase 19, X 208 (still the old area's coordinate).

At initialization the verified NTSC ROM replays the prefix once and caches a
complete emulator snapshot plus the exact black image. Each reset restores
that state; it does not replay the prefix during training. The normal black
transition, area load and pipe emergence then run under the ROM. Timers,
velocity, subpixels, RNG, enemies and frame-rule phase are preserved.
The input file contains no ROM bytes. Its header binds it to the verified
NES2 ROM fingerprint; capture refuses to overwrite an existing tape.

The recurrent policy starts with reset memory, sees black, and observes the
normal loading sequence. This is deliberate; it does not inherit the
policy's underground hidden state. Full-run transfer must later be tested.

## Reward and time

The clock starts at zero on this snapshot and stops at the same next-level
black-screen edge used previously, **not flag contact**. Dashboard keys are
`segment_seconds`, `segment_frames`, `segment_clear_rate`, with `rta_valid=0`.
Both viewers label the clock SEGMENT. Evaluation files declare
`timing_scope=segment` and the replay path; console summaries say
`best_segment_seconds` and `mean_segment_seconds`. Never compare these
numbers directly with full-level times or mix different snapshot panels.

With current weights, a successful finish at frame `f <= 1800` earns:

`16 * (1 - f / 1800) * 0.0625 = 1 - f / 1800`.

Every saved frame pays exactly 1/1800 more (apart from floating-point
rounding). Fast anchor zero means no positive-duration finish reaches the
upper plateau. The 1800-frame timeout is an experiment budget, **not an
optimal time**. Deaths/timeouts pay zero. No checkpoint, pipe, area, score,
coin, idle, death or PBRS terms are enabled. Gamma remains a learner discount.

The former full-run fast anchor of 1200 did NOT erase speed pressure at 1882:
1882 lies inside 1200..3000. It would erase differences between segment
finishes below 1200, which is why practice requires fast anchor zero.

## Commands

```sh
./puffer train retro
./retro watch latest --inspect
```

Before the first practice checkpoint exists, watch the fixed parent explicitly:

```sh
./retro watch checkpoints/retro_cnn_64x60_finetune/retro/sweep_1790053407951_0022/0000000008388608.bin --inspect
```

Practice watching automatically uses single-life/reset-on-finish episodes.
The inherited fixed-parent sweep is also available, but first inspect a short
8.39M-decision training run for improvement from this exact start.

Full-level transfer check (explicit model path, no config mutation):

```sh
./retro watch PATH.bin --full-run --inspect
build/retro_batch/sweep_eval PATH.bin --config config/retro.ini --full-run \
  --frames 3000 --repeats 32 --workers 4 --output /tmp/full-level-transfer.tsv
```

For standalone full-level **training**, also override the timeout/reward range:
`env.practice_replay=None env.max_frames=3000 env.completion_time_max_frames=3000`.
Older checkpoint sidecars without `practice_replay` explicitly evaluate as
full-level starts, not today's active practice setting.

## Verification and next stages

Initial parent baseline with **reset recurrent memory**: 8/8 clears, best
1059 segment frames (17.620980038095 seconds), mean 1132.5 frames
(18.843965904762 seconds), two hits at the best time. This small evaluation
is not evidence of FPG or an optimal ending. Per-attempt results are in
[`practice/baseline.tsv`](practice/baseline.tsv). These times would have been
partly saturated by a 1200-frame fast anchor; the new zero anchor is not.
The reference and compiled-core panels matched exactly. Deterministic viewer
playback from this reset also finished in 1059 frames. A 16,384-decision GPU
smoke run with learning rate zero completed successfully and logged
`segment_seconds`, `segment_frames`, and `rta_valid=0`; it did not update the
parent weights. Both `puffer` and `retro` were rebuilt and the regression
tests passed. No claim is made yet about learning improvements or FPG.

Regression tests compare the entire captured emulator state with an independent
uninterrupted prefix, then compare 300 continuation frames, verify normal
emergence, exact reset images, segment-clock reset and non-RTA dashboard labels.
The capture builder is `make -C ocean/retro/batch ../../../build/retro_batch/capture_practice`.

This stage tests whether it can improve the ending. It does **not** assert
that a fast finish performs FPG, and there is no unverified FPG bonus/detector.
Inspect candidate frame traces before labeling a run as the glitch. Once the
ending improves, move the snapshot back underground, then before the first
pipe, then normal spawn. Those later stages are not yet configured.
Fixed state makes this experiment controlled; success from this state alone
does not prove transfer to other arrival timers or frame-rule phases.
