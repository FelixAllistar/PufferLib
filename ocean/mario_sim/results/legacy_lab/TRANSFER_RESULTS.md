# First real 1-1 transfer — 2026-10-01

> **Fidelity reset — 2026-10-03:** Training is on hold. Prior synthetic results
> do not validate the encoder, reward, curriculum or training-budget conclusions.
> Raw results and ROM replays are preserved. See the
> [experiment audit](../mario_sim/EXPERIMENT_AUDIT.md) and expanded reconstruction work.

A policy trained only on original generated courses now clears real SMB1 1-1.
The latest controller-corrected candidate clears **7/64 fresh attempts**, after
10/32 development attempts. The earlier mixed candidate cleared 6/32 and 8/64
with fresh action seeds. This establishes
the complete synthetic-training-to-emulator-evaluation path; reliability and
clear speed still need work. These are 1-1 results, not full-game play or a speed
record. The old pixel/ROM environment and legacy synthetic controller remain
available independently.

## Reference measurements and controller

The reference bridge boots the owned World/NTSC ROM to a natural playable 1-1
start without changing RAM. Eight 100-frame sequences measure idle, run, walk,
braking, reversal, standing held/tapped jumps and a running held jump. The
synthetic controller executes the same inputs on original flat geometry.

| Probe | Legacy RMS position error | Measured-profile RMS position error |
| --- | --- | --- |
| Run, horizontal | 35.836 px | 1.423 px |
| Walk, horizontal | 15.825 px | 0.186 px |
| Brake, horizontal | 21.910 px | 2.233 px |
| Reverse, horizontal | 53.621 px | 3.143 px |
| Held standing jump, vertical | 16.233 px | 0.535 px |
| Tapped standing jump, vertical | 3.054 px | 0.556 px |
| Held running jump, vertical | 23.309 px | 0.000 px |

Idle has zero position error in both profiles. Comparisons include all 101
samples; reversal also encounters differently placed left boundaries. These
limited sequences are calibration data, not held-out validation or proof of
exact mechanics. Intermediate jump speeds, collisions, stomps and room transitions
remain approximate.

[The measured profile](profiles/ntsc_small.json) changes run speed from 3 to
2.5 pixels/frame, standing jump behavior, body dimensions, gravity order and
jump-button release behavior. It remains optional. It does not import game code,
physics tables, level geometry or assets into the original simulator.

Two simulator errors appeared during this work: stationary contact briefly
lost grounded state, weakening braking; flooring signed fractional velocity
introduced a leftward bias. Both are fixed and covered by support/braking and
movement-symmetry checks. Early adaptation runs failed and remain recorded.
A fresh flat curriculum with stronger frontier shaping restored learning.
Several settings changed together, so these runs do not isolate the benefit of
calibration, reward scaling, initialization or curriculum.

Exact comparisons and run identities are in [calibration.json](results/transfer_20261001/calibration.json)
and [study.json](results/transfer_20261001/study.json). The local calibration bundle
contains aligned ROM and synthetic trajectories, configs, executable/source
snapshots and hashes.

## Transfer protocol

