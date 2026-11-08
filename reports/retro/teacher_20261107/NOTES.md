# Retro curriculum teacher: implementation notes (2026-11-07/08)

Implements `SPEC.md` in this directory. `curriculum_teacher.h` and `tests/test_teacher.cpp` are unchanged.
No commits, no `git add`; `./puffer` untouched (the trainer check went to `build/retro/puffer_retro_teacher_check`).
All `make` runs used `-j2`. No training was run.

## Files changed

| File | Change |
|---|---|
| `ocean/retro/retro_archive.h` | The whole feature. `RetroArchiveCell` gains `group` and `states` (vector); bank gains groups, group index, CDF, `snapshots`, teacher params/options and counters; episode gains the attempt record and death-chain ring (`RetroChainEntry`). New helpers: `retro_archive_snapshot` (shared snapshot construction), `retro_archive_snapshot_area` (area key rebuilt from a stored start), `retro_archive_group`, `retro_archive_capturable` (the capture filter), `retro_archive_insert` (variants + reservoir + eviction), `retro_archive_evict` (rules a/b/c), `retro_archive_refresh` / `retro_archive_teacher_pick` (teacher selection), `retro_archive_teacher_step` (attempt outcome, death chain, failure crediting), v2 serialization with v1 reader, `retro_archive_rebuild` after loads. New config keys parsed and validated in `retro_archive_init`. |
| `ocean/retro/retro.h` | One line: `retro_archive_step(..., maze_loops, old_x)`. |
| `ocean/retro/training_config.h` | `teacher/*` logs in `retro_training_log` (only when the teacher is on). Progress CSV unchanged. |
| `ocean/retro/keypoint_states.cpp` | Its private `snapshot()` replaced by `retro_archive_snapshot`; builds cells with `states.push_back`. |
| `ocean/retro/tests/test_archive.cpp` | Hand-built cells use `states={snapshot}`; added checks: one state per cell with the teacher off, `snapshots` exact after capture, round trip and eviction. Same intent, nothing removed. |
| `ocean/retro/tests/test_archive_teacher.cpp` | New: the seven required cases (plus config rejection). |
| `ocean/retro/Makefile` | `teacher-archive-test` target; `curriculum_teacher.h` added to `test_archive` deps. (The `teacher-test` target was already in the working tree, uncommitted, before I started.) |
| `config/retro.ini` | Header comment rewritten; `archive_start = archive_end = 0.5`, `archive_capacity_mb = 512`, `archive_teacher = 1`, all new keys at spec defaults; `[sweep.env.archive_priority]` min = max = 0. |
| `ocean/retro/README.md` | New section "Curriculum teacher" appended (groups, variants, eviction, three signals, attempts, death chain, shares, memory, logs, v2 format). Existing text kept. |

## Commands and results

Baseline before any edit (to know what already failed):
- `make -C ocean/retro -j2 archive-test test` → both PASS (same lines as below).
- `make -C ocean/retro -j2 teacher-test archive-test test sweep-test` → teacher-test PASS; **sweep-test already failed at baseline**:
  `test_training_config: tests/test_training_config.cpp:27: int main(): Assertion 'varying_count==11' failed.`

After the change (re-run on 2026-11-08 from the final tree, all exit 0):

