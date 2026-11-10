# mario_sim mazes, pipes and route states: implementation notes (2026-11-10)

Implements `SPEC.md` in this directory on top of `reports/mario_sim/teacher_20261107`. Nothing was committed,
staged or stashed. Nothing under `src/`, `ocean/retro/**`, `build.sh` or other envs was touched; `./puffer` was not
written; `build/mario_sim/practice_v2` was not rewritten. The movie was read from
`/mnt/d/puffer_media/mario_tas/smb_warpless_aknell.fm2` and not copied.

## Files changed

| File | Change |
|---|---|
| `ocean/mario_sim/game.h` | `SmbGameProgress` gains `int raw_x, maze_loops;` at the end; `SMB_LOOP_DROP = 900`. The tracked predicate is factored into `smb_game_tracked(m)` (same expression) so the builder and the test use exactly it. In `smb_game_track_progress`, after the predicate and before `p->tracking=1`, the spec's three lines (uncapped `raw`, loop count, `raw_x`). Capped `x`/`last_x`, frontiers and rewards unchanged. |
| `ocean/mario_sim/practice.h` | `SMB_BANK_ROUTE = 32`, `SMB_BANK_EDITED = 64`; `SmbPracticeConfig::loop_terminal = 0` (appended); `SmbTeacherDevice` gains `int pairs; const uint32_t *pair_from, *pair_to;`; `smb_route_pair`, `smb_route_known` (SMB_HD linear scans). |
| `ocean/mario_sim/mario_sim.h` | `Env::teacher_track_deaths` (POD, outside `SmbPracticeProgress`), zeroed in `fpt_reset_state` for every game and practice reset. `smb_after_frame`: captures `was_tracking, k0, x0 = raw_x, loops_before`, computes `loop/regained/forward/backward` exactly as the spec for non-FPG actors with the teacher on, practice win/loss extensions, `loop_terminal` death next to the existing `ram[0xe]==11 \|\| deaths` rule, game-actor failure = new death (as before) or loop or wrong exit, credited once at `(k0, x0/width)` with `events[0]` always and `events[1]`/`events[2]` per condition. Death crediting now uses the uncapped `x0`. `smb_route_pairs` (Part C) + call in `smb_setup_teacher` (`pairs <= 256`, stderr log). Counter block `4G+3`; `smb_teacher_fold` accumulates `loop_events`, `wrong_exit_events`. Config `practice_loop_terminal` (0..1, default 0). Logs `teacher_loop_events`, `teacher_wrong_exit_events`, `teacher_route_pairs`, `game_maze_loops` (new `Log::game_maze_loops`, appended). |
| `ocean/mario_sim/mario_sim.cu` | Device `pair_from/pair_to` buffers uploaded next to the other teacher tables and freed in `puf_close`; counter copies use `4G+3`. |
| `ocean/mario_sim/route_movie.h` | New: the fm2 parser of `ocean/retro/tas_replay.cpp` (same column mapping), with the negative offset. |
| `ocean/mario_sim/build_practice_bank.cpp` | `--route MOVIE.fm2` (Part D). The per-state checks are factored into one lambda that buffers the 16-frame clip and writes it only after the 240-frame check passes; non-route entries still throw the same messages. Route replay, candidate binning, small-Mario snapshot, drop counting, 5% limit, 64 MiB cap, summary fields, area-sequence and pair printout. |
| `ocean/mario_sim/test_teacher_sim.cpp` | Uses the v3 bank; two existing lines also set `raw_x` (crediting now reads it); the stage-share check now requires a share key for exactly the stages present in the bank; new `maze_tests()` (spec tests 1-7). |
| `ocean/mario_sim/test_runtime.cu` | Teacher case: `4G+3` counters, device route table checked against the host, `teacher_track_deaths` in the parity check, host loop/wrong-exit totals checked against the device counters after the forced refresh. |
| `ocean/mario_sim/README.md` | "Curriculum teacher": mazes and pipes, route table, v3 bank; the "no synthetic RAM edits" sentence corrected. |
| `config/mario_sim.ini` | `practice_bank = build/mario_sim/practice_v3/bank.bin`, `practice_loop_terminal = 1`, one comment line each. No `[sweep.*]` change. |

Build artifacts written: `build/mario_sim/practice_v3/` (new bank), rebuilt `build/mario_sim/fpg/{build_practice_bank,
test_teacher_sim,test_modes,test_game_rom,test_curriculum_cpu,test_curriculum_cuda,test_practice,test_cpu,test_cuda}`,
`build/mario_sim/runtime/{test_runtime,bench_env}`, the compile checks `build/mario_sim/puffer_mario_sim_maze_check{,_cu}`,
and the usual test sidecars. The no-`--route` rebuild and the exploration programs live in the session scratchpad only.

## Commands run and results

All from the repository root, `OMP_NUM_THREADS=2`, `-j2`, `CUDA_ARCH=sm_61` where CUDA is built.

