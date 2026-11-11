# mario_sim exit reward, gate signal and progress cap: implementation notes (2026-11-11)

This implements `SPEC.md` in this directory on top of `55cbb283a`. The practice bank (`build/mario_sim/practice_v3`)
was not rebuilt. `TeacherGroup` and the `.teacher` sidecar format are unchanged.

## Files changed

- `ocean/mario_sim/game.h`
  - E1: `SmbGameConfig::progress_cap = 3400`. The reward x is `clamp(smb_game_x, 0, progress_cap)`.
  - E2: `SmbGameConfig::exit_reward = 0` and `SmbGameProgress::arrival` (appended):
    - `arrival = 0` on every tracked call;
    - `= novel` for a non-continuous arrival in a known area;
    - `= -1` when a new area's frontier is appended.
  - `SmbGameProgress::forward_exits` (appended; see deviation 2).
- `ocean/mario_sim/practice.h`: `SMB_TEACHER_EVENTS = 5` and `events[4]` = forward exits. The counter block is
  `4G + 5`.
- `ocean/mario_sim/mario_sim.h`
  - Config keys: `game.progress_cap` (256..16384, default 3400; `checkpoint_distance` now validated against it),
    `game.exit_reward` (0..10, default 0) and `practice_weight_gate` (0..10, default 0).
  - Exit reward in `smb_after_frame`, as specified.
  - `smb_route_order` builds `next[]` and `on_route[]`. `smb_teacher_gates` computes `gate[]` at each refresh and at
    start-up, and it is passed to `teacher_priorities`.
  - Start-up log line: `route order: 393 of 400 groups on-route; weight_gate=0.3 exit_reward=0.5 progress_cap=8192`.
  - Metrics: `teacher_forward_exits`, `game_exit_reward`, `teacher_top_gate_stage`, `teacher_top_gate_section`,
    `teacher_top_gate_value` and `teacher_gate_share`.
  - All counter copies and folds use `4n + SMB_TEACHER_EVENTS`.
- `ocean/mario_sim/mario_sim.cu`: counter allocations and copies use `4n + SMB_TEACHER_EVENTS`.
- `ocean/retro/curriculum_teacher.h` (additive)
  - `TeacherParams::weight_gate = 0`, validated finite and `>= 0`.
  - `teacher_gate(g, next, p)`.
  - `teacher_priorities(groups, now, p, gate = nullptr)`. The gate term is added only when `gate && weight_gate > 0`.
    It throws if `gate->size() != groups.size()`. Otherwise the arithmetic is the old code, so retro's call is
    unchanged.
- `ocean/retro/tests/test_teacher.cpp`: new case 10 with the spec constants. Cases 1–9 are untouched.
- `ocean/mario_sim/test_teacher_sim.cpp`: new `exit_gate_tests()`, plus two changes to existing checks:
  - the counter-size check is now `4G+5`;
  - the refresh check's expected values now pass the gate vector, recomputed independently from `next[]` and
    `teacher_gate` (the ini now has weight_gate 0.3).
- `ocean/mario_sim/test_runtime.cu`: `4G+5`, the config is asserted (exit 0.5, cap 8192, gate 0.3), `forward_exits` is
  checked against the device after the forced refresh, and `parity_forward_exits` / `forward_exits` are printed.
- `ocean/mario_sim/README.md`: progress cap (including that it changes the progress part of evaluation scores, so
  evaluations must compare checkpoints under the same config), the exit-reward and gate bullets (both need
  `practice_teacher = 1`), and the new keys and metrics.
- `config/mario_sim.ini`:
  - `practice_weight_deaths = 0.4`, `practice_weight_lp = 0.2`, `practice_weight_stale = 0.1`,
    `practice_weight_gate = 0.3`;
  - `progress_cap = 8192`, `exit_reward = 0.5`.
- Test fixture (build output, not tracked): `build/mario_sim/fpg/teacher_pre_exit.bin` + `.teacher`. These are
  byte copies of `build/mario_sim/teacher_test_checkpoint.bin(.teacher)` as written by round 1's `test_runtime`
  before this change.