`make -C ocean/retro -j2 teacher-test test archive-test teacher-archive-test`
```
PASS curriculum teacher: EMAs, bias correction, batched outcomes, half-life, kernel, hopeless factor, ranks, mixture, sampling, validation
[retro] ROM ocean/retro/roms/smb1_ntsc.nes fingerprint=6e01246e5d215cb3 mapper=0 World/NTSC; 60.09881390 fps
PASS: archive exact state/image, deterministic continuation, reward ledger, persistence/corruption, mixture, eviction lifetime, terminal boundary, level focus, progress wrap/step cap, maze loops, depth priority, end-first curriculum
[retro] ROM ocean/retro/roms/smb1_ntsc.nes fingerprint=6e01246e5d215cb3 mapper=0 World/NTSC; 60.09881390 fps
[retro] archive_teacher=1: archive_priority, archive_level_focus, archive_depth_priority and archive_backward are ignored
PASS teacher archive: variants, group identity, eviction rules a/b/c, death chain + failure kernel, attempt win/cap/death, teacher selection + spawn gate + lanes, v2/v1 archives + rejection
[retro] ROM ocean/retro/roms/smb1_ntsc.nes fingerprint=6e01246e5d215cb3 mapper=0 World/NTSC; 60.09881390 fps
[retro] PRACTICE return-pipe black screen after 823 prefix frames; segment clock starts at zero
PASS: untimed novelty, revisits, level-entry seeding, reward scaling
PASS: playback frameskip=1: 1666 raw-ROM-identical frames, 2 natural respawns, game-over restart
PASS: playback frameskip=4: 1666 raw-ROM-identical frames, 2 natural respawns, game-over restart
PASS: training natural respawns/game over, viewer single-life override and bounded inspector resets
PASS: unmodified cold boot == saved RTA start at frame 194 (162 after START), offset=0; HUD=24f, bus=21f
PASS: ROM idle return 0.00000000; run-right/game-over return 0.00625000
PASS: no implicit reward on movement, idling, death or timeout
PASS: 32 starts (27 level data pointers), exact reset images, single-level spawn, horizon and return
PASS: 65536 ROM frames across 32 starts, idle fold on/off, shared/private CHR, full/skip render, save/load parity
PASS: actual vector constructor, all spawn coverage, 1/4-worker reproducibility through resets
PASS: 5,120 float32 observations bit-identical to staged reference
PASS: black-screen practice exact prefix/state/image, natural emergence, repeatable resets
```

`make -C ocean/retro -j2 panel viewer` → exit 0 (`build/retro/sweep_eval`, `build/retro/viewer` rebuilt).

`bash build.sh retro build/retro/puffer_retro_teacher_check --float` → exit 0, `Built: ./build/retro/puffer_retro_teacher_check`;
no line containing `error` in the log (only pre-existing warnings from `src/pufferl.cu` and `nes_emu`). `./puffer` unchanged.

Extra checks:
- `make -C ocean/retro sweep-test` → still fails, now one assertion earlier:
  `test_training_config: tests/test_training_config.cpp:21: int main(): Assertion 'lo==puf_ini_get(&ini,name,dot+1)||!strcmp(path,"train.total_timesteps")' failed.`
  Cause: the spec pins `[sweep.env.archive_priority]` to min = max = 0 while `[env] archive_priority` stays 0.5, and the
  test requires a pinned sweep value to equal the env value. It failed at baseline anyway (`varying_count==11`). See open questions.
- `clang++ -fsyntax-only` of every retro tool that touches the archive: `keypoint_states.cpp tas_states.cpp
  keypoint_search.cpp keypoint_eval.cpp seed_maze.cpp pipe_probe.cpp archive_frames.cpp capture_practice.cpp
  maze_cells.cpp` → all ok.
- Mutation check of the new test (scratch copies, tree untouched): skipping no entries in the death chain, flipping
  the rule-a tie break, and `>` instead of `>=` at the attempt cap each make `teacher-archive-test` FAIL with the
  matching message.
- Teacher-off equivalence (scratch probe, not committed): the same randomized scenario (3 envs × 12,000 decisions,
  `archive_enabled=1`, teacher off, `archive_capacity_mb=2` so the 23-cell bank is full and evicting, capture interval
  16, `archive_priority=.7`, `archive_level_focus=.3`, budget 30,000 so the schedule anneals) compiled once against the
  HEAD `retro.h`/`retro_archive.h` and once against the new ones. An FNV hash over every decision's env RNG, reward,
  done, x and mode, the final observation, and every cell's key/visits/selected/failures/successes/last/level:
  ```
  == old
  env 0 cells=23 capacity=23 resets=1/0/19 steps=12000 rng=2695312728 hash=d15d649d38e95e3b
  env 1 cells=23 capacity=23 resets=1/0/46 steps=24000 rng=2391522172 hash=c74180d2b791c671
  env 2 cells=23 capacity=23 resets=1/0/85 steps=36000 rng=3904876770 hash=cc60d4824d49233f
  == new
  env 0 cells=23 capacity=23 resets=1/0/19 steps=12000 rng=2695312728 hash=d15d649d38e95e3b
  env 1 cells=23 capacity=23 resets=1/0/46 steps=24000 rng=2391522172 hash=c74180d2b791c671
  env 2 cells=23 capacity=23 resets=1/0/85 steps=36000 rng=3904876770 hash=cc60d4824d49233f
  ```
  Identical. (The archive file bytes do differ: files are now written as v2. That is intended.)

