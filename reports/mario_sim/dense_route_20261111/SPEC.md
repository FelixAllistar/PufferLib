# mario_sim: dense route states at the maze stages (step-back practice) — spec, 2026-11-11

Builds on `reports/mario_sim/maze_20261110/SPEC.md` (with Review round 1) and
`reports/mario_sim/exit_reward_20261111/SPEC.md`, both implemented and committed (`55cbb283a`, `f4485bfc1`).
Read those and their `NOTES.md` first.

## Why

Run 2 (5.0 @ f68c65a6e, continued from run 1's 5.9B weights) reached 734 of 1,024 real-game level clears and clears
7-4 30/32. It still clears 4-4 and 8-4 0/32. The evaluation trajectories against the reference movie show one
never-tried move in each:

- **4-4.** The movie jumps onto a platform (y 112, x 1492-1510, frames 31638-31656), steps off its right edge and
  falls to the floor (y 176 at x 1531, frame 31680), then stays on the floor. The policy jumps up from that platform
  instead (y 64, 45, 32 at x 1494-1574) and its attempt ends at x ~2030 on the middle corridor, identically in all
  32 tries. Practice from x 1280-1535 is 0 for about 28,000 attempts; from x 1562+ on the floor it is 0.74-0.96.
- **8-4.** The movie enters each down-pipe by pressing Down on the frame it lands on the pipe (frame 64993 at
  x 273-276 after the on-route loop; 65430 at x 2435; 65795 at x 3407). The policy reaches the first pipe every lap
  and never enters; every practice section that needs a pipe is 0.00 over thousands of attempts, with the exit
  reward on.

The bank only holds on-ground states, four per 256-px section, so there is no start from which a near-random policy
performs the move. This change adds *dense* route states (every few movie frames, airborne included) in the maze
stages, each as its own small group, and lets the existing gate signal walk practice backward from the frames where
the move is already under way to the frames before it. The device code does not change: a dense group is an
ordinary group with one entry.

## Hard constraints

- Touch only `ocean/mario_sim/**`, `config/mario_sim.ini`, and `reports/mario_sim/dense_route_20261111/NOTES.md`.
  Nothing under `ocean/retro` (the shared header stays as it is), nothing under `src/`.
- Never write `./puffer` (compile check: `bash build.sh mario_sim build/mario_sim/puffer_mario_sim_dense_check`).
  Never train. No `git add`, commit, stash or checkout. `-j2`, `OMP_NUM_THREADS=2`, `CUDA_ARCH=sm_61`.
- Do not rewrite `build/mario_sim/practice_v2` or `practice_v3`. The new bank is `build/mario_sim/practice_v4`.
- The movie stays at `/mnt/d/puffer_media/mario_tas/smb_warpless_aknell.fm2`; never copy it into the repository.
- Code defaults keep today's behaviour. With the v3 bank, or with `practice_dense_share = 0`, group construction,
  probabilities and every existing test result are unchanged.
- Kernels and `SmbTeacherDevice` are unchanged. CPU/CUDA parity must still be bitwise.

## D1. Builder: `--dense STAGES` and `--dense-step N`

`build_practice_bank ... --route MOVIE --dense 4-4,7-4,8-4 --dense-step 4` (requires `--route`; default step 4,
range 1..64). Without `--dense` the output is byte-identical to today's for the same arguments (verify with `cmp`
against `practice_v3`).

During the movie replay, for each listed stage: let `f0` be the stage's first tracked frame (the
`smb_game_tracked` predicate). Every tracked frame `f` with `(f - f0) % step == 0` is a dense candidate. No
on-ground or position filter (`useful()` is not applied); frames between a movie loop-back and the following pipe
entry are kept (they are the frames right before the 8-4 pipe). Take the snapshot as small Mario with the same
edit rule as route states (`$0754 = 1, $0756 = 0`, restore afterwards, `SMB_BANK_EDITED` when edited).