## Commands run and results

All from the repository root with `OMP_NUM_THREADS=2`, `-j2` and `CUDA_ARCH=sm_61`. Logs are in the session scratch
directory (`scratchpad/exit/`).

1. `make -C ocean/retro teacher-test teacher-archive-test` → exit 0 (18 s).
   - `PASS curriculum teacher: EMAs, bias correction, batched outcomes, half-life, kernel, hopeless factor, ranks, mixture, sampling, validation, gate`
   - `PASS teacher archive: variants, group identity, eviction rules a/b/c, death chain + failure kernel, attempt win/cap/death, teacher selection + spawn gate + lanes, v2/v1 archives + rejection`
   - All pre-existing constants pass unchanged.
2. `make -f ocean/mario_sim/fpg.mk -j2 CUDA_ARCH=sm_61 game-test curriculum-test teacher-test progress-test` → exit 0
   (3m40s).
   - `test_modes` `"failures":0`.
   - `test_game_rom` `"semantic_and_lifecycle_parity":true,"failures":0`.
   - `test_teacher_sim` PASS twice.
   - `test_curriculum_cpu` and `test_curriculum_cuda` (bitwise) `"failures":0`.
   - `test_progress` and `test_progress_cuda` PASS.
3. `make -f ocean/mario_sim/fpg.mk -j2 CUDA_ARCH=sm_61 test` → exit 0. `test_cpu` `"failures":0`; `test_cuda`
   bitwise, `"failures":0`.
4. `make -f ocean/mario_sim/fpg.mk -j2 build/mario_sim/fpg/test_practice && build/mario_sim/fpg/test_practice` → PASS.
5. `make -f ocean/mario_sim/runtime.mk -j2 CUDA_ARCH=sm_61 environment` → exit 0.
   `build/mario_sim/runtime/test_runtime` → exit 0 (4m58s):
   ```
   mode=game/fpg/mixed/practice ... parity=passed
   mode=teacher actors=128 practice=32 groups=400 parity_steps=256 steps=2000 parity_attempts=170 parity_wins=140 parity_failures=0 parity_loops=0 parity_wrong_exits=0 parity_route_loops_passed=0 parity_forward_exits=0 route_pairs=6 selected=1310 attempts=1293 wins=1061 death_events=20 loop_events=0 wrong_exit_events=0 route_loops_passed=1 forward_exits=0 stream_refreshes=27 agent_steps=242688 entropy=5.9789 parity=passed
   mode=teacher-maze 4-4 route_states=41 parity_frames=256 parity_attempts=170 parity_failures=13 parity_loops=10 parity_wrong_exits=0 parity_route_loops_passed=0 parity_forward_exits=0 parity=passed
   {"decisions":262144,"resets":3671,"stages":32,"modes":4,"practice_checkpoint_resume":true,"teacher":true,"failures":0}
   ```
   - The maze case's 4-4 events are unchanged from round 1 (13 failures, 10 loops, 170 attempts).
   - Bitwise parity holds with the `4G+5` block and `exit_reward = 0.5`.
   - The live part's totals differ slightly from round 1 (wins 1061 vs 1038, selected 1310 vs 1311) because the gate
     now shapes the live CDF. That part is not a parity comparison.
6. `CUDA_HOME=/usr/local/cuda bash build.sh mario_sim build/mario_sim/puffer_mario_sim_exit_check` → exit 0, `Built`.
   The only warnings are the existing `src/pufferl.cu` ones.
7. `bash build.sh retro build/retro/puffer_retro_gate_check --float` → exit 0, `Built`. The only warnings are existing
   ones in `ocean/retro/nes_emu/nes_data.h` (multichar) and `ocean/retro/retro_obs.h` (`leftx`). There are none from
   `curriculum_teacher.h`.
8. `./puffer` timestamp is `2026-11-04 21:45:47` before and after, so it was not written.
9. `verify_cuda` was not run (the bank is unchanged, as the spec says).