## Deviations from the spec (and why)

1. **`RetroArchiveCell::state` kept as a read-only mirror of `states.front()`.** `retro.c` (built by `make viewer`,
   which the spec requires) and six diagnostic tools (`tas_states`, `keypoint_search`, `keypoint_eval`, `seed_maze`,
   `pipe_probe`, `archive_frames`) use `cell.state`, and none of them is on the list of files I may edit. The mirror is
   kept in sync on every insert/evict/load; the environment never reads it; the writer stores a cell that only has
   `state` set as a one-state cell. Cost: 16 bytes per cell.
2. **Teacher off keeps the legacy insertion path exactly** (1-in-16 replacement, eviction of a random cell from the
   most represented level, slot reuse, same RNG draws) and the **legacy capacity formula**, using the old
   `sizeof(RetroArchiveCell)` = 72 as a constant (`RETRO_ARCHIVE_V1_CELL_BYTES`); the new formula
   `mb·2^20/(sizeof(snapshot)+64)` applies with the teacher on. The spec routes captures through
   `retro_archive_insert` (done) but its variant/reservoir/eviction rules would change teacher-off behaviour, which the
   hard rule forbids. With the teacher off `variants` is forced to 1. A multi-state v2 bank loaded with the teacher off
   picks a state uniformly (one extra RNG draw only when a cell has more than one state).
3. **Order on a failure: deposit the chain first, then credit the failure**, so groups created by the deposit receive
   this failure's kernel mass (required test 4 needs it; with credit first, a fresh 1-1 run has no groups to credit).
4. **Rule c clears the CDF** (`cdf.clear()`), so the next pick refreshes priorities. Without it the emptied group keeps
   its sampling mass until the 2048-step refresh and a pick could land on a group with no cells. This is the only way
   a refresh happens sooner than 2048 steps.
5. **Float rounding guard in the teacher pick**: `u` can round to 1.0f, and `teacher_cdf` forces the last entry to 1,
   so `teacher_sample` can return the last group even when its prob is 0 (e.g. outside `spawn_levels`). The pick walks
   down to the nearest group with prob > 0. Also, if the drawn group has no cells, or no group is eligible
   (`cdf_eligible` stored at refresh), it falls back like legacy mode 2 → 0. Added bank fields: `cdf_eligible`,
   `spawn_levels` (mask set at init, used for eligibility after a load, since `retro_archive_file` has no Env).
6. **Attempt: win is checked before loss** in the same decision (a decision that both passes `success_distance` and
   dies counts as a win). The spec lists both without precedence.
7. **Teacher-off logging unchanged**: `teacher/*` keys are emitted only when `archive_teacher = 1`.
8. **`archive_teacher = 1` with `archive_enabled = 0` is not an error** (the archive init returns before reading it).
   `sweep_eval` forces `archive_enabled=0` for evaluation, so throwing there would break every panel evaluation.
   `archive_teacher = 1` with `archive_practice = 1` throws as specified.
9. Ranges chosen where the spec gave none: `archive_success_distance` 1..65535, `archive_attempt_decisions`
   1..1e7, `archive_chain_interval` 1..4096, `archive_chain_depth` 1..256, `archive_chain_skip` 0..1e7,
   `archive_upstream_groups` 0..1024, `archive_lp_min_attempts` 1..1e6 (integers), `archive_full_game_share` [0,1].
   The share constraints `start+share ≤ 1`, `end+share ≤ 1` are checked only with the teacher on (legacy configs
   such as start 0.8 must still load).