1. `make -f ocean/mario_sim/fpg.mk -j2 build/mario_sim/fpg/build_practice_bank` -> built, no warnings.
2. No-`--route` rebuild of v2's command into a scratch directory
   (`build_practice_bank <scratch>/practice_v2_repro --per-stage 48 config/mario_sim.ini <the two models>`, 2m57s), then
   `cmp` against `build/mario_sim/practice_v2/` -> **identical**: `bank.bin`, `summary.json`, `states.jsonl`,
   `validation.bin`, `routes.jsonl`; `diff -r` of `states/` and `routes/` identical.
3. v3 build, exactly the spec command (4m08s):
   ```
   build/mario_sim/fpg/build_practice_bank build/mario_sim/practice_v3 --per-stage 48 \
     --route /mnt/d/puffer_media/mario_tas/smb_warpless_aknell.fm2 config/mario_sim.ini \
     checkpoints/mario_sim/sweep_1792016021935_0000/0000006442450944.bin \
     checkpoints/mario_sim/sweep_1792052933835_0001/0000009999745024.bin
   ```
   `route movie: 67115 frames, end 8-4 mode=2 x=4804 deaths=0 errors=0 candidates=1170` ->
   `route states=1170 dropped=0 edited=1112` -> `practice states=1478 stages=32 bytes=60567026 CPU_ROM_frames=354720`.
   The builder's 240-frame native-vs-ROM check passed for all 1,478 states (none dropped).
4. `build/mario_sim/runtime/verify_cuda build/mario_sim/practice_v3/validation.bin` (GTX 1060, sm_61 module) ->
   `CUDA 1478/1478 selected cases, 23648 frames, 0 failures`, `"bitwise_observations":true`.
5. `make -f ocean/mario_sim/fpg.mk -j2 CUDA_ARCH=sm_61 game-test curriculum-test teacher-test` -> exit 0 (13m41s):
   `test_modes` `"failures":0`; `test_game_rom` `"semantic_and_lifecycle_parity":true,"failures":0` (72,110 ROM frames);
   `test_teacher_sim` (runs twice, from game-test and teacher-test) `PASS ...` with all new maze lines (below);
   `build_table` cache hit; `test_curriculum_cpu` `"failures":0`; `test_curriculum_cuda` bitwise, `"failures":0`.