Frozen MLP/MinGRU policies use 1,128 semantic floats and 64 controller combinations,
with one decision per NES frame. The adapter reads the live block-buffer ring,
kinematics and relative entities; it does not provide absolute world position or
copy the level into the training generator. Its RAM mapping references the
[primary SMB1 disassembly](https://6502disassembly.com/nes-smb/SuperMarioBros.html).

Every attempt restores the naturally reached 1-1 start, empties recurrent memory
and uses its recorded stochastic action seed. Episode index modulo 32 determines
initial idle frames, which count toward elapsed time. The frame cap is 4,000;
the endpoint is first death, the 1-1 black-screen RTA split, or timeout. Every
action tape is replayed and compared with the complete serialized emulator state.
ROM execution performs no weight updates.
After reconnecting, all 995 recorded artifact hashes across nine evaluation
bundles were checked again, alongside the saved replay-verification records for
280 attempts. See [artifact_audit.json](results/transfer_20261001/artifact_audit.json).

The original diagnostic misclassified floor tile `0x54` as brick. That run is
preserved and excluded from the comparison below. Subsequent adapter fixes map
terrain correctly and suppress pit claims in unknown block-buffer columns.
Adapters/configs/binaries are captured separately in each bundle. This comparison
is development evidence for the whole pipeline, not a controlled single-variable
ablation. All attempts use the same real level; different action/start seeds do
not establish generalization to other levels.

| Candidate | Real clears | Deaths / timeouts | Furthest world X |
| --- | --- | --- | --- |
| Earlier adaptive synthetic policy, corrected terrain adapter | 0/32 | 9 / 23 | 899 |
| Measured controller, pipe-height curriculum | 0/8 | 7 / 1 | 1,669 |
| Measured controller, mixed courses | **6/32** | 23 / 3 | 3,267 |
| Focused walker continuation | 3/32 | 25 / 4 | 3,266 |
| Frozen mixed candidate, fresh action seeds | **8/64** | 46 / 10 | 3,267 |
| Corrected buttons, synthetic adaptation | **10/32** | 18 / 4 | 3,267 |
| Frozen corrected-button candidate, fresh action seeds | **7/64** | 48 / 9 | 3,267 |

The mixed candidate's mean successful endpoint time is 2,545.7 native frames.
Four clears stay on the surface; two use the ordinary underground route and
return. Success-conditioned times exclude failed attempts. This benchmark does
not adjudicate a speedrun ruleset. The final corrected-button candidate's fresh
panel averages 2,511.1 frames on its seven successes: three surface clears and
four ordinary underground returns. Its development success rate of 31.3% drops
to 10.9% on this fresh panel; reliable transfer is still unresolved. The earlier
8/64 and final 7/64 checks use different action seeds, so they do not demonstrate
an improvement between candidates.

The walker continuation improves the harder synthetic walker panel from 7/32 to
17/32, while real clears fall from 6/32 to 3/32. The mixed candidate was retained
over the walker continuation, then used as the parent for the button adaptation.
On synthetic split 2 with fresh seed 7109, it clears 64/64 each of flat, gaps,
pipes and stairs, 14/64 walkers, 19/64 underground routes, and 51/64 composite.
All 448 complete synthetic tapes replay exactly. The real fresh-seed check
changes action randomness and idle phases, not the level's geometry.

The final adaptation models Down braking and applies opposing-direction
cancellation consistently in both backends. It uses another 31,457,280 synthetic
frames from the mixed candidate, with no ROM weight updates. Its development
synthetic panel clears 32/32 each of flat, gaps, pipes and stairs, 8/32 walkers,
5/32 underground routes and 28/32 composite. Greedy real evaluation improves from
eight early stalls at X≈94 to eight enemy deaths at X≈1,961–2,023; it still clears
0/8. The stochastic policy remains necessary for the observed successful runs.
The checkpoint was selected using development seed 901, then frozen for the
64-attempt check with seed 9127. Both working candidates and unsuccessful
adaptations remain available with their configurations and parent identities.

## Failures that are now visible

- **Enemy timing and contact.** Twenty mixed-policy deaths occur with feet above
  the pit cutoff. Detailed examples show direct Goomba contact at X≈297, 690 and
  1,511. An earlier candidate also dies beside a Koopa at X≈1,669. The current
  simulator has one simple walker model; Koopas, elevated enemies, collision
  extents and bounce behavior need more faithful coverage. The mixed candidate
  also clears only 7/32 of the harder synthetic walker family. In the final fresh
  panel, 35 deaths occur above the pit cutoff; these are enemy/contact candidates,
  not individually verified causes. The final synthetic walker panel is 8/32.
- **Later pit landings.** Three mixed-policy deaths have feet below 240 pixels,
  near X≈2,470. The pipe-only candidate falls into earlier gaps. Four-tile pipes
  and tall stairs are now trainable; safe landings after obstacle combinations
  remain a reliability target. The final fresh panel has 13 deaths below the
  pit cutoff.
- **Underground exit loops.** Three mixed-policy attempts enter the bonus room
  and time out. The pipe-only trace shows repeated jumping against the side exit.
  Required synthetic underground routes score only 7/32 for the mixed candidate;
  surface completion is not evidence of reliable pipe recovery. The final fresh
  panel has nine underground timeouts; its synthetic required-route panel is 5/32.
- **Observation uncertainty and unsupported mechanics.** Unknown ring columns
  occur especially underground and around transitions. They now produce no
  unsupported-space claim, but there is no explicit unknown channel. The mixed
  panel records 9,060 frames with missing cells, 24 with unsupported entity IDs,
  and zero large-player frames. Powerups/large form, exact pipe eligibility and
  detailed enemy types are still incomplete. The final fresh panel records
  22,170 frames with missing cells, 192 with unsupported entity IDs and zero
  large-player frames.
- **Button semantics and deterministic failures.** The earlier greedy policy
  clears 0/8 and stalls at X≈94, repeatedly selecting Down+Right+Jump. A new ROM probe
  confirms that holding Down while running brakes the real small player; the
  earlier measured simulator ignored it outside pipes. Mode 2 now models that
  braking. A separate raw Left+Right probe also differs from synthetic cancellation;
  mode 2 cancels opposing directions before either backend receives them. Earlier
  modes and their evaluation bundles retain their original behavior. This is a
  shared horizontal-input convention for the main experiment. The corrected
  greedy policy advances to enemy deaths at X≈1,961–2,023, still clearing 0/8.
  The expanded calibration now saves eleven input sequences.
  The Down-run probe's RMS horizontal error is 2.469 pixels in mode 2; the raw
  opposing-direction probe intentionally has different applied inputs from the
  main experiment. See [calibration_buttons.json](results/transfer_20261001/calibration_buttons.json).

The offline viewers show actual pre-action semantic inputs, button probabilities,
kinematics, enemy records and sampled gameplay frames. A high jump-button
probability alone does not establish a timed jump edge or a safe landing.

Local evidence:

- [Old-policy comparison](../../logs/mario_lab/transfers/evaluate_corrected-old-policy_20261001T125852Z_2f5db32b/manifest.json)
- [Pipe-only failure viewer](../../logs/mario_lab/transfers/evaluate_tall-pipes-only_20261001T132044Z_ade41fbe/trace_viewer.html)
- [Mixed-policy transfer manifest](../../logs/mario_lab/transfers/evaluate_mixed-1-1_20261001T132707Z_3575eded/manifest.json)
- [Mixed-policy success/failure viewer](../../logs/mario_lab/transfers/evaluate_mixed-1-1_20261001T132707Z_3575eded/trace_viewer.html)
- [Replayable mixed synthetic panel](../../logs/mario_lab/experiments/TRANSFER001_measured-mixed_20261001T132044Z_4ec097b3/evaluations/20261001T132612Z_afe9da42/manifest.json)
- [Corrected-button candidate and checkpoint](../../logs/mario_lab/experiments/TRANSFER001_button-semantics_20261001T134625Z_744fe092/manifest.json)
- [Corrected-button success/failure viewer](../../logs/mario_lab/transfers/evaluate_corrected-buttons_20261001T134846Z_8da5fbcc/trace_viewer.html)
- [Final fresh-panel manifest](../../logs/mario_lab/transfers/evaluate_corrected-buttons-frozen_20261001T135148Z_c47baa70/manifest.json)
- [Final fresh-panel viewer](../../logs/mario_lab/transfers/evaluate_corrected-buttons-frozen_20261001T135148Z_c47baa70/trace_viewer.html)

## Speed and verification

On the GTX 1060 3GB, the final mode-2 simulator executes 16,777,216 frames in
5.697 seconds: **2.95 million frames/s**. This includes scripted actions,
observations and automatic resets, with 64 warmup steps excluded. It excludes
policy inference/PPO. Successful training stages execute roughly 262k–374k frames/s
including startup. The earlier mode-1 measurement was 2.52 million frames/s.
See [benchmark_buttons.json](results/transfer_20261001/benchmark_buttons.json),
[benchmark.json](results/transfer_20261001/benchmark.json)
and the run timings in [study.json](results/transfer_20261001/study.json).

CPU and sanitizer checks cover movement, support, jump release, collision,
observation invariance, replay, generation and practice starts. Fifteen CUDA parity
cases cover all three controller modes, root/practice resets and graph execution. ROM
adapter fixtures cover ring wrapping, terrain tags, fractional coordinates,
translation, defeated enemies and missing-column pit suppression. The policy
wrapper's recurrent logits/actions match Puffer inference over 256 decisions
including a memory reset. The offline viewer's frame/episode controls were
exercised with a DOM/canvas stub; it was not opened in a graphical browser.

Legacy physics/geometry retain bit-identical observations, rewards and the
original state prefix over 32,768 compared decisions; see
[legacy_parity.json](results/transfer_20261001/legacy_parity.json). Snapshot structs
remain version-specific.

## Next experiments

1. Measure contact extents and stomp/bounce trajectories against the emulator,
   then add original Koopa-like, elevated and clustered enemy scenarios. Use
   reachable enemy/recovery practice starts and track retention of gaps, pipes
   and stairs. The walker continuation shows that higher synthetic scores alone
   are insufficient.
2. Calibrate underground side exits and add varied original bonus-room layouts,
   including recovery after an unnecessary jump. Keep a separate required-route
   holdout rather than counting surface clears as pipe competence.
3. Inspect recurrent state and jump-button edges around contacts and landings;
   compare greedy and stochastic execution on frozen tapes. Add explicit
   observation uncertainty in a separate encoder experiment, preserving the
   existing 1,128-float policy contract for comparisons.
4. Repeat frozen real evaluations after each change, with development seeds
   separated from final checks. Later real levels are needed to test geometric
   generalization; this study evaluates only 1-1.

TAS/reset data,
Go-Explore on both backends, QD and independent encoder experiments remain in
the [experiment register](../retro/EXPERIMENTS.md).