New `test_teacher_sim` output:
```
arrival: first -1, continuous 0, gap +300 past frontier 300, gap behind frontier 0
progress cap: 3400 -> 3528 earns 1 checkpoint at cap 8192, 0 at cap 3400
route order: hand-built C(10) -> A(50) -> exit, off-route B, other-level D alone
exit reward: same-area 600 px gap pays 0.5 once, repeat 0, new-area pair pays, pair behind frontier 0, lost life 0, 255 px 0, exit_reward=0 pays 0, practice actor paid
route order: 393 of 400 groups on-route, one chain per stage (32)
8-4 chain: a3fc:0 a3fc:1 a3fc:2 a3fc:3 a3fc:4 a3fc:7 a3fc:8 a3fc:9 a3fc:12 a3fc:13 aec2:0 aec2:1 aec2:2 aec2:3 aec2:4 a3fc:16 a3fc:17 a3fc:18 -> exit
last 7-4 group a371:12 has no next (level exit follows)
gate: 8-4 a3fc:4 top gate 0.499501, prob 0.004484 with weight_gate=0.3 vs 0.000278 with 0 (x16.1)
pre-change sidecar: 400 groups restored (1295 attempts), refreshed with the gate on (18 groups with gate > 0)
movie exit rewards (8, episode status 1 at frame 67114, deaths 0):
  1-2 frame 3730: ac37 x=2644 -> a690 x=2872 (arrival -1)
  2-2 frame 12682: ae47 x=3009 -> a690 x=2872 (arrival -1)
  4-2 frame 28487: acda x=2977 -> a690 x=2872 (arrival -1)
  7-2 frame 52207: ae47 x=3009 -> a690 x=2872 (arrival -1)
  8-4 frame 65163: a3fc x=273 -> a3fc x=1848 (arrival 566)
  8-4 frame 65600: a3fc x=2435 -> a3fc x=3128 (arrival 693)
  8-4 frame 65965: a3fc x=3407 -> aec2 x=56 (arrival -1)
  8-4 frame 66835: aec2 x=1074 -> a3fc x=4152 (arrival 695)
```

## 8-4 next-group chain

The groups follow the movie's order through 8-4, using each group's earliest route-state frame:

`a3fc:0 → 1 → 2 → 3 → 4 → 7 → 8 → 9 → 12 → 13 → aec2:0 → 1 → 2 → 3 → 4 → a3fc:16 → 17 → 18 → (level exit)`

- `a3fc` section 4 → section 7, as the spec expects: pipe 1 lands at x 1848.
- 9 → 12 is pipe 2, 13 → `aec2` is pipe 3, and `aec2:4` → `a3fc:16` is the water exit.
- 393 of 400 groups are on-route. The 7 off-route groups are the policy-only groups: 6 in 1-1 and 1 in 8-2.
- The test asserts exactly one chain per stage (one head and one tail in each of the 32 stages), and that no link
  crosses stages or touches an off-route group.

## Where the movie's exit rewards land, per stage

These come from a game actor with the teacher on and the training config, replaying the movie from the earliest
route state (1-1, unedited) to 8-4 victory with no deaths.
- **1-2, 2-2, 4-2, 7-2:** once each, at the exit pipe into `a690` (x 2872).
- **8-4: four times.**
  - Pipe 1: x 273 → 1848, arrival 566 past the frontier, which is 1282 from the passed loop.
  - Pipe 2: 2435 → 3128, arrival 693.
  - Pipe 3: `a3fc → aec2`, first arrival.
  - Water exit: `aec2 → a3fc` at x 4152, arrival 695 past the frontier at 3457. This one needs E1: with a 3400 cap
    both positions clamp to 3400 and the arrival would be 0.
- **Every other stage, including 4-4 and 7-4: none.** The test asserts the exact per-stage map
  `{1-2:1, 2-2:1, 4-2:1, 7-2:1, 8-4:4}`, 8 rewards in total, and that `forward_exits` equals the `events[4]` count.

## Deviations, with reasons