10. Loaded group tables are validated (key = area<<16|section, level < 32, wins ≤ attempts, finite values, no
    duplicate keys); an invalid file is rejected and the bank is left untouched (the old code replaced the cells
    before its duplicate check).
11. The test's case 4 uses `archive_chain_interval = 4`, `archive_chain_depth = 32`, `archive_chain_skip = 64`
    (via the config) rather than the defaults; see open question 1 for why the defaults deposit everything there.

## Not run

- No training, no sweep, no GPU run of the trainer binary (compile only, as instructed).
- `batch-test`, `panel-test`, `policy-test`, `encoder-test` were not part of the requested runs and were not run.

## Open questions

1. **`chain_skip` counts from the life-loss decision, which lags the fatal hit.** Running into the first Goomba,
   Mario starts dying at decision 33 (x = 312) but the life counter drops at decision 85. With the default skip of 16
   decisions every ring entry (the newest at decision 32, one decision before the hit) is deposited, i.e. the chain
   deposits states from which death is unavoidable. Counting the skip from the last capturable decision (dying onset)
   would match the intent better. Pit deaths lag less. Not changed (spec arithmetic followed).
2. **Maze loop failures are credited at the rewound x**: the frontier `last_x` after a loop is the position Mario
   was sent back to (≈1024 px earlier), not where he took the wrong floor. `old_x` would be the wrong-floor point.
3. **The sweep in `config/retro.ini` will throw on many trials.** `[sweep.env.archive_start]` is still 0.4–0.8 and
   `archive_end` 0.05–0.2; with the fixed 0.3 full-game share every trial with `archive_start > 0.7` fails init, and
   `end ≠ start` anneals away from the 0.5 the spec sets. The spec said to leave everything else as is, so I did.
   Pin them (or narrow `archive_start` to ≤ 0.7) before sweeping; note that editing sweep ranges resets Protein history.
4. `sweep-test` now fails on the pinned `archive_priority` (see above); setting `[env] archive_priority = 0` would fix
   that assertion (the knob is ignored with the teacher on), but the test also fails on `varying_count==11` at
   baseline. Also note `replay_ratio = 1.95590782` in `retro.ini` (untouched).
5. New groups have `prob = 0` until the next refresh, so eviction rules a–c hit them first; at a full bank, cells in
   a just-discovered section lose their extra variants before older ones. Probably fine with 2048-step refreshes.
6. Host memory: at the config defaults (512 MiB bank, 4-entry chain, 1024 envs) expect ≈ 512 MiB + ≈ 370 MiB of
   ring snapshots + one active snapshot per archive episode. Worth checking on the box before launching.

## Review follow-ups (Fable, 2026-11-08)

- **Failure reference moved to the dying onset.** The life counter drops ~50 decisions after a hit, so the chain skip
  and the credited position now use the first decision Mario was dying (`is_dead`) and his position then; a maze loop
  uses the pre-rewind `old_x`. `RetroArchiveEpisode` gained `dying_*` and `last_failure_*` fields; the death-chain test
  asserts the reference is at least 20 decisions before the life-counter drop and the position is before the first
  Goomba (x < 600). The test fixture's `archive_chain_skip` went from 64 to 8 because the onset is at decision ~33.
- **Sweep ranges.** `[sweep.env.archive_start]` and `[sweep.env.archive_end]` are pinned to 0.5 (the fixed share), so
  no trial violates `archive_start + archive_full_game_share <= 1`; `[env] archive_priority = 0` matches its pinned dim.
- **`sweep-test` repaired.** `tests/test_training_config.cpp` expected 11 varying dims from the October layout; it now
  expects the 3 that still vary (anneal_fraction, capture_interval, cell_width). `make -C ocean/retro sweep-test` passes.
- Re-ran after the changes: `test` (12 PASS lines), `archive-test`, `teacher-test`, `teacher-archive-test`, `sweep-test`,
  and `bash build.sh retro build/retro/puffer_retro_teacher_check --float` all pass. Nothing committed.