6. `make -f ocean/mario_sim/runtime.mk -j2 CUDA_ARCH=sm_61 environment` -> exit 0 (38 s, no warnings from mario_sim);
   `build/mario_sim/runtime/test_runtime` -> exit 0 (32m34s):
   ```
   mode=game/fpg/mixed/practice ... parity=passed
   mode=teacher actors=128 practice=32 groups=400 parity_steps=256 steps=2000 parity_attempts=170 parity_wins=140
     parity_failures=0 parity_loops=0 parity_wrong_exits=0 route_pairs=6 selected=1311 attempts=1295 wins=1036
     death_events=20 loop_events=0 wrong_exit_events=0 stream_refreshes=27 agent_steps=242688 entropy=5.9812 parity=passed
   {"decisions":229376,"resets":3671,"stages":32,"modes":4,"practice_checkpoint_resume":true,"teacher":true,"failures":0}
   ```
   Bitwise parity covers the v3 bank, the device route table (checked equal to the host's), `teacher_track_deaths`
   and all `4G+3` counters after every step; after the forced refresh the host's death/loop/wrong-exit totals equal
   the device counters. **Limit:** with random actions over 256 parity steps no failure, loop or wrong exit happened
   inside the parity window (counts above are 0), so parity of those three counters is shown only at value 0 there;
   the arithmetic itself is exercised on the CPU path by test_teacher_sim (shared SMB_HD code).
7. Trainer compile checks:
   `CUDA_HOME=/usr/local/cuda bash build.sh mario_sim build/mario_sim/puffer_mario_sim_maze_check` -> exit 0,
   `Built: ./build/mario_sim/puffer_mario_sim_maze_check` (1m00s);
   `... build/mario_sim/puffer_mario_sim_maze_check_cu --cu` -> exit 0 (1m23s). Only the pre-existing `src/pufferl.cu`
   warnings (#550 `explicit_render`, `-Wformat-truncation`); none from `ocean/mario_sim`. `./puffer` timestamp unchanged
   (2026-11-04 21:45:47) before and after.
8. `make -f ocean/mario_sim/fpg.mk -j2 build/mario_sim/fpg/test_practice && build/mario_sim/fpg/test_practice` ->
   `PASS practice pools, ...` (legacy band machine, v1 bank).
9. `make -f ocean/mario_sim/fpg.mk -j2 CUDA_ARCH=sm_61 test` -> exit 0: `test_cpu` `"failures":0`, `test_cuda` bitwise,
   `"failures":0`.

New `test_teacher_sim` output (identical in both runs):
```
groups=400 entries=1478 per_stage: 1-1=13 1-2=13 1-3=10 1-4=9 2-1=13 2-2=14 2-3=14 2-4=9 3-1=12 3-2=13 3-3=10 3-4=9 4-1=15 4-2=14 4-3=10 4-4=11 5-1=12 5-2=13 5-3=10 5-4=9 6-1=12 6-2=13 6-3=11 6-4=9 7-1=11 7-2=14 7-3=15 7-4=13 8-1=24 8-2=13 8-3=14 8-4=18
sampling: 20000 draws, max |freq-prob|=0.00191, prob range 0.00037..0.01736
crediting: section=23 of area 001caa01 credited 7 groups
route table: hand-built bank gives 4 pairs as expected
practice attempts: forward win, wrong exit loss (terminal on/off), loop loss (terminal on/off), unknown/255/lost-life/no-area/other-stage none, 300 back loss, 600 forward win
game failures: loop credited 7 groups; death at x=3600 credited section 14 of area 000ca539
native movie replay: 66868 frames, 1170 route states equal the replay, 42 on-ground 4-4/7-4/8-4 sections all have groups
real-game attempts along the movie (8-4 gaps: 64993-65163 65430-65600 65795-65965 66661-66835):
  8-4 before pipe 1 (movie loops first): route state frame 64960 x=1217 (small-Mario edit) -> LOSS after 27 frames at 1fa3fc x=259, maze_loops=1
  8-4 before pipe 2 (same-area forward jump, distance win): edited state (frame 65409) gave loss after 617 frames at 1fa3fc x=1501 (status 2, deaths 0, maze_loops=1); retrying with the unedited movie state
  8-4 before pipe 2 (same-area forward jump, distance win): route state frame 65409 x=2419 (unedited) -> WIN after 191 frames at 1fa3fc x=3128, maze_loops=0
  8-4 before pipe 3 (a3fc -> aec2, forward route pair): route state frame 65747 x=3446 (small-Mario edit) -> WIN after 218 frames at 1faec2 x=56, maze_loops=0
  4-4 across the first maze check (distance win): route state frame 31323 x=706 (small-Mario edit) -> WIN after 205 frames at 0fa212 x=1218, maze_loops=0
real-game attempts: 4 cases as expected
PASS teacher groups, reset sampling, attempt outcomes, failure crediting, refresh and cadence, metrics, sidecars and legacy mode
```
What the new cases check (spec numbering): (1) 1300 -> 276 gives one loop, the same with an untracked frame between
gives 0, 899 px gives 0, exactly 900 gives 1, a drop into another area gives 0, `raw_x = 3600` with `last_x = 3400`.
(2) `smb_route_pairs` on a hand-built bank (stage 31: A A B A plus a non-route C; stage 30 uses the same pointer A) gives
exactly `(30:A,30:E),(30:E,30:A),(31:A,31:B),(31:B,31:A)`; no route flag -> no pairs. (3)/(4) on the practice actor with a
temporary route table: every case in the printed line, each with exactly one record (or none), the expected episode
status, and no failure events from practice actors. (5) on a game actor placed in a bank state: reward, episode and
`game` struct are bitwise equal to the same frame run without the teacher; loop -> kernel at `(k0, x0/256)` +
`events[0]` + `events[1]`; wrong exit (other area, and a 256-px same-area jump back) -> `events[0]` + `events[2]`; a
lost life during the gap -> no counter changes; death at x = 3600 -> the section-14 group gets `U+1`. (6) every stage has
route states; the 8-4 pairs are exactly `1fa3fc->1faec2, 1faec2->1fa3fc`; a native replay of the movie from the earliest
route state (1-1, frame 246, unedited) reaches 8-4 victory and reproduces **all 1,170 route states** (RAM, registers,
frame clock) up to the two edited bytes; every 256-px section the movie passes on the ground in 4-4, 7-4 and 8-4 (42) has
a group; the legacy loader (`practice_teacher = 0`) accepts v3 (existing legacy block, now on v3). (7) below.

## v3 bank: per stage

Route states (all 1,170 kept, 0 dropped), teacher groups at width 256 (in brackets: groups containing route states),
and the movie's tracked area sequence printed by the builder:

| stage | route states | groups (with route) | areas | | stage | route states | groups (with route) | areas |
|---|---|---|---|---|---|---|---|---|
| 1-1 | 16 | 13 (7) | a690 ad7b a690 | | 5-1 | 31 | 12 (12) | a83d |
| 1-2 | 34 | 13 (13) | ac37 a690 | | 5-2 | 37 | 13 (13) | aa94 |
| 1-3 | 23 | 10 (10) | a6f5 | | 5-3 | 26 | 10 (10) | a6f5 |
| 1-4 | 26 | 9 (9) | a1b1 | | 5-4 | 29 | 9 (9) | a291 |
| 2-1 | 38 | 13 (13) | a7cf | | 6-1 | 35 | 12 (12) | a95d |
| 2-2 | 49 | 14 (14) | ae47 a690 | | 6-2 | 38 | 13 (13) | a58c |
| 2-3 | 37 | 14 (14) | a74a | | 6-3 | 29 | 11 (11) | a8f8 |
| 2-4 | 30 | 9 (9) | a291 | | 6-4 | 26 | 9 (9) | a1b1 |
| 3-1 | 36 | 12 (12) | a61b | | 7-1 | 35 | 11 (11) | ab80 |
| 3-2 | 46 | 13 (13) | ac04 | | 7-2 | 50 | 14 (14) | ae47 a690 |
| 3-3 | 24 | 10 (10) | a46d | | 7-3 | 42 | 15 (15) | a74a |
| 3-4 | 30 | 9 (9) | a304 | | 7-4 | 46 | 13 (13) | a371 |
| 4-1 | 47 | 15 (15) | a539 | | 8-1 | 71 | 24 (24) | aa01 |
| 4-2 | 41 | 14 (14) | acda a690 | | 8-2 | 34 | 13 (12) | ab07 |
| 4-3 | 27 | 10 (10) | a891 | | 8-3 | 41 | 14 (14) | a4d0 |
| 4-4 | 41 | 11 (11) | a212 | | 8-4 | 55 | 18 (18) | a3fc aec2 a3fc |

Totals: 1,478 entries (308 v2 + 1,170 route), 400 groups (v2: 79), 1,112 route entries carry the small-Mario edit
(the 58 unedited ones are where the movie's Mario is small: all of 1-1 (16) and 1-2 (34), and the first 8 of 1-3
before the movie powers up). This differs slightly from the spec's "small in 1-1 to 1-3": by `$0754/$0756` the
movie's Mario stops being small partway through 1-3 (the remaining 15 1-3 route states are edited). `summary.json` has `"synthetic_RAM_edits":1112,"route_states":1170,"route_dropped":0` and per
stage `{route_states, areas}` under `route_stages`. Size 60,567,026 bytes (cap 64 MiB), of which `validation.bin`
52.6 MB. **The area sequence equals the spec's list exactly** (1-1 `a690 -> ad7b -> a690`, 1-2 `ac37 -> a690`, 2-2
`ae47 -> a690`, 4-2 `acda -> a690`, 7-2 `ae47 -> a690`, 8-4 `a3fc -> aec2 -> a3fc`; all other stages one area).

## Pair list (route table, from the bank's route entries)

```
1-2: ac37 -> a690    2-2: ae47 -> a690    4-2: acda -> a690    7-2: ae47 -> a690
8-4: a3fc -> aec2    8-4: aec2 -> a3fc
```
(keys with stage bits: `01ac37->01a690 05ae47->05a690 0dacda->0da690 19ae47->19a690 1fa3fc->1faec2 1faec2->1fa3fc`).
**1-1 has no pair**: the movie visits `ad7b` (coin room, tracked x 24..195) but no frame there passes `useful()`
(x >= 128 and standing), so no route state exists in `ad7b` and Part C's rule (pairs from bank entries) cannot see it.
Effect: in 1-1 entering and leaving the coin room is "unknown" (no transition outcome); a practice attempt that comes
out at x 2616 still wins by the distance rule.

## Dropped states

None: 0 of 1,170 route candidates failed the reset or rollout check (the code drops, counts and prints a reason per
state, and fails above 5%).

## Deviations from the spec, with reasons

1. **Test 7, 8-4 "just before the first pipe": the attempt records a LOSS, not a win.** This is the spec's own quirk:
   the movie is looped 1282 -> 261 (a 1,021-px same-area drop, `maze_loops` +1) 27 frames before it enters the pipe at
   x 276, so under Part B an attempt started before x 1282 loses on that loop (and with `loop_terminal = 1` ends as a
   death). The last route state before that pipe is at x 1217 (frame 64960); the post-loop frames (x 261..273) fall in
   bins already filled at the level start, so there is no route state between the loop and the pipe. I kept the spec's
   rules ("do not fix it") and made the test assert what they give (loss, one loop), and added two more 8-4 cases that
   do win: before pipe 2 (same-area forward jump 2419 -> 3128, distance win) and before pipe 3 (`a3fc -> aec2`, forward
   pair win). See open question 1: this is not only a builder matter.
2. **Test 7, 8-4 before pipe 2 uses the unedited snapshot** (allowed by the spec): with the small-Mario edit, the movie's
   inputs from x 2419 miss the pipe, Mario runs on and is looped (x 1501 after 617 frames; loss, `maze_loops = 1`). The
   unedited movie state (the native replay's state at that frame, equal to the bank entry except `$0754/$0756`) wins.
   The 8-4 pipe-1 and pipe-3 cases and the 4-4 case use the edited bank states as they are.
3. **Test 6 and 7 use a native replay of the movie instead of the ROM core.** `test_teacher_sim` cannot include the
   retro core (`ocean/retro/retro.h` defines its own `Env`). It replays the movie on the native engine from the earliest
   (unedited, 1-1) route state; the test first proves that replay matches the bank (all 1,170 route states equal it) and
   ends in 8-4 victory, then uses it for the on-ground sections and the real-game attempts. If the movie file is
   missing the test prints `SKIP movie checks` and still runs the rest (it was present for every run reported here).
4. **The "first maze check" of 4-4 is taken as x = 1024** (found by probing route states with held inputs: wrong-floor
   loops 1025 -> 1 and 2084 -> 1063). The test picks the last 4-4 route state with `x < 768` and `x + 512 > 1040`
   (x 706); the attempt wins by distance at x 1218 with no loop.
5. **Band of a route entry** = quartile of its position among the stage's *kept* route entries (`k*4/n` in movie
   order), assigned after the checks so a drop cannot empty a band. With 0 drops this is identical to "among its
   stage's route entries".
6. **Per-state check refactor in the builder**: the existing native-reset/240-frame/clip code is now one lambda that
   buffers the 16 clip frames and writes them after the check passes (route entries must be droppable). Non-route
   entries throw the same messages; output verified byte-identical (command 2).
7. **`summary.json` `"stages"`** with `--route` is the number of stages in the bank (32); without `--route` it is the old
   count (13, unchanged). Extra keys `route_stages` (per stage `{route_states, areas}`, areas as the hex sequence) and
   `route_pairs` (the preview list). `states.jsonl` route lines add `edited`, `movie_size`, `movie_status`.
8. **`smb_game_tracked(m)`** was factored out of `smb_game_track_progress` (identical expression) so the builder and the
   test use exactly the tracked predicate. New header `route_movie.h` (fm2 parser) shared by builder and test.
9. **`game_maze_loops`** is logged regardless of `practice_teacher` (it is a plain game metric; `maze_loops` is counted
   by `game.h` for every game actor). Everything in Part B, including `loop_terminal`, acts only with the teacher on.
10. `teacher_track_deaths` is updated only with the teacher on (Part B is "teacher on only"); it is zeroed at every
   game/practice reset in `fpt_reset_state` regardless.

## Open questions / findings for review

1. **Every correct play of 8-4's first section registers a maze loop.** From the unedited movie state at x 1217 I
   held Down from each of the 46 frames before the loop: no pipe entry happened before the loop trigger (Mario crosses
   it near x 1262-1282 when sliding); the pipe was entered only after the loop (x 276, exits at 1848). If that holds for
   all approaches, then under these rules a game actor that takes the correct first 8-4 pipe is credited a loop
   failure at 8-4 section 5, and every practice attempt from 8-4 sections 0-4 loses and (with `loop_terminal = 1`)
   ends at the loop, so those groups can never be won. Possible fixes (not implemented): exempt a loop that is followed
   by a pipe gap within N frames, or treat 8-4's first loop as neutral. Needs a decision.
2. **Leaving an off-route area counts as a wrong exit.** `backward` needs only `k1` known: returning from an unknown
   bonus room (vine/coin room not in the table) into a route area that is not paired with it is a wrong exit, even if
   the return is forward. In v3 the only off-route areas a policy can enter are bonus rooms; 1-1's own coin room
   `ad7b` -> `a690` is such a case only if stage 0's `a690` were known, which it is not (no 1-1 pair). Flagging because
   the spec's prose says unknown areas produce "neither outcome".
3. **x wrap at a loop-back below x 1024.** When the 4-4 check fires at x < 1024 Mario lands at x < 0 (`raw_x = 65535`)
   for one frame; the loop is still counted once (on the next frame, 65535 -> 0) but its credit section is 65535/256 =
   255, where no group exists, so that loop credits no group (it still counts in `events[0]`/`events[1]`).
4. The small-Mario edit can change the route (deviation 2). 1,112 of 1,170 route states are edited; whether that is
   the right default for later rooms (where real arrivals may be big) is a training question.
5. Unchanged from the teacher notes: `config/mario_sim.ini` still has `[sweep.train.replay_ratio]` and the
   `[sweep.policy.*]` size dimensions; not touched (the spec forbids `[sweep.*]` edits here).

## Could not run / not verified

- No training (not allowed locally): the effect of the new failure signals and of `loop_terminal` on learning, and
  their real rates inside the trainer's CUDA graph, are untested.
- In the CUDA parity window no loop, wrong exit or failure occurred (random actions), so CPU/CUDA equality of those
  counters is shown only at 0 there (see command 6); the shared SMB_HD code path is the same on both backends.
- `semantic-test`, `progress-test` and `test_parity` were not run (not required; no observation code changed).

# Review round 1 (2026-11-10)

Corrections R1 to R4 of SPEC.md "Review round 1" are implemented. The text above describes round 0 and is kept as it
was. Where it conflicts with this section (the pipe-1 result in test 7, the "Limit" under command 6, and the first
"could not run" bullet about parity of the event counters), this section is the current state.

## Changes

- `game.h`: `smb_game_x(m)`, signed 16-bit x. `raw_x` and the loop test use it, so a loop-back is a drop of exactly
  1,024. The reward x in `smb_game_track_progress` is `clamp(smb_game_x(m), 0, 3400)`.
- `practice.h`:
  - Flag `SMB_BANK_ROUTE_LOOP = 128`.
  - New `SmbTeacherDevice` fields: `loops`, `loop_key` (uint32) and `loop_x` (int32).
  - `smb_route_loop(t, k, x)` exactly as specified.
  - `events[3]` counts passed route loops.
- `mario_sim.h`:
  - Env gets `teacher_route_loops` and `teacher_loop_land`. Both are zeroed at every reset, and on every frame where
    tracking is regained (`!was_tracking && tracking`).
  - The host builds the loop table from the `ROUTE|ROUTE_LOOP` entries (at most 16). Each entry stores its key and
    `fpt_x`. The table is logged at start-up as `on-route loops=1: 8-4:a3fc x>=1217`.
  - Part B follows the spec: the `passed` / `credit_x` block. `backward`'s different-area branch now also needs
    `smb_route_known(k0)` (R3).
  - The attempt win rule uses `smb_game_x`. The credit section is `max(0, credit_x) / group_width`.
  - The counter block is `4G+4`. The fold adds metric `teacher_route_loops_passed`.
- `mario_sim.cu`: the loop table goes to the device (allocated if non-empty, freed in `puf_close`), and counter
  copies use `4G+4`.
- `build_practice_bank.cpp`:
  - Runs a `SmbGameProgress` along the movie replay.
  - Each movie loop-back records `(stage, key, x before, frame)`.
  - Drops route candidates between a loop-back and the next untracked frame, and counts them.
  - Flags the kept route entry of that stage and key with the largest `x <= xl` and `source_frame < loop frame`. It
    fails if there is none or if `xl - x > 512`.
  - Prints `route loops (...)` and `route loop entry: ...`.
  - `states.jsonl` gets `"route_loop":0/1` on route lines. `summary.json` gets `route_loops[]` and
    `route_dropped_after_loop`.
- Tests: `test_teacher_sim.cpp` (R1, R2 (a)-(d), R3; `4G+4`) and `test_runtime.cu` (R4; device loop table;
  `teacher_route_loops` / `teacher_loop_land` in the parity compare; `route_loops_passed` checked against the device
  after the forced refresh; `4G+4`).
- `README.md`: new "Signed x" bullet, which calls this a deliberate fix of legacy behaviour. Updated "Mazes and pipes"
  for R3, a new "On-route loops" bullet, and the dropped-after-loop rule in the v3 bullet.

## Bank rebuild: loops found and drops

`rm -rf build/mario_sim/practice_v3`, then the same spec command as round 0 (1m18s wall, run alongside the repro):
```
route movie: 67115 frames, end 8-4 mode=2 x=4804 deaths=0 errors=0 candidates=1170
route loops (1): 8-4:a3fc x=1282 frame=64987; candidates dropped between a loop and the next untracked frame: 0
route loop entry: 8-4:a3fc frame=64960 x=1217 (loop at x=1282)
route pairs (6): 1-2:ac37->a690 2-2:ae47->a690 4-2:acda->a690 7-2:ae47->a690 8-4:a3fc->aec2 8-4:aec2->a3fc
route states=1170 dropped=0 edited=1112
practice states=1478 stages=32 bytes=60584576 CPU_ROM_frames=354720
```
- **Loops found: exactly one**, 8-4 area a3fc, x 1282 → 258, movie frame 64987. The movie never loops in 4-4 or 7-4.
- **Candidates dropped between a loop and the next untracked frame: 0.** After the throw-back the movie enters the pipe
  (gap from frame 64993) before any new 64-px bin's first standing frame comes up.
- **Flagged entry:** 8-4 a3fc, frame 64960, x 1217, which is 65 px before the loop. This is the entry round 0's notes
  predicted.
- Route states, per-stage counts, groups (400) and pairs are identical to round 0, so the per-stage table above still
  holds.
- The total size grew by 17,550 bytes. That is exactly 1,170 × `,"route_loop":0|1` in `states.jsonl`.
  `bank.bin` differs only in the one flag.

## Commands run and results (round 1)

All from the repository root with `OMP_NUM_THREADS=2` and `-j2`. Logs are in the session scratch directory.

1. `make -f ocean/mario_sim/fpg.mk -j2 build/mario_sim/fpg/build_practice_bank` → built.
2. No-`--route` repro into a fresh scratch directory (`practice_v2_repro_r1`, 45 s), then `cmp` against
   `build/mario_sim/practice_v2/`. Result: **identical** `bank.bin`, `summary.json`, `states.jsonl`, `validation.bin`
   and `routes.jsonl`; `diff -r` of `states/` and `routes/` identical.
3. v3 rebuild (above) → exit 0.
4. `build/mario_sim/runtime/verify_cuda build/mario_sim/practice_v3/validation.bin` →
   `CUDA 1478/1478 selected cases, 23648 frames, 0 failures`, `"bitwise_observations":true`.
5. `make -f ocean/mario_sim/fpg.mk -j2 game-test curriculum-test teacher-test` → exit 0 (8m39s). CUDA_ARCH was not
   passed here, so the makefile default `native` applied (this GPU is sm_61). Results:
   - `test_modes` `"failures":0`.
   - `test_game_rom` `"semantic_and_lifecycle_parity":true,"failures":0` (72,110 ROM frames).
   - `test_teacher_sim` ran twice, PASS both times (output below).
   - `test_curriculum_cpu` `"failures":0`.
   - `test_curriculum_cuda` `"bitwise_curriculum":true,"failures":0`.
6. `make -f ocean/mario_sim/fpg.mk -j2 CUDA_ARCH=sm_61 progress-test` → exit 0. Both `test_progress` and
   `test_progress_cuda` print `PASS checkpoint boundaries, backtracking, deaths, pipe revisits, ...`. This was run
   because R1 changes the reward x.
7. `make -f ocean/mario_sim/fpg.mk -j2 build/mario_sim/fpg/test_practice && build/mario_sim/fpg/test_practice` →
   `PASS practice pools, ...`.
8. `make -f ocean/mario_sim/fpg.mk -j2 CUDA_ARCH=sm_61 test` → exit 0. `test_cpu` `"failures":0`; `test_cuda` bitwise,
   `"failures":0`.
9. `CUDA_HOME=/usr/local/cuda bash build.sh mario_sim build/mario_sim/puffer_mario_sim_maze_check` → exit 0, `Built`.
   `... build/mario_sim/puffer_mario_sim_maze_check_cu --cu` → exit 0, `Built`. The only warnings are the existing
   `src/pufferl.cu` ones; none come from `ocean/mario_sim`.
10. `make -f ocean/mario_sim/runtime.mk -j2 CUDA_ARCH=sm_61 environment` → exit 0 (15 s).
11. R4 trial run before the full suite: a scratch copy of `test_runtime.cu` whose `main` runs only the two teacher
    cases (built with the same nvcc line, sm_61). Result: exit 0 (9m00s).
    ```
    mode=teacher-maze 4-4 route_states=41 parity_frames=256 parity_attempts=170 parity_failures=13 parity_loops=10 parity_wrong_exits=0 parity_route_loops_passed=0 parity=passed
    mode=teacher actors=128 practice=32 groups=400 parity_steps=256 steps=2000 parity_attempts=170 parity_wins=140 parity_failures=0 parity_loops=0 parity_wrong_exits=0 parity_route_loops_passed=0 route_pairs=6 selected=1311 attempts=1294 wins=1038 death_events=20 loop_events=0 wrong_exit_events=0 route_loops_passed=1 stream_refreshes=27 agent_steps=242688 entropy=5.9812 parity=passed
    ```
    Bitwise CPU/CUDA parity, including all `4G+4` counters after every frame, now holds with **10 loop failures and 13
    failures in total** inside the window. During the live 2000-step run one actor passed the 8-4 on-route loop
    (`route_loops_passed=1`, folded from the device).
12. `build/mario_sim/runtime/test_runtime` (full suite, which now includes the maze case) → exit 0 (2m35s wall this time; round 0's run took 32m34s on the same machine,
    probably because of contention, which was not investigated). game/fpg/mixed/practice `parity=passed`; the
    `mode=teacher` and `mode=teacher-maze` lines are identical to command 11;
    `{"decisions":262144,"resets":3671,"stages":32,"modes":4,"practice_checkpoint_resume":true,"teacher":true,"failures":0}`.
13. `./puffer` timestamp before and after → `2026-11-04 21:45:47` both times, so it was not written.

New `test_teacher_sim` lines (the rest of its output is as in round 0):
```
practice attempts: forward win, wrong exit loss (terminal on/off), loop loss (terminal on/off), off-route-exit/unknown/255/lost-life/no-area/other-stage none, 300 back loss, 600 forward win
R1 signed x: wrapped loop-back = 1 loop, no win, credit section 3, frontier stays 1000
R2 on-route loop: 8-4 x>=1217; passed once, second loop = loss/failure at section 5, deaths after it at sections 5 and 2, 4-4 never passed
  8-4 before pipe 1 (on-route loop passed, distance win): edited state (frame 64960) gave no outcome before the movie ended after 2154 frames at 1fa3fc x=402 (status 0, deaths 0, maze_loops=1); retrying with the unedited movie state
  8-4 before pipe 1 (on-route loop passed, distance win): route state frame 64960 x=1217 (unedited) -> WIN after 203 frames at 1fa3fc x=1848, maze_loops=1
  8-4 before pipe 2 ... (unedited) -> WIN after 191 frames at 1fa3fc x=3128   (as round 0)
  8-4 before pipe 3 ... (small-Mario edit) -> WIN after 218 frames at 1faec2 x=56   (as round 0)
  4-4 across the first maze check ... -> WIN after 205 frames at 0fa212 x=1218   (as round 0)
```

What the new checks assert:
- **R1.** Practice actor with `teacher_x0 = 600`, then x 1000, three frames with page 0xFC (x −792, −791, −790), then
  x 5:
  - exactly one loop, and the attempt is recorded as a loss (no win);
  - the same sequence on a game actor gives `events[0]+1` and `events[1]+1`, with the integer kernel at section
    1000/256 = 3;
  - the area's reward frontier stays at 1000 (it would have been 3400 with unsigned x).
- **R2 (a).** Test 7 pipe 1 is now a WIN, with `events[3]` +1 and `events[1]` +0. These deltas are measured over the
  run that decides the case (see deviation 1).
- **R2 (b).** Practice actor on the flagged entry:
  - x 1282 → 258 is passed: still pending, not terminal, `events[3]+1`, `teacher_loop_land == 259`;
  - x 1282 → 258 again is a loss and ends the episode as DEAD;
  - on a game actor, the second loop is a failure (`events[0]+1`, `events[1]+1`) credited at section 5 (x0 = 1282).
- **R2 (c).** Game actor: after a passed loop, 20 tracked frames walking to x 270, then a life lost at x 270 → kernel
  at section 5 (credit_x = 1294). The same at x 600 → section 2.
- **R2 (d).** A game actor on a 4-4 route state with x 1025 → 1: `events[1]+1`, `events[3]+0`.
- **R3.** Practice actor: the previous area is not on the route and the new area is on it → no outcome, not terminal,
  no events.
- The table check: exactly one loop entry (`0x1fa3fc`, x 1217), and it is in the runtime bank.

## Deviations (round 1), with reasons

1. **Test 7 pipe 1 uses the unedited movie state.** From the small-Mario-edited state at frame 64960, the movie's
   inputs pass the on-route loop correctly (`maze_loops=1`, no loss) but do not enter the pipe, because big and small
   Mario differ. The attempt was still undecided when the movie ended (2154 frames, below
   `attempt_frames = 2400`). `real()` used to fail on that; it now returns "no outcome" when the movie ends first, and
   the case retries with the unedited native-replay state. This is the same retry pipe 2 already used in round 0, and
   the spec allows it. The unedited state wins at x 1848 after 203 frames. The `events[3]` / `events[1]` deltas are
   taken over the deciding run, so the check is `events[3] == +1`, `events[1] == +0` exactly.
2. **The R4 window runs at least 256 frames and then until `events[1] > 0`, not only until `events[1] > 0`.** One 4-4
   route state (scene 1069, taken just before the movie's x-1024 check) loops back on the very first frame under
   Right+B+A. Stopping there would have compared one frame and no practice attempts. With the 256-frame minimum the
   window has 10 loops, 13 failures and 170 attempts.
3. **R4 setup.**
   - Game actors are placed on the 41 v3 4-4 route states with `smb_practice_load`, as `test_teacher_sim`'s `place()`
     does. Later resets go to the 4-4 start via `fixed_stage = 15`, with game `max_frames = 3000`.
   - Only game actors are scripted, with `(frame + 7i) % 40 < 20 ? 35 : 34` (Right+B, A held half the time). Practice
     and FPG actors keep the existing case's random actions, so attempts are compared too.
   - It is a parameter of the existing `teacher_case` rather than a separate case, and it skips the live-refresh and
     sidecar part.
4. **The R1 test uses page 0xFC as written.** From x 1000 (page 3) the ROM would wrap to page 0xFF (x −24). The test
   follows the spec's page 0xFC (x −792): the drop is 1,792 rather than 1,024, but every asserted outcome is the same.
5. The R2 (b)–(d) and R1 cases drive x through RAM writes (`ram[0x6d]`, `ram[0x86]`) and `smb_after_frame`, like
   tests 3–5, not through engine frames. The real-engine evidence for R2 is test 7 pipe 1 plus R4.
6. The test harness's game-actor `place()` now also zeroes `teacher_route_loops` and `teacher_loop_land`. Its
   `table()` helper turns the loop table off together with the test pair table, and both are restored afterwards.
   This affects the tests only.

## Could not run / not verified (round 1)

- No training. The effect of passed loops and `credit_x` on learning is untested.
- `credit_x` (+1,024) is exercised only with synthetic positions (R2 (c)) and in the parity windows. No real-engine
  death right after the 8-4 throw-back was produced on purpose.