1. **`arrival` when the frontier table is full.** The spec sets −1 "in the new-area branch (frontier appended)". When
   all 256 frontier slots are used, no frontier is appended. `arrival` then stays 0 (no new ground), because a −1
   there would pay on every re-entry to an area that can never get a frontier. This is the same reason
   the existing code suppresses progress rewards when the table is full.
2. **`SmbGameProgress::forward_exits` (appended) is not in the spec.** `game_exit_reward` is "mean exit reward per
   completed ordinary game episode", and the episode's own count is needed at episode end. The field is counted
   with `events[4]` (the test checks that they are equal) and logged as `forward_exits * exit_reward`, like
   `game_checkpoint_reward`. `SmbGameProgress` is not part of any sidecar.
3. **The exit condition reuses `forward`.** It is written as `regained && new_ground && (forward || (k1 == k0 &&
   x1 - x0 >= 256))`. `forward` is already `regained && k1 != k0 && smb_route_pair(k0, k1)`, so this is the spec's
   expression. `x0` and `x1` are the signed uncapped `raw_x`, as in Part B.
4. **Pipe 1 starts at x 273, not the spec's 276.** The last tracked x before the pipe is 273. The spec's 276 was the
   probe's first untracked frame. The assertion is per stage, and every place is printed.
5. **The hand-set table for the 10x gate test is my design.** The spec does not give it. With all other groups fresh
   (no attempts, equal signals) the 10x cannot hold: every group shares rank 1 on deaths, lp and stale, the gate
   only adds 0.3 versus 0.15, and the ratio is about 1.16. The table used instead:
   - every other group has 10 attempts with p = 7/12 (records of 5 outcomes with 2 then 4 wins, or 4 then 2), so
     progress is > 0;
   - other groups have death mass 1 + (k mod 13) and stale stamps (k mod 17)·1000;
   - 8-4 `a3fc:4` (0/1000) and its next `a3fc:7` (500/1000) were just selected and have no death mass, so they rank
     last on the three old signals, as a hopeless, over-sampled pipe section would.

   This gives 0.004484 against 0.000278 (×16.1). Without the gate, its probability is almost only the uniform floor
   (0.1/400 = 0.00025). The other values come out as the spec says: top gate (1001/1002)·(501/1002) = 0.499501, 
   exact to 1e-12. Finding: with the rank mixture, the gate can lift a section at most to about
   `0.9·(0.3 + its other pr)/total + floor/n`. So the ×10 depends on the group ranking low on the other signals,
   which is the case for the real 8-4 pipe section.
6. **Sidecar test 3 uses a fixture.** It is a byte copy of round 1's `test_runtime` sidecar (old binary, same layout)
   at `build/mario_sim/fpg/teacher_pre_exit.bin(.teacher)`. The test restores it with `smb_read_teacher` /
   `smb_teacher_restore` and refreshes with the gate on: 400/400 groups and 1295 attempts restored, probabilities
   sum to 1, 18 groups with gate > 0. It does not go through `fpt_cpu_load`, whose curriculum resume would need the
   fixture's `.curriculum` file. The test prints SKIP if the fixture is missing, for example on another machine.
7. **CUDA covers the new counter only at 0.** In both CUDA parity windows no actor took a pipe (random and scripted
   4-4 actions), so `events[4]` is compared bitwise only at 0. Nonzero values are covered by the CPU tests, which run
   the same `SMB_HD` code. I did not add a CUDA case that scripts a pipe exit.
8. **Not changed:** `sweep_score` and `legacy_sweep_score` still normalise game pixels by 3400, and `sim2rom.cpp`
   still checks `checkpoint_distance <= 3400` with its own default config. The spec does not mention either. With
   `progress_cap = 8192` the sweep score's progress part can now exceed its old range.

## Not run / not verified

- No training: the effect of the exit reward and the gate on learning, and on the 8-4 section-4 gate, is untested.
- `verify_cuda` was not run (bank unchanged); `semantic-test` and `test_parity` were not run either.
- No CUDA case with a nonzero forward-exit count (deviation 7).