Each dense candidate goes through the existing per-state path (reset parity, 240-frame native-vs-ROM rollout,
16-frame validation clip; a failing state is dropped and counted, more than 5% dropped is fatal) with
`flags = SMB_BANK_PRACTICE | SMB_BANK_ROUTE | SMB_BANK_DENSE [| SMB_BANK_EDITED] | (band << SMB_PRACTICE_BAND_SHIFT)`
(`SMB_BANK_DENSE = 1 << 16`; band = quartile of its position among the stage's dense entries), `source_frame = f`,
source name `route_dense`. Dense entries are appended after the route entries, write no `states/N.state` file, and
do not count toward `--per-stage`. Raise the artifact cap to 192 MiB when `--dense` is given.

`summary.json`: `dense_states`, `dense_dropped`, `dense_step`, and per dense stage `{dense_states, first_frame,
last_frame}`. Expected with step 4: about 420 (4-4), 468 (7-4), 490 (8-4); report the real counts.

Build (v3's command plus the two options):
```
OMP_NUM_THREADS=2 build/mario_sim/fpg/build_practice_bank build/mario_sim/practice_v4 --per-stage 48 \
  --route /mnt/d/puffer_media/mario_tas/smb_warpless_aknell.fm2 --dense 4-4,7-4,8-4 --dense-step 4 \
  config/mario_sim.ini \
  checkpoints/mario_sim/sweep_1792016021935_0000/0000006442450944.bin \
  checkpoints/mario_sim/sweep_1792052933835_0001/0000009999745024.bin
```
Run `verify_cuda` on its validation clip.

## D2. Loader and groups (`mario_sim.h`)

- Legacy path (`practice_teacher = 0`): skip entries with `SMB_BANK_DENSE` (they are not copied and not counted
  in the completeness check). Mask the band with `(flags >> SMB_PRACTICE_BAND_SHIFT) & 255` wherever it is compared.
- Teacher path: new config key `practice_dense_share` (default 0, range 0..0.9). If it is 0, dense entries are
  skipped exactly as on the legacy path. If it is > 0:
  - Non-dense entries form groups exactly as today (same keys, same order).
  - Every dense entry is its own group, appended after the regular groups in `(stage, source_frame)` order:
    `key = (1ull << 63) | ((uint64_t)stage << 32) | source_frame`, `area` and `level` as for regular groups,
    `section = max(0, smb_game_x(ram)) / group_width`, `reserved = 1` (regular groups keep `reserved = 0`),
    `eligible = 1`. Host vector `dense[g]` (0/1) and `dense_frame[g]`.
  - The group limit stays 4096 (fail with a clear message above it).
- `smb_route_pairs` keeps using all `SMB_BANK_ROUTE` entries (dense ones included).

## D3. Route order and gate (`mario_sim.h`, host only)

Two separate chains per stage:
- **Regular chain**: `smb_route_order` over regular groups only, exactly as today (dense groups are neither
  on it nor anybody's `next`).
- **Dense chain**: the stage's dense groups sorted by `source_frame`.

New config keys: `practice_gate_lookahead` (default 1, range 1..16; used for dense groups only) and
`practice_gate_patience` (default 0 = off, range 0..1e9).

Gate value, computed on the host at each refresh:
```
pl(x)        = (x.wins + 1) / (x.attempts + 2)
candidates   = regular group: { next[g] } (one group, or none at the end of the stage)
               dense group:   the next `lookahead` groups on its dense chain (fewer at the end of the stage)
best         = among candidates with attempts >= lp_min_attempts, the one with the largest pl (earliest on ties)
base         = no candidates at all (end of the chain): teacher_gate(g, nullptr, params)
               candidates but none with enough attempts:  0
               otherwise:                                  teacher_gate(g, &best, params)
fade         = patience == 0 || g.wins > 0 ? 1 : patience / (patience + g.attempts)
gate[g]      = base * fade
```
With `lookahead = 1` and `patience = 0` this is exactly today's value for regular groups. The lookahead lets the
chain step over a dense state that is unwinnable as small Mario; the fade stops a group that has never won from
holding the gate budget forever.

## D4. Probabilities (`mario_sim.h`, host only)

At each refresh (and at start-up), with `s = practice_dense_share` and at least one dense group:
1. Regular set: set `eligible = 0` on dense groups, call
   `teacher_priorities(groups, now, params, &gate)` → `P_reg` (dense groups get 0).
2. Dense set: set `eligible = 1` on dense groups and `0` on regular ones, call `teacher_priorities` with a copy of
   `params` changed to `weight_deaths = 0, weight_lp = 0.2, weight_stale = 0.1, weight_gate = 0.7, floor = 0.05`
   → `P_dense`.
3. Restore `eligible = 1` everywhere and set `prob[g] = (1 - s) * P_reg[g] + s * P_dense[g]`; then `teacher_cdf`.
With no dense groups (or `s = 0`) the single call of today is used unchanged.

Everything else (counters, EMAs, death mass, sidecar, restore by key) treats dense groups like any group. The
`.teacher` sidecar layout is unchanged; regular groups keep their keys, so statistics saved with the v3 bank can
only be restored if the existing bank-hash check allows it. Do not weaken that check: report what happens when a
v3 sidecar meets the v4 bank (expected: refused, and the run then needs `practice_resume = 0`).

## D5. Config and metrics

`config/mario_sim.ini`: `practice_bank = build/mario_sim/practice_v4/bank.bin`, `practice_dense_share = 0.3`,
`practice_gate_lookahead = 3`, `practice_gate_patience = 2000`, one comment line each. Nothing else changes.

Metrics: `teacher_dense_groups`, `teacher_dense_prob` (sum of `prob` over dense groups), `teacher_dense_attempts`,
`teacher_dense_win_rate`, `teacher_dense_top_stage`, `teacher_dense_top_frame` (frames after the stage's first
dense frame), `teacher_dense_top_gate`, and per dense stage `teacher_dense_W_L_solved` (share of its dense groups
with `pl >= 0.5`). Log the dense group count per stage once at start-up.

## Tests (`test_teacher_sim.cpp`; CUDA in `test_runtime.cu`)

1. Builder: `cmp` of a no-`--dense` rebuild against `practice_v3`; v4 counts per stage; every dense entry has
   `SMB_BANK_DENSE | SMB_BANK_ROUTE`, a frame inside its stage, and spacing `step`; dropped states listed.
2. Groups: with the v4 bank and `practice_dense_share = 0` the group list, keys, offsets and start-up probabilities
   equal those of the v3 bank; with 0.3 the first `G_v3` groups are identical to v3's and every dense entry is a
   one-entry group with the specified key; the legacy loader accepts v4 and ignores dense entries.
3. Gate (hand-set statistics): lookahead 3 picks the best of the next three; a candidate under `lp_min_attempts` is
   ignored; end of chain uses factor 1; fade: 0 wins of 2000 attempts with patience 2000 halves the gate, one win
   restores it; regular groups with lookahead 1 and patience 0 give today's values.
4. Probabilities (hand-set): `sum(prob) = 1`, `sum over dense = s`, the regular part equals `(1 - s)` times
   today's single-call result, and inside the dense part a group with gate 0.9 gets more than one with gate 0.
5. Sidecar: round trip with dense groups; a v3 sidecar against the v4 bank (report the behaviour).
6. Real engine, movie present (use the unedited movie state where the small-Mario edit breaks the movie's own
   inputs, and say so):
   - 8-4, dense state at frame 64991 or 64992 (2 or 1 frames before the pipe entry): replaying the movie's inputs
     gives a forward exit, an attempt WIN and one exit reward within 120 frames.
   - 4-4, dense state at a frame in 31668-31676 (mid-fall): replaying the movie's inputs wins by distance.
   - Report (do not assert) what happens from the *edited* small-Mario versions of those states with these scripted
     inputs: 8-4: hold Down for 30 frames; 4-4: hold Right+B until the attempt resolves. This tells us whether the
     dense starts are winnable for small Mario with trivial actions.
7. CUDA: the teacher parity cases stay bitwise equal with the v4 bank and `practice_dense_share = 0.3` (device
   tables are just longer); the maze case's 4-4 events are unchanged.

Run and report: `make -f ocean/mario_sim/fpg.mk -j2 game-test curriculum-test teacher-test progress-test`,
`fpg.mk test`, `test_practice`, `runtime.mk environment` + `test_runtime`, `verify_cuda` on v4, the compile check,
and `make -C ocean/retro teacher-test` (must be untouched and still pass).

## README and report

README: a "Dense route states" paragraph in the Curriculum teacher subsection (what they are, the two chains,
lookahead, patience, the dense share, the v4 bank counts). `NOTES.md`: files changed, every command with its
result, per-stage dense counts, the answers to test 6's report-only cases, the v3-sidecar behaviour, deviations
with reasons, anything not run.

## Not in this change

No reward change, no change to wins/losses, no device code, no change to the shared teacher header, no other
stages (8-1 is not a maze; add it later with `--dense` if needed).
